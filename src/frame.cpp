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
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/property_map/property_map.hpp>
#include <srk31/endian.hpp>
#include <srk31/algorithm.hpp>
#include <gelf.h>

#include "lib.hpp"
#include "frame.hpp"
#include "regs.hpp"

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

static bool debug;
static void init() __attribute__((constructor));
static void init()
{
	debug = (getenv("DWARFPP_DEBUG_FRAME") && 0 != atoi(getenv("DWARFPP_DEBUG_FRAME")));
}

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
							assert((Dwarf_Signed) decoded.fp_offset_or_block_len <= 0);
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
					
					/* HACK: somewhere better to put the vendor-specific stuff? */
					case DW_CFA_GNU_args_size:
						/* from LSB 3.1.1: 
						 * "The DW_CFA_GNU_args_size instruction takes an unsigned LEB128 operand 
						 * representing an argument size. This instruction specifies the total 
						 * of the size of the arguments which have been pushed onto the stack.
						 */
						decoded.fp_offset_or_block_len = read_uleb128(&pos, limit);
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
		using dwarf::encap::read_8byte_le;
		using dwarf::encap::read_4byte_le;
		using dwarf::encap::read_2byte_le;
		using dwarf::encap::read_8byte_be;
		using dwarf::encap::read_4byte_be;
		using dwarf::encap::read_2byte_be;
		using dwarf::encap::read_2byte_be;
	
		::Elf *FrameSection::get_elf() const
		{
			lib::Elf_opaque_in_libdwarf *e;
			int elf_ret = dwarf_get_elf(dbg.raw_handle(), &e, &core::current_dwarf_error);
			assert(elf_ret == DW_DLV_OK);
			return reinterpret_cast< ::Elf *>(e);
		}
	
		int FrameSection::get_elf_machine() const
		{
			GElf_Ehdr ehdr;
			GElf_Ehdr *ret = gelf_getehdr(get_elf(), &ehdr);
			assert(ret != 0);
			return ehdr.e_machine;
		}
	
		std::vector<Dwarf_Small>::const_iterator Cie::find_augmentation_element(char marker) const
		{
			/* FIXME: turn this into a map-style find function, 
			 * instead of just searching through the keys. 
			 * We want to generate the key-value mappings as 
			 * we go along, and return an iterator to a pair. */
		
			auto& augbytes = get_augmentation_bytes();
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
				auto e_machine = owner.get_elf_machine();
				return (e_machine == EM_X86_64) ? 8 : (e_machine == EM_386) ? 4 : (assert(false), 4);
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
			
			unsigned char const *tmp_bytes = bytes;
			/*Dwarf_Unsigned ret = */ read_with_encoding(encoding, &tmp_bytes, limit, true); // FIXME: use target byte-order, not host
			return tmp_bytes - bytes;
		}
		
		Dwarf_Unsigned 
		Cie::read_with_encoding(unsigned char encoding, unsigned char const **pos, unsigned char const *limit, bool use_host_byte_order) const
		{
			bool read_be = host_is_little_endian() ^ use_host_byte_order;
			
			if (encoding == 0xff) return 0; // skip
			
			switch (encoding & ~0xf0) // low-order bytes only
			{
				case DW_EH_PE_absptr:
					if (get_address_size() == 8) goto udata8;
					else { assert(get_address_size() == 4); goto udata4; }
				case DW_EH_PE_omit: assert(false); // handled above
				case DW_EH_PE_uleb128: return encap::read_uleb128(pos, limit);
				/* udata2 */ case DW_EH_PE_udata2: return (read_be ? read_2byte_be : read_2byte_le)(pos, limit);
				udata4:      case DW_EH_PE_udata4: return (read_be ? read_4byte_be : read_4byte_le)(pos, limit);
				udata8:      case DW_EH_PE_udata8: return (read_be ? read_8byte_be : read_8byte_le)(pos, limit);
				case /* DW_EH_PE_signed */ 0x8: 
					if (get_address_size() == 8) goto sdata8;
					else { assert(get_address_size() == 4); goto sdata4; }
				case DW_EH_PE_sleb128: return encap::read_sleb128(pos, limit);
				/* sdata2 */ case DW_EH_PE_sdata2: return (read_be ? read_2byte_be : read_2byte_le)(pos, limit);
				sdata4:      case DW_EH_PE_sdata4: return (read_be ? read_4byte_be : read_4byte_le)(pos, limit);
				sdata8:      case DW_EH_PE_sdata8: return (read_be ? read_8byte_be : read_8byte_le)(pos, limit);
				
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
			
			// skip the FDE starting address // FIXME: is it safe to call get_fde_encoding() here?
			unsigned startaddr_encoded_nbytes = cie.encoding_nbytes(cie.get_fde_encoding(), pos, instrs_start);
			pos += startaddr_encoded_nbytes;
			
			// read the FDE length-in-bytes and sanity-check
			// const unsigned char *pos_saved = pos;
			Dwarf_Unsigned fde_length = cie.read_with_encoding(cie.get_fde_encoding(), &pos, instrs_start, true); // FIXME: use target byte-order, not host
			// unsigned fde_length_encoded_length = pos - pos_saved;
			assert(fde_length == get_func_length());
			
			// read the augmentation length, if present
			string aug_string(cie.get_augmenter());
			Dwarf_Unsigned augmentation_length;
			if (aug_string.length() > 0 && aug_string.at(0) == 'z')
			{
				// read a uleb128
				augmentation_length = encap::read_uleb128(&pos, instrs_start);
			}
			else
			{
				augmentation_length = 0;
			}
			
			while (pos < instrs_start) augbytes.push_back(*pos++);
			assert(augbytes.size() == augmentation_length);
		}
		
		Dwarf_Unsigned Fde::get_lsda_pointer() const
		{
			// NOTE: the LSDA pointer is included in the augmentation bytes

			unsigned char const *pos = &*get_augmentation_bytes().begin();
			unsigned char const *end = &*get_augmentation_bytes().end();
			const core::Cie& cie = *find_cie();
			
			/* "If the CIE does not specify DW_EH_PE_omit as the LSDA encoding, 
			 * the FDE next has a pointer to the LSDA, encoded as specified by the CIE. */
			// NOTE: LSDA pointer can apparently be omitted implicitly by no-more-bytes
			auto found_lsda = cie.find_augmentation_element('L');
			if (pos == end)
			{
				// assume implicitly omitted, so do nothing
				return 0;
			}
			else if (found_lsda == cie.get_augmentation_bytes().end())
			{
				// assume the default, which is DW_EH_absptr
				return cie.read_with_encoding(DW_EH_PE_absptr, nullptr, nullptr, true); // HACK
			} else {
				// do what it says
				return cie.read_with_encoding(*found_lsda, &pos, end, true); // HACK: use target encoding
			}
		}
		
		FrameSection::instrs_results
		Fde::decode() const
		{
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
				final_result.add_unfinished_row(get_low_pc() + get_func_length());
			}
			
			// that's it! (no unfinished rows now)
			return final_result;
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
			else
			{
				// others are initialized to empty
				current_row_addr = initial_row_addr;
			}
			std::stack< map<int, register_def> > remembered_row_defs;

			if (debug) cerr << "Interpreting instrlist " << instrlist << endl;
			for (auto i_op = instrlist.begin(); i_op != instrlist.end(); ++i_op)
			{
				if (debug) cerr << "\tInterpreting instruction " << *i_op << endl;
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
						current_row_addr = new_row_addr;
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
							 * Answer: "indeterminate" (not "undefined") */
							current_row_defs[i_op->fp_register] = (register_def) { .k = register_def::INDETERMINATE };
						}

					} break;
					// row state
					case DW_CFA_restore_state: {
						auto current_cfa = current_row_defs[DW_FRAME_CFA_COL3];
						
						assert(remembered_row_defs.size() > 0);
						current_row_defs = remembered_row_defs.top(); remembered_row_defs.pop();
						// don't clobber the CFA we had
						current_row_defs[DW_FRAME_CFA_COL3] = current_cfa;
					} break;
					case DW_CFA_remember_state:
						remembered_row_defs.push(current_row_defs);
						// do NOT remember CFA!
						remembered_row_defs.top().erase(DW_FRAME_CFA_COL3);
						break;
					// padding 
					case DW_CFA_nop: // this is a full zero byte
						break;
						cerr << "FIXME!" << endl;
					case DW_CFA_GNU_args_size:
						/* purely informational I think? */
						break;
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
		
		struct register_edge
		{
			int from_reg;
			int to_reg;
			int difference;

			bool operator<(const register_edge& e) const
			{
				return make_pair(from_reg, make_pair(to_reg, difference)) < make_pair(e.from_reg, make_pair(e.to_reg, e.difference));
			}

			bool operator!=(const register_edge& e) const
			{
				return e < *this || *this < e;
			}
			bool operator==(const register_edge& e) const { return !(*this != e); }
		};
		
		typedef int /* regnum */ register_node;
		struct register_graph : public std::multimap< int, register_edge >
		{
			set<key_type> key_set;
			
			typedef std::multimap< int, register_edge > super;

			iterator insert(const value_type& val)
			{
				key_set.insert(val.first);
				return super::insert(val);
			}
		};
	}
}
namespace boost
{
	template<>
	struct graph_traits<dwarf::encap::register_graph> 
	{
		// copied from encap_sibling_graph.hpp (inheritance doesn't work for some reason...)
		typedef dwarf::encap::register_node  vertex_descriptor;
		
		static vertex_descriptor null_vertex() 
		{ return /*make_pair(boost::icl::interval<dwarf::lib::Dwarf_Addr>::right_open(0, 1), -1);*/ -1; }
		
		typedef dwarf::encap::register_edge edge_descriptor;

		typedef std::set< dwarf::encap::register_node >::iterator vertex_iterator;
		
		typedef directed_tag directed_category;
		typedef disallow_parallel_edge_tag edge_parallel_category;

		struct traversal_tag :
		  public virtual vertex_list_graph_tag,
		  public virtual incidence_graph_tag { };
		typedef traversal_tag traversal_category;
		
		typedef unsigned vertices_size_type;
		typedef unsigned edges_size_type;
		typedef unsigned degree_size_type;

		static
		const dwarf::encap::register_edge& 
		transform_to_edge(const pair<int, dwarf::encap::register_edge>& val) 
		{
			return val.second;
		}
		
		typedef std::function<const dwarf::encap::register_edge& (const pair<int, dwarf::encap::register_edge>&)> function_base_t;
		struct transform_to_edge_t : public function_base_t
		{
			transform_to_edge_t() : function_base_t(transform_to_edge) {}
		};

		typedef boost::transform_iterator<
			transform_to_edge_t,
			std::multimap< int, dwarf::encap::register_edge >::const_iterator
		> out_edge_iterator;
	};
}
namespace dwarf
{
	namespace encap
	{
		using boost::graph_traits;
		using lib::Dwarf_Addr;
		
		pair<
			graph_traits<register_graph>::out_edge_iterator, 
			graph_traits<register_graph>::out_edge_iterator
		>
		out_edges(
			graph_traits<register_graph>::vertex_descriptor u, 
			const register_graph& g);
			
		pair<
			graph_traits<register_graph>::vertex_iterator,
			graph_traits<register_graph>::vertex_iterator >  
		vertices(const register_graph& g);
		
		graph_traits<register_graph>::degree_size_type
		out_degree(
			graph_traits<register_graph>::vertex_descriptor u,
			const register_graph& g);
		
		graph_traits<register_graph>::vertex_descriptor
		source(
			graph_traits<register_graph>::edge_descriptor e,
			const register_graph& g);
		
		graph_traits<register_graph>::vertex_descriptor
		target(
			graph_traits<register_graph>::edge_descriptor e,
			const register_graph& g);
		
		graph_traits<register_graph>::vertices_size_type 
		num_vertices(const register_graph& g);

		struct weight_map_t
		{
			typedef typename graph_traits<register_graph>::edge_descriptor key_type;
			typedef int value_type;
			typedef int& reference;
			typedef boost::readable_property_map_tag category;
		};
		inline int get(const weight_map_t& wm, const register_edge& e) { return 1; }
		
		loclist rewrite_loclist_in_terms_of_cfa(
			const loclist& l, 
			const FrameSection& fs, 
			dwarf::spec::opt<const loclist&> opt_fbreg // fbreg is special -- loc exprs can refer to it
			)
		{
			/* The important points here are that 
			 *
			 * - our loclist is a list of loc_exprs, each valid for some vaddr range
			 * - we want to rewrite as many loc_exprs as possible to avoid using DW_OP_bregn
			 * - ... and instead use a { cfa, push <const>, plus } sequence
			 * - we can do this because the FDEs tell us how to compute registers in the CFA. 
			 * 
			 * For each breg(n)-containing loc_expr, we
			 * - duplicate the loc_expr once for each overlapping FDE row, and once for each FDE-uncovered range
			 * - ... substituting CFA-based sequences for breg(n) ops, wherever such sequences exist
			 * - ... using the original breg(n) op where they don't.
			 *
			 * We identify CFA-based sequences using a graph search.
			 */
			
			map< boost::icl::discrete_interval<Dwarf_Addr>, loc_expr> loclist_intervals;
			
			loclist copied_l = l;
			for (auto i_loc_expr = copied_l.begin();	
				i_loc_expr != copied_l.end();
				++i_loc_expr)
			{
				/* Always build loclist_intervals, assigning this loc_expr to this interval. 
				 * We will reassign this mapping, as necessary, over any subintervals that 
				 * we choose to rewrite. */
				loclist_intervals[boost::icl::interval<Dwarf_Addr>::right_open(
						i_loc_expr->lopc, 
						i_loc_expr->hipc)] = *i_loc_expr;
				/* Does this expr contain any bregn opcodes? If not, we don't need
				 * to look for rewritings. */
				if (std::find_if(i_loc_expr->begin(), i_loc_expr->end(), [](const expr_instr& arg) -> bool {
					return arg.lr_atom >= DW_OP_breg0 && arg.lr_atom <= DW_OP_breg31;
				}) == i_loc_expr->end())
				{
					continue;
				}
				
				boost::icl::discrete_interval<Dwarf_Addr> loc_expr_int
				 = boost::icl::interval<Dwarf_Addr>::right_open(
				 	i_loc_expr->lopc,
					i_loc_expr->hipc
					);
				auto i_int = &loc_expr_int;

				// walk all FDEs overlapping our loc_expr
				// ASSUME fdes are in address-range order?!
				Dwarf_Addr prev_fde_lopc = 0;
				auto end_fde = fs.find_fde_for_pc(i_int->upper());
				bool end_fde_is_valid = end_fde != fs.fde_end();
				/* We start with the FDE for i_int->lower(), and continue blindly along the 
				 * FDEs. At some point we will reach an FDE which no longer overlaps i_int, 
				 * at which point we terminate the loop. This is to complex to encode in the
				 * for-loop's termination condition, so we put a break near the top of the body. */
				for (auto i_fde = fs.find_fde_for_pc(i_int->lower()); 
					i_fde != fs.fde_end() && (!end_fde_is_valid || i_fde <= end_fde); ++i_fde)
				{
					Dwarf_Addr fde_lopc = i_fde->get_low_pc();
					Dwarf_Addr fde_hipc = i_fde->get_low_pc() + i_fde->get_func_length();

					assert(i_fde->get_low_pc() >= prev_fde_lopc); // I have seen the == case

					FrameSection::instrs_results current_decoded = i_fde->decode();
					boost::icl::discrete_interval<Dwarf_Addr> current_fde_overlap_interval
					 = interval<Dwarf_Addr>::right_open(
										/* intersection of our loc_expr's interval and the FDE's interval */
										std::max(fde_lopc, i_int->lower()), 
										std::min(fde_hipc, i_int->upper()));
					// extra termination test
					if (current_fde_overlap_interval.upper() == current_fde_overlap_interval.lower()) break;

					// process each FDE row
					for (auto i_row = current_decoded.rows.begin(); i_row != current_decoded.rows.end(); ++i_row)
					{	
						// how much does it overlap the loc_expr interval?
						auto row_overlap_interval = interval<Dwarf_Addr>::right_open(
								/* intersection of our loc_expr's interval and the *row*'s (not FDE's) interval */
								std::max(i_row->first.lower(), i_int->lower()), 
								std::min(i_row->first.upper(), i_int->upper())
							);
						if (row_overlap_interval.upper() <= row_overlap_interval.lower())
						{
							// there's no overlap with this FDE and this row, so try another row
							continue;
						}

						// Compute the edge set -- 
						// we only want register_plus_offset register definitions
						register_graph g;
						for (auto i_ent = i_row->second.begin(); i_ent != i_row->second.end(); ++i_ent)
						{
							if (i_ent->second.k == FrameSection::register_def::REGISTER)
							{
								int source = i_ent->second.register_plus_offset_r().first;
								int target = i_ent->first;

								// we want source + difference = target
								// ... cf. the meaning of register_plus_offset is that 
								// target = source + difference, i.e. it's correct!

								// add *two* edges: forward and back
								g.insert(make_pair(source, (register_edge){
									.from_reg = source,
									.to_reg = target,
									.difference = i_ent->second.register_plus_offset_r().second,
								}));
								g.insert(make_pair(target, (register_edge){
									.from_reg = target,
									.to_reg = source,
									.difference = - i_ent->second.register_plus_offset_r().second,
								}));
							}
						}

						/* For each breg, see if we can find a path to the CFA. */
						bool success = true;
						auto copied_loc_expr = *i_loc_expr;
						auto i_op = copied_loc_expr.begin();
						while (i_op != copied_loc_expr.end())
						{
							if (!(i_op->lr_atom >= DW_OP_breg0 && i_op->lr_atom <= DW_OP_breg31))
							{
								goto continue_loop;
							}
							else
							{
								int regnum = i_op->lr_atom - DW_OP_breg0;
								Dwarf_Signed regoff = static_cast<Dwarf_Signed>(i_op->lr_number);

								// define a weight map that gives equal weight to every edge
								weight_map_t w;

								std::map<graph_traits<decltype(g)>::vertex_descriptor, graph_traits<decltype(g)>::vertex_descriptor> vertex_to_predecessor;
								boost::associative_property_map< decltype(vertex_to_predecessor) >
								  p(vertex_to_predecessor);

								std::map<graph_traits<decltype(g)>::vertex_descriptor, int> vertex_to_distance;
								boost::associative_property_map< decltype(vertex_to_distance) >
								  d(vertex_to_distance);

								std::map<graph_traits<decltype(g)>::vertex_descriptor, int> vertex_to_index;
								std::map<int, graph_traits<decltype(g)>::vertex_descriptor> index_to_vertex;
								int max_issued_index = -1;
								for (auto i_edge = g.begin(); i_edge != g.end(); ++i_edge)
								{
									{
										int source = i_edge->second.from_reg;
										if (vertex_to_index.find(source) == vertex_to_index.end())
										{ index_to_vertex[++max_issued_index] = source;
										  vertex_to_index[source] = max_issued_index; }
									}
									{
										int target = i_edge->second.to_reg;
										if (vertex_to_index.find(target) == vertex_to_index.end())
										{ index_to_vertex[++max_issued_index] = target;
										  vertex_to_index[target] = max_issued_index; }
									}
								}
								boost::associative_property_map< decltype(vertex_to_index) >
								  i(vertex_to_index);

								/* We want to start at the CFA. So if we don't 
								 * have the CFA node in the vertex map, abort. */
								if (vertex_to_index.find(DW_FRAME_CFA_COL3) == vertex_to_index.end())
								{ goto continue_loop; }
								
								dijkstra_shortest_paths(g, 
									/* start at CFA */ DW_FRAME_CFA_COL3,
									predecessor_map(p).
									distance_map(d).
									weight_map(w).
									vertex_index_map(i)
								);

								// build path by following  predecessors from the target vertex, a.k.a. regnum
								bool found_path = false;
								// we walk a path from target reg to CFA, to solve for x in
								//    regs[regnum] == CFA + x
								// ... but edges hold from->to differences, i.e. CFA->...->regnum deltas.
								// So we are collecting the right differences, but in reverse order.
								auto index_for_vertex = [&](int regnum) -> int {
									auto found = vertex_to_index.find(regnum);
									if (found != vertex_to_index.end()) return found->second;
									else return -1;
								};
								auto vertex_for_index = [&](int index) -> int {
									auto found = index_to_vertex.find(index);
									if (found != index_to_vertex.end()) return found->second;
									else return -1;
								};
								auto pos = index_for_vertex(regnum);
								int sum_of_differences = 0;
								vector<register_edge> path_edges;

								auto predecessor_of_index = [&] (int index) -> int {
									auto vertex = vertex_for_index(index);
									auto found = vertex_to_predecessor.find(vertex);
									if (found != vertex_to_predecessor.end())
									{
										int pred_vertex = found->second;
										return index_for_vertex(pred_vertex);
									}
									return -1;
								};

								// pos is an index, but p maps vertices to vertices!
								while (pos >= 0)
								{
									if (pos == index_for_vertex(DW_FRAME_CFA_COL3))
									{
										found_path = true;
										break;
									}
									else if (predecessor_of_index(pos) >= 0)
									{
										auto found_edge = std::find_if(g.begin(), g.end(), [&](const register_graph::value_type& arg) -> bool {
											return arg.second.from_reg == vertex_for_index(predecessor_of_index(pos))
												&& arg.second.to_reg == vertex_for_index(pos);
										});
										assert(found_edge != g.end());
										path_edges.push_back(found_edge->second);

										sum_of_differences += found_edge->second.difference;

									} // else we have no predecessor, so failed to find a path...
									// we will terminate at the head of the next iteration

									pos = predecessor_of_index(pos);
								}

								// if we found a path, we keep going
								success &= found_path;

								if (found_path)
								{
									if (debug) cerr << "Found that in vaddr range " 
										<< std::hex << row_overlap_interval << std::dec 
										<< " we can rewrite DW_OP_breg" << (unsigned) regnum 
										<< " " << std::showpos << regoff << std::noshowpos
										<< " in " << copied_loc_expr
										<< " with CFA" << std::showpos << sum_of_differences << std::noshowpos 
										<< " " << std::showpos << regoff << std::noshowpos
										<< endl;
									for (auto i_edge = path_edges.rbegin(); i_edge != path_edges.rend(); ++i_edge)
									{
										auto reg_name = [&](int regnum) -> const char * {
											if (regnum == DW_FRAME_CFA_COL3) return "CFA";
											else return dwarf_regnames_for_elf_machine(fs.get_elf_machine())[regnum];
										};
										if (debug) cerr << reg_name(i_edge->from_reg) 
											<< std::showpos << i_edge->difference << std::noshowpos
											<< " == " << reg_name(i_edge->to_reg) << endl;
									}
									
									/* Does this rewrite cover the whole range of the current locexpr? 
									 * If not, we need to split it. */
									 /* NO we don't! We will find all row_overlap_intervals, and 
									  * for those intervals over which the rewriting doesn't apply, 
									  * we will have the original mapping in situ. 
									auto pre_interval = right_subtract(loc_expr_int, row_overlap_interval);
									auto post_interval = left_subtract(loc_expr_int, row_overlap_interval);
									if (pre_interval.lower() != pre_interval.upper())
									{
										// insert a pre-interval after us in the loclist
										// (YES, sic -- we don't have to be in order)
										i_loc_expr = copied_l.insert(i_loc_expr + 1, copied_loc_expr) - 1;
									}
									if (post_interval.lower() != post_interval.upper())
									{
										// insert a post-interval after us in the loclist
										i_loc_expr = copied_l.insert(i_loc_expr + 1, copied_loc_expr) - 1;
									}
									*/

									// erase it!
									Dwarf_Signed reg_offset = (Dwarf_Signed) i_op->lr_number;
									unsigned size_before_erase = copied_loc_expr.size();
									i_op = copied_loc_expr.erase(i_op);
									assert(copied_loc_expr.size() == size_before_erase - 1);
									/* 
									 * now i_op points to "the new location of the element that followed 
									 * the last element erased by the function call", which is "the container 
									 * end if the operation erased the last element in the sequence".
									 */
									// HACK: I think I'm supposed to use vector::insert directly
									struct my_output_iter : public std::insert_iterator<dwarf::encap::loc_expr>
									{
										using insert_iterator::insert_iterator;
										dwarf::encap::loc_expr::iterator get_iter() const { return iter; }
									} out_iter(/*std::inserter(*/copied_loc_expr, i_op/*)*/);
									*out_iter = (expr_instr) {
										.lr_atom = DW_OP_call_frame_cfa,
										.lr_number = 0,
										.lr_number2 = 0,
										.lr_offset = 0
									};
									*out_iter = (expr_instr) {
										.lr_atom = DW_OP_consts,
										.lr_number = (Dwarf_Unsigned) (sum_of_differences + reg_offset),
										.lr_number2 = 0,
										.lr_offset = 0
									};
									*out_iter = (expr_instr) {
										.lr_atom = DW_OP_plus,
										.lr_number = 0,
										.lr_number2 = 0,
										.lr_offset = 0
									};
									i_op = out_iter.get_iter();
									// rewriting done!
									if (debug) cerr << "Rewritten loc expr is " << copied_loc_expr << endl;
									assert(i_op >= copied_loc_expr.begin());
									assert(i_op <= copied_loc_expr.end());
									// do a special increment: point i_op after out_iter
									// i_op = out_iter; // can't do this, so inserted incs above
									// continue without the usual increment
									continue;
								} // end if found_path
							} // else else is-a-breg
						continue_loop:
							++i_op;
						} // end for i_op
						
						/* We've rewritten the instruction stream, but what about the 
						 * lopc/hipc metadata? We will discard this and rebuild it using
						 * the intervals in loclist_intervals when we do the write-out
						 * at the end. */
						
						loclist_intervals[row_overlap_interval] = copied_loc_expr;
					} // end for row

					prev_fde_lopc = fde_lopc;
				} // end for FDE

				// leave 'current' [current FDE] since it might be useful on the next iteration

				// FIXME: CHECK for unoverlapped subintervals of this loc expr's interval!
				
			} // end for loc_expr
			 

			/* What about fbreg? It is just another node (with definition providing the edges).
			 * HMM. FIXME. */
			
			// build a new loclist out of loclist_intervals
			loclist fresh_l;
			for (auto i_int = loclist_intervals.begin(); i_int != loclist_intervals.end(); ++i_int)
			{
				auto expr = i_int->second;
				expr.lopc = i_int->first.lower();
				expr.hipc = i_int->first.upper();
				fresh_l.push_back(expr);
			}
			return fresh_l;
		}
		
		pair<
			graph_traits<register_graph>::out_edge_iterator, 
			graph_traits<register_graph>::out_edge_iterator
		>
		out_edges(
			graph_traits<register_graph>::vertex_descriptor u, 
			const register_graph& g)
		{
			auto base_iters = g.equal_range(u);
			return make_pair(
				graph_traits<register_graph>::out_edge_iterator(base_iters.first),
				graph_traits<register_graph>::out_edge_iterator(base_iters.second)
				);
		}
			
		pair<
			graph_traits<register_graph>::vertex_iterator,
			graph_traits<register_graph>::vertex_iterator 
		>
		vertices(const register_graph& g)
		{
			return make_pair(g.key_set.begin(), g.key_set.end());
		}
		
		graph_traits<register_graph>::degree_size_type
		out_degree(
			graph_traits<register_graph>::vertex_descriptor u,
			const register_graph& g)
		{
			auto range = g.equal_range(u);
			return srk31::count(range.first, range.second); // HACK
		}
		
		graph_traits<register_graph>::vertex_descriptor
		source(
			graph_traits<register_graph>::edge_descriptor e,
			const register_graph& g)
		{
			return e.from_reg;
		}
		
		graph_traits<register_graph>::vertex_descriptor
		target(
			graph_traits<register_graph>::edge_descriptor e,
			const register_graph& g)
		{
			return e.to_reg;
		}
		
		graph_traits<register_graph>::vertices_size_type 
		num_vertices(const register_graph& g)
		{
			return g.key_set.size();
		}
	}
}
