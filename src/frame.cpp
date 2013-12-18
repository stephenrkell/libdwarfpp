/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * frame.cpp: .debug_frame and .eh_frame decoding
 *
 * Copyright (c) 2013, Stephen Kell.
 */

#include <limits>
#include <map>
#include <set>
#include <cassert>
#include <boost/optional.hpp>
#include <boost/icl/interval_map.hpp>
#include <srk31/endian.hpp>
#include <gelf.h>

#include "lib.hpp"
#include "frame.hpp" 

using std::map;
using std::pair;
using std::make_pair;
using std::set;
using std::string;
using std::cerr;
using std::endl;
using dwarf::core::FrameSection;
using dwarf::core::Fde;
using dwarf::core::Cie;
using dwarf::spec::opt;
using boost::icl::interval;
using boost::optional;
using srk31::host_is_little_endian;
using srk31::host_is_big_endian;

namespace dwarf
{
	namespace encap
	{
		std::ostream& operator<<(std::ostream& s, const frame_instr& arg) 
		{
			const char *opcode_name;
			int ret = dwarf_get_CFA_name(arg.fp_base_op << 6 | arg.fp_extended_op, &opcode_name);
			assert(ret == DW_DLV_OK);
			if (string(opcode_name) == "DW_CFA_extended") opcode_name = "DW_CFA_nop";
			s << "<" << opcode_name << ": reg " << arg.fp_register 
				<< ", offset/blklen " << arg.fp_offset_or_block_len;
			if (arg.fp_expr_block && arg.fp_offset_or_block_len) 
			{
				s << ", expr " << encap::loc_expr(arg.dbg, arg.fp_expr_block, arg.fp_offset_or_block_len);
			}
			s << ", instroff " << arg.fp_instr_offset << ">";
			return s;
		}
		std::ostream& operator<<(std::ostream& s, const frame_instrlist& arg)
		{
			s << "[";
			for (auto i_instr = arg.begin(); i_instr != arg.end(); ++i_instr)
			{
				if (i_instr != arg.begin()) s << ", ";
				s << *i_instr;
			}
			s << "]";
			return s;
		}
		Dwarf_Unsigned read_uleb128(unsigned char const **cur, unsigned char const *limit)
		{
			Dwarf_Unsigned working = 0;
			unsigned char const *start = *cur;
			do 
			{
				assert(*cur < limit);
				
				int n7bits = *cur - start;
				// add in the low-order 7 bits
				working |= ((**cur) & ~0x80) << (7 * n7bits);
				
			} while (*(*cur)++ & 0x80);
			
			return working;
		}
		Dwarf_Signed read_sleb128(unsigned char const **cur, unsigned char const *limit)
		{
			Dwarf_Signed working = 0;
			unsigned char const *start = *cur;
			unsigned char byte_read = 0;
			do 
			{
				assert(*cur < limit);
				
				int n7bits = *cur - start;
				// add in the low-order 7 bits
				byte_read = **cur;
				working |= (byte_read & ~0x80) << (7 * n7bits);
				
			} while (*(*cur)++ & 0x80);
			
			// sign-extend the result
			unsigned nbits_read = 7 * (*cur - start);
			if (nbits_read < 8 * sizeof (Dwarf_Signed) 
				&& byte_read >= 0x80)
			{
				working |= -(1 << nbits_read);
			}
			
			return working;
		}
		uint64_t read_8byte_le(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 8;
			assert(*cur <= limit);
			return (Dwarf_Unsigned) *pos
				| (Dwarf_Unsigned) *(pos + 1) << 8
				| (Dwarf_Unsigned) *(pos + 2) << 16
				| (Dwarf_Unsigned) *(pos + 3) << 24
				| (Dwarf_Unsigned) *(pos + 4) << 32
				| (Dwarf_Unsigned) *(pos + 5) << 40
				| (Dwarf_Unsigned) *(pos + 6) << 48
				| (Dwarf_Unsigned) *(pos + 7) << 56;
		}
		uint32_t read_4byte_le(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 4;
			assert(*cur <= limit);
			return (uint32_t) *pos
				| (uint32_t) *(pos + 1) << 8
				| (uint32_t) *(pos + 2) << 16
				| (uint32_t) *(pos + 3) << 24;
		}
		uint16_t read_2byte_le(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 2;
			assert(*cur <= limit);
			return (uint32_t) *pos
				| (uint32_t)  *(pos + 1) << 8;
		}		
		uint64_t read_8byte_be(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 8;
			assert(*cur <= limit);
			return  (Dwarf_Unsigned) *pos       << 56
				  | (Dwarf_Unsigned) *(pos + 1) << 48
				  | (Dwarf_Unsigned) *(pos + 2) << 40
				  | (Dwarf_Unsigned) *(pos + 3) << 32
				  | (Dwarf_Unsigned) *(pos + 4) << 24
				  | (Dwarf_Unsigned) *(pos + 5) << 16
				  | (Dwarf_Unsigned) *(pos + 6) << 8
				  | (Dwarf_Unsigned) *(pos + 7);
		}
		uint32_t read_4byte_be(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 4;
			assert(*cur <= limit);
			return  (Dwarf_Unsigned) *pos       << 24
				  | (Dwarf_Unsigned) *(pos + 1) << 16
				  | (Dwarf_Unsigned) *(pos + 2) << 8
				  | (Dwarf_Unsigned) *(pos + 3);
		}
		uint16_t read_2byte_be(unsigned char const **cur, unsigned char const *limit)
		{
			const unsigned char *pos = *cur;
			*cur += 2;
			assert(*cur <= limit);
			return (Dwarf_Unsigned) *pos       << 8
				 | (Dwarf_Unsigned) *(pos + 1);
		}
		
		Dwarf_Addr read_addr(int addrlen, unsigned char const **cur, unsigned char const *limit, bool use_host_byte_order)
		{
			assert(addrlen == 4 || addrlen == 8);
			bool read_be = srk31::host_is_little_endian() ^ use_host_byte_order;
			return static_cast<Dwarf_Addr>(
			       (read_be  && addrlen == 4) ? read_4byte_be(cur, limit)
			     : (read_be  && addrlen == 8) ? read_8byte_be(cur, limit)
			     : (!read_be && addrlen == 4) ? read_4byte_le(cur, limit)
			     : (!read_be && addrlen == 8) ? read_8byte_le(cur, limit)
			     : (assert(false), 0)
			);
		}
		

		frame_instrlist::frame_instrlist(const core::Cie& cie, int addrlen, const pair<unsigned char*, unsigned char*>& seq, bool use_host_byte_order /* = true */)
		{
			// unsigned char *instrs_start = seq.first;
			const unsigned char *pos = seq.first;
			const unsigned char *const limit = seq.second;

			while (pos < limit)
			{
				Dwarf_Frame_Op3 decoded = { 0, 0, 0, 0, 0, 0 };
				/* See DWARF4 page 181 for the summary of opcode encoding and arguments. 
				 * This macro masks out any argument part of the basic opcodes. */
#define opcode_from_byte(b) (((b) & 0xc0) ? (b) & 0xc0 : (b))
				
				unsigned char opcode_byte = *pos++;
				
				decoded.fp_base_op = opcode_byte >> 6;
				decoded.fp_extended_op = (decoded.fp_base_op == 0) ? opcode_byte & ~0xc0 : 0;

				switch (opcode_from_byte(opcode_byte))
				{
					// "packed" two-bit opcodes
					case DW_CFA_advance_loc: 
						decoded.fp_offset_or_block_len = opcode_byte & ~0xc0;
						break;
					case DW_CFA_offset:
						decoded.fp_register = opcode_byte & ~0xc0;
						// NOTE: here we are writing a signed value into an unsigned location
						decoded.fp_offset_or_block_len = cie.get_data_alignment_factor() * read_uleb128(&pos, seq.second);
						// ... so assert something that says we can read it back
						if (cie.get_data_alignment_factor() < 0)
						{
							assert((Dwarf_Signed) decoded.fp_offset_or_block_len < 0);
						}
						break;
					case DW_CFA_restore:
						decoded.fp_register = opcode_byte & ~0xc0;
						break;
					// DW_CFA_extended and DW_CFA_nop are the same value, BUT
					case DW_CFA_nop: goto no_args;      // this is a full zero byte
					// extended opcodes follow
					case DW_CFA_remember_state: goto no_args;
					case DW_CFA_restore_state: goto no_args;
					no_args:
						break;
					case DW_CFA_set_loc:
						decoded.fp_offset_or_block_len = read_addr(addrlen, &pos, seq.second, use_host_byte_order);
						break;
					case DW_CFA_advance_loc1:
						decoded.fp_offset_or_block_len = *pos++;
						break;
					case DW_CFA_advance_loc2:
						decoded.fp_offset_or_block_len = (host_is_big_endian() ^ use_host_byte_order) 
							? read_2byte_le(&pos, limit)
							: read_2byte_be(&pos, limit);
						break;
					case DW_CFA_advance_loc4:
						decoded.fp_offset_or_block_len = (host_is_big_endian() ^ use_host_byte_order) 
							? read_4byte_le(&pos, limit)
							: read_4byte_be(&pos, limit);
					
					// case DW_CFA_offset: // already dealt with, above
					
					case DW_CFA_restore_extended: goto uleb128_register_only;
					case DW_CFA_undefined: goto uleb128_register_only;
					case DW_CFA_same_value: goto uleb128_register_only;
					case DW_CFA_def_cfa_register: goto uleb128_register_only;
					uleb128_register_only:
						decoded.fp_register = read_uleb128(&pos, limit);
						break;
						
					case DW_CFA_offset_extended: goto uleb128_register_and_factored_offset;
					case DW_CFA_register: goto uleb128_register_and_factored_offset;
					uleb128_register_and_factored_offset:// FIXME: second register goes where? I've put it in fp_offset_or_block_len
						decoded.fp_register = read_uleb128(&pos, limit);
						decoded.fp_offset_or_block_len = cie.get_data_alignment_factor() * read_uleb128(&pos, limit);
						break;
					
					case DW_CFA_def_cfa: goto uleb128_register_and_offset;
					uleb128_register_and_offset:// FIXME: second register goes where? I've put it in fp_offset_or_block_len
						decoded.fp_register = read_uleb128(&pos, limit);
						decoded.fp_offset_or_block_len = read_uleb128(&pos, limit);
						break;

					case DW_CFA_offset_extended_sf: goto uleb128_register_sleb128_offset;
					case DW_CFA_def_cfa_sf: goto uleb128_register_sleb128_offset;
					uleb128_register_sleb128_offset:
						decoded.fp_register = read_uleb128(&pos, limit);
						decoded.fp_offset_or_block_len = cie.get_data_alignment_factor() * read_sleb128(&pos, limit);
						break;
					
					case DW_CFA_def_cfa_offset: goto uleb128_offset_only;
					uleb128_offset_only:
						decoded.fp_offset_or_block_len = read_uleb128(&pos, limit);
						break;
					
					case DW_CFA_def_cfa_offset_sf: goto sleb128_offset_only;
					sleb128_offset_only:
						decoded.fp_offset_or_block_len = cie.get_data_alignment_factor() * read_sleb128(&pos, limit);
						break;
						
					case DW_CFA_expression:
						decoded.fp_register = read_uleb128(&pos, limit);
						decoded.fp_offset_or_block_len = read_uleb128(&pos, limit);
						decoded.fp_expr_block = const_cast<Dwarf_Small*>(pos);
						pos += decoded.fp_offset_or_block_len;
						break;
					
					case DW_CFA_def_cfa_expression:
						decoded.fp_offset_or_block_len = read_uleb128(&pos, limit);
						decoded.fp_expr_block = const_cast<Dwarf_Small*>(pos);
						pos += decoded.fp_offset_or_block_len;
						break;
					
					case DW_CFA_val_offset: 
						decoded.fp_register = read_uleb128(&pos, limit);
						decoded.fp_offset_or_block_len = cie.get_data_alignment_factor() * read_sleb128(&pos, limit);
						break;
						
					case DW_CFA_val_offset_sf:
						decoded.fp_register = read_uleb128(&pos, limit);
						decoded.fp_offset_or_block_len = cie.get_data_alignment_factor() * read_uleb128(&pos, limit);
						break;
						
					case DW_CFA_val_expression:
						decoded.fp_register = read_uleb128(&pos, limit);
						decoded.fp_offset_or_block_len = read_uleb128(&pos, limit);
						decoded.fp_expr_block = const_cast<Dwarf_Small*>(pos);
						pos += decoded.fp_offset_or_block_len;
						break;
						
					default:
						assert(false);
				} // end switch

				// push the current row
				push_back(frame_instr(cie.get_owner().get_dbg().raw_handle(), decoded));
				
#undef opcode_from_byte
			} // end while
		}
	}
	namespace core
	{
		std::vector<Dwarf_Small>::const_iterator Cie::find_augmentation_element(char marker) const
		{
			/* FIXME: turn this into a map-style find function, 
			 * instead of just searching through the keys. 
			 * We want to generate the key-value mappings as 
			 * we go along, and return an iterator to a pair. */
		
			auto augbytes = get_augmentation_bytes();
			assert(sizeof(Dwarf_Small) == 1);
			auto i_byte = augbytes.begin();
			string augstring = get_augmenter();
			for (auto i_char = augstring.begin(); i_char != augstring.end(); ++i_char)
			{
				// we only understand augstrings beginning 'z'
				if (i_char == augstring.begin())
				{
					assert(*i_char == 'z');
					continue;
				}
				
				if (marker == *i_char)
				{
					return i_byte;
				}
				
				// else advance over this element
				switch (*i_char)
				{
					case 'L': // LSDA encoding
						++i_byte;
						break;
					case 'R': // FDE encoding
						++i_byte;
						break;
					case 'S': // signal frame flag; no byte
						break;
					case 'P': { // personality encoding
						int personality_encoding = *i_byte++;
						// skip over the personality pointer too
						i_byte += Cie::encoding_nbytes(personality_encoding, 
							reinterpret_cast<unsigned char const *>(&*i_byte), 
							reinterpret_cast<unsigned char const *>(&*augbytes.end()));
					} break;
					default:
						assert(false);
				}
			}
			
			return augbytes.end();
		}
		
		int Cie::get_fde_encoding() const
		{
			auto found = find_augmentation_element('R');
			assert(found != get_augmentation_bytes().end());
			
			return *found;
		}
		
		unsigned char Cie::get_address_size() const
		{
			if (address_size_in_dwarf > 0) return address_size_in_dwarf;
			else 
			{
				assert(version == 1);
				// we have to guess it's an ELF file
				GElf_Ehdr ehdr;
				Elf_opaque_in_libdwarf *e;
				int elf_ret = dwarf_get_elf(owner.dbg.raw_handle(), &e, &core::current_dwarf_error);
				assert(elf_ret == DW_DLV_OK);
				GElf_Ehdr *ret = gelf_getehdr(reinterpret_cast< ::Elf *>(e), &ehdr);
				assert(ret != 0);
				return (ehdr.e_machine == EM_X86_64) ? 8 : (ehdr.e_machine == EM_386) ? 4 : (assert(false), 4);
			}
		}
		unsigned char Cie::get_segment_size() const
		{
			return get_address_size(); // FIXME
		}
		
		void Cie::init_augmentation_bytes()
		{
			/* This is the data between the return address register and the 
			 * instruction bytes.
			 * To find this, we ask libdwarf for both: 
			 * - the augmentation string, and 
			 * - the instruction bytes;
			 * Then we skip forward a little from the former. */
			
			const unsigned char *pos = reinterpret_cast<unsigned char *>(get_augmenter());
			const unsigned char * instrs_start = (const unsigned char *) get_initial_instructions();
			
			// skip the null-terminated augmenter
			while (*pos++);
			
			if (version == 4)
			{
				this->address_size_in_dwarf = *pos++;
				assert(this->address_size_in_dwarf == 4 || this->address_size_in_dwarf == 8);
				this->segment_size_in_dwarf = *pos++;
			} else 
			{
				this->address_size_in_dwarf = 0;
				this->segment_size_in_dwarf = 0;
			}

			// skip the code alignment factor (uleb128)
			encap::read_uleb128(&pos, instrs_start);
			
			// skip the data alignment factor (sleb128)
			encap::read_sleb128(&pos, instrs_start);
			
			// skip the return address register
			// "In CIE version 1 this is a single byte; in CIE version 3 this is an unsigned LEB128."
			(get_version() == 1) ? *++pos : (get_version() == 3) ? encap::read_uleb128(&pos, instrs_start)
			 : (assert(false), 0);
			
			// now we're pointing at the beginning of the augmentation data
			// -- it should be empty if we don't start with a z
			// -- it should be a uleb128 length if we do start with a z
			string aug_string(get_augmenter());
			Dwarf_Unsigned expected_data_length;
			if (aug_string.length() > 0 && aug_string.at(0) == 'z')
			{
				// read a uleb128
				expected_data_length = encap::read_uleb128(&pos, instrs_start);
			}
			else
			{
				expected_data_length = 0;
			}
			
			while (pos < instrs_start) augbytes.push_back(*pos++);
			
			assert(augbytes.size() == expected_data_length);
		}
		
		unsigned Cie::encoding_nbytes(unsigned char encoding, unsigned char const *bytes, unsigned char const *limit) const
		{
			if (encoding == 0xff) return 0; // skip
			
			switch (encoding & ~0xf0) // low-order bytes only
			{
				case DW_EH_PE_absptr:
					return get_address_size();
				case DW_EH_PE_omit: assert(false); // handled above
				case DW_EH_PE_uleb128: {
					unsigned char const *pos = bytes;
					encap::read_uleb128(&pos, limit);
					return pos - bytes;
				}
				case DW_EH_PE_udata2: return 2;
				case DW_EH_PE_udata4: return 4;
				case DW_EH_PE_udata8: return 8;
				case /* DW_EH_PE_signed */ 0x8: 
					return get_address_size();
				case DW_EH_PE_sleb128: {
					unsigned char const *pos = bytes;
					encap::read_sleb128(&pos, limit);
					return pos - bytes;
				}
				case DW_EH_PE_sdata2: return 2;
				case DW_EH_PE_sdata4: return 4;
				case DW_EH_PE_sdata8: return 8;
				
				default: goto unsupported_for_now;
				
				unsupported_for_now:
					assert(false);
			}
		}

		void Fde::init_augmentation_bytes()
		{
			/* 
			 * An FDE starts with the length and ID described above, and then continues as follows.
			
			 * The starting address to which this FDE applies. 
			 * This is encoded using the FDE encoding specified by the associated CIE.
			 * The number of bytes after the start address to which this FDE applies. 
			 * This is encoded using the FDE encoding.
			 * If the CIE augmentation string starts with 'z', 
			 * the FDE next has an unsigned LEB128 which is the total size of the FDE augmentation data. 
			 * This may be used to skip data associated with unrecognized augmentation characters. 
			 */
			 
			const unsigned char *instrs_start = (const unsigned char *) instr_bytes_seq().first;
			
			const Cie& cie = *find_cie();
			
			// we need a pointer *before* the instructions.  Q. where?  A. the FDE base addr 
			const unsigned char *pos = reinterpret_cast<const unsigned char *>(fde_bytes);
			assert(instrs_start > fde_bytes);
			
			// skip the length and id
			pos += owner.is_64bit ? 12 : 4;
			pos += 4; // id is always 32 bits
			
			// skip the FDE starting address
			unsigned encoding_nbytes = cie.encoding_nbytes(cie.get_fde_encoding(), pos, instrs_start);
			pos += encoding_nbytes;
	
			// look for the augmentation length-in-bytes
			string aug_string(cie.get_augmenter());
			Dwarf_Unsigned expected_data_length;
			if (aug_string.length() > 0 && aug_string.at(0) == 'z')
			{
				// read a uleb128
				expected_data_length = encap::read_uleb128(&pos, instrs_start);
			}
			else
			{
				expected_data_length = 0;
			}
			
			while (pos < instrs_start) augbytes.push_back(*pos++);
			assert(augbytes.size() == expected_data_length);

/*
			unsigned encoding2 = *pos++;
			unsigned encoding2_nbytes = cie.encoding_nbytes(encoding2, pos, instrs_start);
			pos += encoding2_nbytes;
			
			// skip the LSDA encoding
			// "If the CIE does not specify DW_EH_PE_omit as the LSDA encoding, 
			// the FDE next has a pointer to the LSDA, encoded as specified by the CIE. 
			auto found_lsda = cie.find_augmentation_element('L');
			if (found_lsda == cie.get_augmentation_bytes().end())
			{
				// assume the default, which is DW_EH_absptr
				pos += cie.encoding_nbytes(DW_EH_PE_absptr, nullptr, nullptr);
			} else {
				// do what it says
				pos += cie.encoding_nbytes(*found_lsda, pos, instrs_start);
			}
			*/
			
		}
		
		FrameSection::instrs_results
		Fde::decode() const
		{
			boost::icl::interval_map<Dwarf_Addr, set< pair<int /* regnum */, FrameSection::register_def > > > working; 
			unsigned char *instr_bytes_begin = instr_bytes_seq().first;
			unsigned char *instr_bytes_end = instr_bytes_seq().second;
			
			/* Get the CIE for this FDE. */
			const core::Cie& cie = *find_cie();

			typedef optional< const FrameSection::instrs_results & > initial_instrs_results_t;
			/* Invoke the interpreter. */
			
			/* Walk the CIE initial instructions. */
			auto initial_result = owner.interpret_instructions(cie, get_low_pc(),
				cie.get_initial_instructions(), cie.get_initial_instructions_length(), initial_instrs_results_t());
			/* Walk the FDE instructions. */
			auto final_result = owner.interpret_instructions(cie, initial_result.unfinished_row_addr,
				instr_bytes_begin, instr_bytes_end - instr_bytes_begin, initial_result);
			/* Add any unfinished row, using the FDE high pc */
			if (final_result.rows.size() > 0)
			{
				set< pair< int, FrameSection::register_def > > current_row_defs_set(
					final_result.unfinished_row.begin(), final_result.unfinished_row.end());
				assert(get_low_pc() + get_func_length() > final_result.unfinished_row_addr);
				working += make_pair( 
					interval<Dwarf_Addr>::right_open(final_result.unfinished_row_addr, get_low_pc() + get_func_length()),
					current_row_defs_set
				);
			}
			
			// that's it! (no unfinished rows now)
			return (FrameSection::instrs_results) { working, std::map<int, FrameSection::register_def>(), 0 };
		}
		
		FrameSection::instrs_results
		FrameSection::interpret_instructions(const Cie& cie, 
				Dwarf_Addr initial_row_addr, 
				Dwarf_Ptr instrs, Dwarf_Unsigned instrs_len,
				optional< const FrameSection::instrs_results & > initial_instrs_results) const
		{
			/* Expand the instructions. We would use dwarf_expand_frame_instructions but 
			 * it seems to be DWARF2-specific, and I don't want to use too many more 
			 * libdwarf calls. So use our own frame_instrlist. */
			encap::frame_instrlist instrlist(cie, cie.get_address_size(), 
				make_pair(reinterpret_cast<unsigned char *>(instrs), 
					      reinterpret_cast<unsigned char *>(instrs) + instrs_len),
					      /* use_host_byte_order -- FIXME */ true);
			
			// create container for return values & working storage
			instrs_results result;
			auto& working = result.rows;
			auto& current_row_defs = result.unfinished_row;
			Dwarf_Addr& current_row_addr = result.unfinished_row_addr;
			
			Dwarf_Addr new_row_addr = -1;
			if (initial_instrs_results)
			{
				current_row_defs = initial_instrs_results->unfinished_row;
				current_row_addr = initial_instrs_results->unfinished_row_addr;
			}
			std::stack< map<int, register_def> > remembered_row_defs;

			cerr << "Interpreting instrlist " << instrlist << endl;
			for (auto i_op = instrlist.begin(); i_op != instrlist.end(); ++i_op)
			{
				cerr << "\tInterpreting instruction " << *i_op << endl;
				switch (i_op->fp_base_op << 6 | i_op->fp_extended_op)
				{
					// row creation
					case DW_CFA_set_loc:
						new_row_addr = i_op->fp_offset_or_block_len;
						goto add_new_row;
					case DW_CFA_advance_loc:
					case DW_CFA_advance_loc1:
					case DW_CFA_advance_loc2:
					case DW_CFA_advance_loc4:
						new_row_addr = current_row_addr + i_op->fp_offset_or_block_len;
						goto add_new_row;
					add_new_row: {
						// assert greater than current
						assert(new_row_addr > current_row_addr);
						set< pair< int, register_def > > current_row_defs_set(current_row_defs.begin(), current_row_defs.end());

						// add the old row to the interval map
						working += make_pair( 
							interval<Dwarf_Addr>::right_open(current_row_addr, new_row_addr),
							current_row_defs_set
						);
						} break;
					// CFA definition
					case DW_CFA_def_cfa:
						current_row_defs[DW_FRAME_CFA_COL3].register_plus_offset_w() = make_pair(i_op->fp_register, i_op->fp_offset_or_block_len);
						break;
					case DW_CFA_def_cfa_sf: // signed, factored
						current_row_defs[DW_FRAME_CFA_COL3].register_plus_offset_w() = make_pair(i_op->fp_register, i_op->fp_offset_or_block_len);
						break;
					case DW_CFA_def_cfa_register:
						assert(current_row_defs.find(DW_FRAME_CFA_COL3) != current_row_defs.end());
						// FIXME: also assert that it's a reg+off def, not a locexpr def
						current_row_defs[DW_FRAME_CFA_COL3].register_plus_offset_w().first = i_op->fp_register;
						break;
					case DW_CFA_def_cfa_offset:
						assert(current_row_defs.find(DW_FRAME_CFA_COL3) != current_row_defs.end());
						// FIXME: also assert that it's a reg+off def, not a locexpr def
						current_row_defs[DW_FRAME_CFA_COL3].register_plus_offset_w().second = i_op->fp_offset_or_block_len;
						break;
					case DW_CFA_def_cfa_offset_sf:
						assert(current_row_defs.find(DW_FRAME_CFA_COL3) != current_row_defs.end());
						// FIXME: also assert that it's a reg+off def, not a locexpr def
						current_row_defs[DW_FRAME_CFA_COL3].register_plus_offset_w().second = i_op->fp_offset_or_block_len;
						break;
					case DW_CFA_def_cfa_expression: 
						current_row_defs[DW_FRAME_CFA_COL3].saved_at_expr_w() = encap::loc_expr(get_dbg().raw_handle(), i_op->fp_expr_block, i_op->fp_offset_or_block_len);
						break;
					// register rule
					case DW_CFA_undefined:
						// mark the specified register as undefined
						current_row_defs[i_op->fp_register].undefined_w();
						break;
					case DW_CFA_same_value:
						current_row_defs[i_op->fp_register].same_value_w();
						break;
					case DW_CFA_offset:
					case DW_CFA_offset_extended:
					case DW_CFA_offset_extended_sf:
						current_row_defs[i_op->fp_register].saved_at_offset_from_cfa_w() = i_op->fp_offset_or_block_len;;
						current_row_defs[i_op->fp_register].saved_at_offset_from_cfa_w() = i_op->fp_offset_or_block_len;;
						break;
					case DW_CFA_val_offset: 
					case DW_CFA_val_offset_sf:
						current_row_defs[i_op->fp_register].val_is_offset_from_cfa_w() = i_op->fp_offset_or_block_len;
						break;
					case DW_CFA_register: // FIXME: second register goes where? I've put it in fp_offset_or_block_len
						current_row_defs[i_op->fp_register].register_plus_offset_w() = make_pair(i_op->fp_offset_or_block_len, 0);
						break;
					case DW_CFA_expression:
						current_row_defs[i_op->fp_register].saved_at_expr_w() = encap::loc_expr(get_dbg().raw_handle(), i_op->fp_expr_block, i_op->fp_offset_or_block_len);
						break;
					case DW_CFA_val_expression:
						current_row_defs[i_op->fp_register].val_of_expr_w() = encap::loc_expr(get_dbg().raw_handle(), i_op->fp_expr_block, i_op->fp_offset_or_block_len);
						break;
					case DW_CFA_restore:
					case DW_CFA_restore_extended: {
						// look in the unfinished row
						auto &initial_results = *initial_instrs_results;
						const register_def *opt_previous_def = nullptr;
						auto found_in_unfinished = initial_results.unfinished_row.find(i_op->fp_register);
						if (found_in_unfinished != initial_results.unfinished_row.end())
						{
							opt_previous_def = &found_in_unfinished->second;
						}
						else 
						{
							auto found_row = initial_results.rows.find(current_row_addr);
							if (found_row != initial_results.rows.end())
							{
								// look for the register
								auto& inner_set = initial_results.rows.find(current_row_addr)->second;
								// HACK: we build a map, rather than searching the set
								map<int, register_def> inner_map(inner_set.begin(), inner_set.end());
								auto found = inner_map.find(i_op->fp_register);
								if (found != inner_map.end())
								{
									opt_previous_def = &found->second;
								}
							}
						}
						if (opt_previous_def)
						{
							current_row_defs[i_op->fp_register] = *opt_previous_def;
						}
						else
						{
							/* What does it mean if we're not defined in the initial instructions? 
							 * Let's suppose it means undefined. */
							current_row_defs[i_op->fp_register] = (register_def) { .k = register_def::UNDEFINED };
						}

						} break;
					// row state
					case DW_CFA_restore_state:
						assert(remembered_row_defs.size() > 0);
						current_row_defs = remembered_row_defs.top(); remembered_row_defs.pop();
						break;
					case DW_CFA_remember_state:
						remembered_row_defs.push(current_row_defs);
						break;
					// padding 					
					case DW_CFA_nop:      // this is a full zero byte
						break;
						cerr << "FIXME!" << endl;
					default: goto unsupported_for_now;
					unsupported_for_now:
						assert(false);
				} // end switch
			} // end for i_op

			// there might be an unfinished row in result.unfinished_row; if so we'll fix it up outside
			return result;
		}
		
	}
	namespace encap
	{
		using core::FrameSection;
		using core::Cie;
		using core::Fde;
		
		loclist rewrite_loclist_in_terms_of_cfa(
			const loclist& l, 
			const FrameSection& fs, 
			const boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned>& containing_intervals,
			dwarf::spec::opt<const loclist&> opt_fbreg // fbreg is special -- loc exprs can refer to it
			)
		{
			/* First
			
			 * - compute a map from vaddrs to CFA expressions of the form (reg + offset). 
			 * - for some vaddrs, CFA might not expressible this way 
			 
			 * Then for each vaddr range in the locexpr
			 
			 * - note any breg(n) opcodes
			 * - see if we can compute them from CFA instead
			 * - if so, rewrite them as a { cfa, push, plus } operation
			 
			 * HMM. In general we seem to be building a constraint graph
			 * s.t. two nodes (n1, n2) are connected by an edge labelled k
			 * if n2 == n1 + k.
			 *
			 * NOTE that every edge has an opposite-direction edge whose weight
			 * is the negation of the first weight.
			 
			 * NOTE that in general, this graph changes with each instruction. 
			 * So what we are labelling the intervals with is really the edge set.
			 
			 * Can we relate all registers this way, including cfa as a pseudo-reg, 
			 * then look for a path from cfa to the referenced register?
			 
			 * YES, this is a nice formulation.
			 
			 * We also add the loc expr of interest itself, as another node, from which
			 * we will try to find paths to the CFA.
			 
			 * We still have to collapse identical vaddr ranges at the end, because edge
			 * sets refer to *all* regs and we only care about one (the loc expr).
			 
			 * What about fbreg? It is just another node (with definition providing the edges)
			 */
			
			struct edge
			{
				int from_reg;
				int to_reg;
				int difference;
				
				edge(const pair</*const*/ int /* regnum */, FrameSection::register_def >& map_entry)
				{
					assert(map_entry.second.k == FrameSection::register_def::REGISTER);
					from_reg = map_entry.second.register_plus_offset_r().first;
					to_reg = map_entry.first;
					difference = map_entry.second.register_plus_offset_r().second;
				}
				
				bool operator<(const edge& e) const
				{
					return make_pair(from_reg, make_pair(to_reg, difference)) < make_pair(e.from_reg, make_pair(e.to_reg, e.difference));
				}
				
				bool operator!=(const edge& e) const
				{
					return e < *this || *this < e;
				}
				bool operator==(const edge& e) const { return !(*this != e); }
			};
			
			boost::icl::interval_map<Dwarf_Addr, set< edge > > edges;
			
			/* Walk our FDEs starting from the lowest addr in the interval. */
			
			optional<Fde> current;
			Dwarf_Addr hipc = 0;
			for (auto i_int = containing_intervals.begin(); i_int != containing_intervals.end(); ++i_int)
			{
				assert((hipc == 0  && !current) || hipc > i_int->first.lower());
				Dwarf_Addr lopc;
				
				// walk all FDEs that overlap this interval
				
				if (!current)
				{
					// we don't have a FDE that overlaps this interval
					auto found = fs.find_fde_for_pc(i_int->first.lower());
					assert(found != fs.fde_end());
					*current = *found;
				}
				
				// while there is some overlap with our interval
				while (lopc < i_int->first.upper() && hipc > i_int->first.lower())
				{
					// decode the table into rows
					auto results = current->decode();
					boost::icl::interval_map<Dwarf_Addr, set<pair<int /* regnum */, FrameSection::register_def > > >& rows = results.rows;
					
					// process each row
					for (auto i_row = rows.begin(); i_row != rows.end(); ++i_row)
					{
						// add an entry to our interval map
						edges += make_pair(
								interval<Dwarf_Addr>::right_open(
									/* intersection of this interval and the *row*'s (not FDE's) interval */
									std::max(lopc, i_int->first.lower()), 
									std::min(hipc, i_int->first.upper())
								), 
								/* set of edge definitions in this row */
								std::set<edge>(i_row->second.begin(), i_row->second.end()) // i.e. pair<int, pair<int, int> > i.e. (src, dst, weight)
							);
					}
					
					// get the next FDE
					auto found = fs.find_fde_for_pc(hipc);
					assert(found != fs.fde_end());
					*current = *found;
					lopc = current->get_low_pc();
					hipc = current->get_low_pc() + current->get_func_length();
				}
				
				// leave 'current' since it might be useful on the next iteration
				
			} // end for interval

			// FIXME: now do the rewrites and coalesce

			return l; // FIXME
		}
	}
}
