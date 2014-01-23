/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * lib.hpp: basic C++ wrapping of libdwarf C API (frame section).
 *
 * Copyright (c) 2008--13, Stephen Kell.
 */

#ifndef DWARFPP_FRAME_HPP_
#define DWARFPP_FRAME_HPP_

#include <iostream>
#include <stack>
#include <vector>
#include <queue>
#include <cassert>
#include <elf.h>
#include <boost/optional.hpp>
#include <boost/icl/interval_map.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include "spec.hpp"
#include "opt.hpp"
#include "attr.hpp" // includes forward decls for iterator_df!
#include "lib.hpp"
#include "expr.hpp"

namespace dwarf
{
	namespace core
	{
		using std::pair;
		using std::make_pair;
		using std::string;
		using std::map;
		using dwarf::spec::opt;
		using namespace dwarf::lib;

		struct FrameSection;
		struct Fde;
		struct Cie;
		struct FrameSection
		{
			const Debug& dbg;
			bool using_eh;
			bool is_64bit;
			
			Dwarf_Cie *cie_data;
			Dwarf_Signed cie_element_count;
			Dwarf_Fde *fde_data;
			Dwarf_Signed fde_element_count;
			
			map<lib::Dwarf_Off, set<lib::Dwarf_Off> > fde_offsets_by_cie_offset;
			map<int, int> cie_offsets_by_index;
			/* Our iterators transform from Dwarf_Fde to Fde and Dwarf_Cie to Cie. */
			struct fde_transformer_t
			{
				const FrameSection *p_owner;
				fde_transformer_t(const FrameSection& owner) : p_owner(&owner) {}
				typedef Fde result;
				
				inline Fde operator()(Dwarf_Fde fde) const;
			} fde_transformer;
			struct cie_transformer_t
			{
				const FrameSection *p_owner;
				cie_transformer_t(const FrameSection& owner) : p_owner(&owner) {}
				
				inline Cie operator()(Dwarf_Cie cie) const;
			} cie_transformer;
			
			// transformers operate on the value, 
			// but the second type argument is the *iterator*
			typedef boost::transform_iterator< fde_transformer_t, Dwarf_Fde *, Fde, Fde > fde_iterator;
			typedef boost::transform_iterator< cie_transformer_t, Dwarf_Cie *, Cie, Cie > cie_iterator;
			
			const Debug& get_dbg() const { return dbg; }
			::Elf *get_elf() const; // don't rely on these!
			int get_elf_machine() const;
			
			inline fde_iterator fde_begin() const;
			inline fde_iterator fde_end() const;
			inline fde_iterator fde_begin();
			inline fde_iterator fde_end();

			inline cie_iterator cie_begin() const;
			inline cie_iterator cie_end() const;
			inline cie_iterator cie_begin();
			inline cie_iterator cie_end();

			inline fde_iterator begin() const;
			inline fde_iterator end() const;
			inline fde_iterator begin();
			inline fde_iterator end();

			inline FrameSection(const Debug& dbg, bool use_eh = false);
		
		private:
			// don't copy FrameSections
			inline FrameSection(const FrameSection& arg)
			: dbg(arg.dbg), fde_transformer(*this), cie_transformer(*this)
			{ assert(false); }
		public:
			
			virtual ~FrameSection()
			{
				if (cie_data) dwarf_fde_cie_list_dealloc(dbg.raw_handle(), cie_data, cie_element_count, fde_data, fde_element_count);
				else
				{
					assert(cie_element_count == 0);
					assert(fde_element_count == 0);
				}
			};
			/* "whole section" methods
			     dwarf_set_frame_rule_table_size (for pre-getting-fde-info table sizing, ABI-dependent)
			     dwarf_set_frame_rule_initial_value (similar)
			     dwarf_set_frame_cfa_value (similar)
			     dwarf_set_frame_same_value (similar)
			     dwarf_set_frame_undefined_value (similar)
			     dwarf_get_fde_n
			     dwarf_get_fde_at_pc
			 */
			
			/* libdwarf "methods" relevant to a CIE:
			   
			     dwarf_get_cie_info (claimed "internal-only")
			     dwarf_get_cie_index (claimed "little used")
			     dwarf_expand_frame_instructions (expensive? encap-like)
			 */	
			
			inline fde_iterator find_fde_for_pc(Dwarf_Addr pc) const;
			struct register_def
			{
				enum kind 
				{
					INDETERMINATE,
					UNDEFINED, // i.e. explicitly "undefined"
					SAME_VALUE,
					SAVED_AT_OFFSET_FROM_CFA,
					VAL_IS_OFFSET_FROM_CFA,
					REGISTER,
					SAVED_AT_EXPR,
					VAL_OF_EXPR
				} k;
				/*union*/ struct value_t // union makes us non-POD, so avoid the boilerplate operator{=,==,...} for now
				{
					int m_undefined; // unused
					int m_same_value; // unused
					int m_saved_at_offset_from_cfa;
					int m_val_is_offset_from_cfa;
					pair<int, int> m_register_plus_offset; // "second" a.k.a. offset is only used for CFA-defining rules
					encap::loc_expr m_saved_at_expr;
					encap::loc_expr m_val_of_expr;
				} value;
				int  undefined_r() const { assert (k == UNDEFINED); return value.m_undefined; }
				int& undefined_w()       { k = UNDEFINED;           return value.m_undefined; }

				int  same_value_r() const { assert (k == SAME_VALUE); return value.m_same_value; }
				int& same_value_w()       { k = SAME_VALUE;           return value.m_same_value; }

				int  saved_at_offset_from_cfa_r() const { assert (k == SAVED_AT_OFFSET_FROM_CFA); return value.m_saved_at_offset_from_cfa; }
				int& saved_at_offset_from_cfa_w()       { k = SAVED_AT_OFFSET_FROM_CFA;           return value.m_saved_at_offset_from_cfa; }

				int  val_is_offset_from_cfa_r() const { assert (k == VAL_IS_OFFSET_FROM_CFA); return value.m_val_is_offset_from_cfa; }
				int& val_is_offset_from_cfa_w()       { k = VAL_IS_OFFSET_FROM_CFA;           return value.m_val_is_offset_from_cfa; }

				pair<int, int>  register_plus_offset_r() const { assert (k == REGISTER); return value.m_register_plus_offset; }
				pair<int, int>& register_plus_offset_w()       { k = REGISTER;           return value.m_register_plus_offset; }

				encap::loc_expr  saved_at_expr_r() const { assert(k == SAVED_AT_EXPR); return value.m_saved_at_expr; }
				encap::loc_expr& saved_at_expr_w()       { k = SAVED_AT_EXPR;          return value.m_saved_at_expr; }

				encap::loc_expr  val_of_expr_r() const { assert(k == VAL_OF_EXPR); return value.m_val_of_expr; }
				encap::loc_expr& val_of_expr_w()       { k = VAL_OF_EXPR;          return value.m_val_of_expr; }

				bool operator<(const register_def& r) const
				{
					if (this->k < r.k) return true;
					if (r.k < this->k) return false;
					// else they're equal-keyed
					if (
						(k == UNDEFINED && undefined_r() < r.undefined_r())
					||  (k == SAVED_AT_OFFSET_FROM_CFA && saved_at_offset_from_cfa_r() < r.saved_at_offset_from_cfa_r())
					||  (k == VAL_IS_OFFSET_FROM_CFA && val_is_offset_from_cfa_r() < r.val_is_offset_from_cfa_r())
					||  (k == REGISTER && register_plus_offset_r() < r.register_plus_offset_r())
					||  (k == SAVED_AT_EXPR && saved_at_expr_r() < r.saved_at_expr_r())
					||  (k == VAL_OF_EXPR && val_of_expr_r() < r.val_of_expr_r())) return true;
					else return false;
				}
				bool operator==(const register_def& r) const
				{
					if (this->k != r.k) return false;
					return (
						(k == UNDEFINED && undefined_r() == r.undefined_r())
					||  (k == SAVED_AT_OFFSET_FROM_CFA && saved_at_offset_from_cfa_r() == r.saved_at_offset_from_cfa_r())
					||  (k == VAL_IS_OFFSET_FROM_CFA && val_is_offset_from_cfa_r() == r.val_is_offset_from_cfa_r())
					||  (k == REGISTER && register_plus_offset_r() == r.register_plus_offset_r())
					||  (k == SAVED_AT_EXPR && saved_at_expr_r() == r.saved_at_expr_r())
					||  (k == VAL_OF_EXPR && val_of_expr_r() == r.val_of_expr_r()));
				}
			};

			struct instrs_results
			{
				boost::icl::interval_map<Dwarf_Addr, set<pair<int /* regnum */, register_def > > > rows;
				std::map<int, register_def> unfinished_row;
				Dwarf_Addr unfinished_row_addr;
				
				void add_unfinished_row(Dwarf_Addr high_pc) 
				{
					set< pair< int, FrameSection::register_def > > current_row_defs_set(
						unfinished_row.begin(), unfinished_row.end()
					);
					rows += make_pair( 
						boost::icl::interval<Dwarf_Addr>::right_open(unfinished_row_addr, high_pc),
						current_row_defs_set
					);
					unfinished_row.clear();
					unfinished_row_addr = std::numeric_limits<Dwarf_Addr>::max();
				}
			};

			// extracted lambda function
			instrs_results interpret_instructions(const Cie& cie, 
				Dwarf_Addr initial_row_addr, 
				Dwarf_Ptr instrs, Dwarf_Unsigned instrs_len,
				optional< const instrs_results & > initial_instrs_results = optional< const instrs_results & >()) const;
		};
		struct Cie
		{
			friend struct FrameSection;
			
		private:
			const FrameSection& owner; 
			
			Dwarf_Cie m_cie;
			Dwarf_Unsigned bytes_in_cie;
			Dwarf_Small version;
			char *augmenter;
			Dwarf_Unsigned code_alignment_factor;
			Dwarf_Signed data_alignment_factor;
			Dwarf_Half return_address_register_rule;
			Dwarf_Ptr initial_instructions;
			Dwarf_Unsigned initial_instructions_length;
			// init'd in init_augmentation_bytes()
			unsigned char address_size_in_dwarf;
			unsigned char segment_size_in_dwarf;
			// we snarf the augmentation bytes
			vector<Dwarf_Small> augbytes;
			
		public:
			// use sed -r 's/(([a-zA-Z0-9_]+)( *\*)?) *([a-zA-Z0-9_]+)/\1 get_\4() const { return \4; }/'
			Dwarf_Cie raw_handle() const { return m_cie; };
			inline FrameSection::cie_iterator iterator_here() const;
			Dwarf_Off get_cie_offset() const { 
				int index = iterator_here() - owner.cie_begin();
				auto found = owner.cie_offsets_by_index.find(index);
				assert(found != owner.cie_offsets_by_index.end());
				return found->second;
			}
			const FrameSection& get_owner() const { return owner; }
			Dwarf_Off get_offset() const { return get_cie_offset(); } // alias
			Dwarf_Unsigned get_bytes_in_cie() const { return bytes_in_cie; };
			Dwarf_Small get_version() const { return version; };
			char * get_augmenter() const { return augmenter; };
			std::vector<Dwarf_Small>::const_iterator find_augmentation_element(char marker) const;
			int get_fde_encoding() const;
			unsigned encoding_nbytes(unsigned char encoding, unsigned char const *bytes, unsigned char const *limit) const;
			Dwarf_Unsigned  read_with_encoding(unsigned char encoding, unsigned char const **pos, unsigned char const *limit, bool use_host_byte_order) const;
			std::vector<Dwarf_Small> const& get_augmentation_bytes() const { return augbytes; }
			unsigned char get_address_size() const;
			unsigned char get_segment_size() const;
		private:
			void init_augmentation_bytes();
		public:
			Dwarf_Unsigned get_code_alignment_factor() const { return code_alignment_factor; };
			Dwarf_Signed get_data_alignment_factor() const { return data_alignment_factor; };
			Dwarf_Half get_return_address_register_rule() const { return return_address_register_rule; };
			Dwarf_Ptr get_initial_instructions() const { return initial_instructions; };
			Dwarf_Unsigned get_initial_instructions_length() const { return initial_instructions_length; };
			pair<unsigned char *, unsigned char *> initial_instructions_seq() const
			{ return make_pair(
				(unsigned char *) get_initial_instructions(), 
				(unsigned char *) get_initial_instructions() + get_initial_instructions_length()
				);
			}
			bool operator==(const Cie& arg) const { return arg.m_cie == this->m_cie; }
			bool operator!=(const Cie& arg) const { return arg.m_cie != this->m_cie; }

		private:
			Cie(const FrameSection& owner, Dwarf_Cie cie)
			 : owner(owner), m_cie(cie)
			{
				int cie_ret = dwarf_get_cie_info(m_cie, &bytes_in_cie, &version, &augmenter, 
					&code_alignment_factor, &data_alignment_factor, &return_address_register_rule, 
					&initial_instructions, &initial_instructions_length, &current_dwarf_error);
				assert(cie_ret == DW_DLV_OK);
				init_augmentation_bytes();
			}

			Cie(const FrameSection& owner, Dwarf_Fde fde)
			 : owner(owner)
			{
				int cie_ret = dwarf_get_cie_of_fde(fde, &m_cie, &current_dwarf_error);
				assert(cie_ret == DW_DLV_OK);
				cie_ret = dwarf_get_cie_info(m_cie, &bytes_in_cie, &version, &augmenter, 
					&code_alignment_factor, &data_alignment_factor, &return_address_register_rule, 
					&initial_instructions, &initial_instructions_length, &current_dwarf_error);
				assert(cie_ret == DW_DLV_OK);
				init_augmentation_bytes();
			}
		};

		struct Fde
		{
			friend struct FrameSection;
			
			/* libdwarf "methods" relevant to an FDE:
			
			     dwarf_get_cie_of_fde
			     dwarf_get_fde_range (also gets CIE)
			     dwarf_get_fde_instr_bytes
			     dwarf_get_fde_info_for_reg3
			     dwarf_get_fde_info_for_cfa_reg3
			     dwarf_get_fde_info_for_all_regs3
			 */
		private:
			const FrameSection& owner;
			Dwarf_Fde m_fde;
			Dwarf_Addr low_pc;
			Dwarf_Unsigned func_length;
			Dwarf_Ptr fde_bytes;
			Dwarf_Unsigned fde_byte_length;
			Dwarf_Off m_cie_offset;
			Dwarf_Signed cie_index;
			Dwarf_Off fde_offset;
			
			std::vector<Dwarf_Small> augbytes;
			
		public:
			Fde& operator=(const Fde& arg)
			{
				assert(&owner == &arg.owner);
				m_fde = arg.m_fde;
				low_pc = arg.low_pc;
				func_length = arg.func_length;
				fde_bytes = arg.fde_bytes;
				fde_byte_length = arg.fde_byte_length;
				m_cie_offset = arg.m_cie_offset;
				cie_index = arg.cie_index;
				fde_offset = arg.fde_offset;
				return *this;
			}
		

			FrameSection::instrs_results
			decode() const;
			
			Dwarf_Fde raw_handle() const { return m_fde; }
			Dwarf_Off get_fde_offset() const   { return fde_offset; }
			Dwarf_Off get_offset() const { return get_fde_offset(); } // alias
			Dwarf_Signed get_cie_index() const { return cie_index; }
			Dwarf_Addr get_low_pc() const { return low_pc; }
			Dwarf_Unsigned get_func_length() const { return func_length; }
			Dwarf_Ptr get_fde_bytes() const { return fde_bytes; }
			Dwarf_Unsigned get_fde_byte_length() const { return fde_byte_length; }
			const std::vector<Dwarf_Small>& get_augmentation_bytes() const { return augbytes; }
			Dwarf_Unsigned get_lsda_pointer() const;
		private:
			void init_augmentation_bytes();
		public:
			
			pair<unsigned char *, unsigned char *> instr_bytes_seq() const 
			{
				lib::Dwarf_Ptr instrbytes;
				lib::Dwarf_Unsigned len;
				int fde_ret = dwarf_get_fde_instr_bytes(m_fde, &instrbytes, &len, &core::current_dwarf_error);
				assert(fde_ret == DW_DLV_OK);
				return make_pair((unsigned char *) instrbytes, (unsigned char *) instrbytes + len);
			}
			
			FrameSection::cie_iterator 
			find_cie() const
			{
				Dwarf_Cie cie;
				int cie_ret = dwarf_get_cie_of_fde(m_fde, &cie, &core::current_dwarf_error);
				assert(cie_ret == DW_DLV_OK);
				// the iterator needs a Dwarf_Cie*, so we have to find this Dwarf_Cie
				// in the array
				auto found = std::find(owner.cie_data, owner.cie_data + owner.cie_element_count, cie);
				assert(found != owner.cie_data + owner.cie_element_count);
				return FrameSection::cie_iterator(found, owner.cie_transformer);
			}
			Dwarf_Off 
			get_id() const
			{
				/* the byte offset from this field to the start of the CIE 
				  with which this FDE is associated. 
				  The byte offset goes to the length record of the CIE. 
				  A positive value goes backward; that is, 
				  you have to subtract the value of the ID field from the current byte position 
				  to get the CIE position. */

				// note: FDE and CIE id field have offset  is_64bit ? 12 : 4
				// CIE length field has offset             is_64bit ? 4  : 0

				auto i_cie = find_cie();
				Dwarf_Off cie_off = i_cie->get_offset();
				Dwarf_Off cie_length_off = cie_off + (owner.is_64bit ? 4  : 0);
				Dwarf_Off fde_id_off = get_offset() + (owner.is_64bit ? 12 : 4);
				return fde_id_off - cie_length_off;
			}
			Dwarf_Off
			get_cie_offset() const
			{
				return owner.using_eh 
					? fde_offset + (owner.is_64bit ? 12 : 4) - m_cie_offset - (owner.is_64bit ? 4 : 0)
					: m_cie_offset; 
			}
			bool operator==(const Fde& arg) const { return arg.m_fde == this->m_fde; }
			bool operator!=(const Fde& arg) const { return arg.m_fde != this->m_fde; }

		private: // FrameSection is our friend, and does the constructing
			Fde(const FrameSection& owner, Dwarf_Fde fde)
			 : owner(owner), m_fde(fde)
			{
				int fde_ret = dwarf_get_fde_range(fde, &low_pc, &func_length, &fde_bytes, 
					&fde_byte_length, &m_cie_offset, &cie_index, &fde_offset, &core::current_dwarf_error);
				assert(fde_ret == DW_DLV_OK);
				init_augmentation_bytes();
			}
		};
		
		inline Fde FrameSection::fde_transformer_t::operator()(Dwarf_Fde fde) const
		{
			return Fde(*p_owner, fde);
		}
		inline Cie FrameSection::cie_transformer_t::operator()(Dwarf_Cie cie) const
		{
			return Cie(*p_owner, cie);
		}
		inline FrameSection::fde_iterator FrameSection::fde_begin() const { return fde_iterator(fde_data, fde_transformer); }
		inline FrameSection::fde_iterator FrameSection::fde_end() const   { return fde_iterator(fde_data + fde_element_count, fde_transformer); }
		inline FrameSection::fde_iterator FrameSection::fde_begin() { return fde_iterator(fde_data, fde_transformer); }
		inline FrameSection::fde_iterator FrameSection::fde_end()  { return fde_iterator(fde_data + fde_element_count, fde_transformer); }

		inline FrameSection::cie_iterator FrameSection::cie_begin() const { return cie_iterator(cie_data, cie_transformer); }
		inline FrameSection::cie_iterator FrameSection::cie_end() const   { return cie_iterator(cie_data + cie_element_count, cie_transformer); }
		inline FrameSection::cie_iterator FrameSection::cie_begin() { return cie_iterator(cie_data, cie_transformer); }
		inline FrameSection::cie_iterator FrameSection::cie_end()   { return cie_iterator(cie_data + cie_element_count, cie_transformer); }

		inline FrameSection::fde_iterator FrameSection::begin() const { return fde_begin(); }
		inline FrameSection::fde_iterator FrameSection::end() const   { return fde_end(); }
		inline FrameSection::fde_iterator FrameSection::begin() { return fde_begin(); }
		inline FrameSection::fde_iterator FrameSection::end() { return fde_end(); }

		inline FrameSection::FrameSection(const Debug& dbg, bool use_eh /* = false */)
		 : dbg(dbg), using_eh(use_eh), is_64bit(false), fde_transformer(*this), cie_transformer(*this)
		{

			int ret = (use_eh ? dwarf_get_fde_list_eh : dwarf_get_fde_list)(
						dbg.raw_handle(), &cie_data, &cie_element_count, 
						&fde_data, &fde_element_count, &current_dwarf_error);

			assert(ret == DW_DLV_OK || ret == DW_DLV_NO_ENTRY);
			if (ret == DW_DLV_NO_ENTRY)
			{
				/* Set up empty arrays. */
				fde_element_count = 0;
				fde_data = nullptr;
				cie_element_count = 0;
				cie_data = nullptr;
			}

			/* Since libdwarf doesn't let us get the CIE offset, do a pass
			 * to build a table of these eagerly. */
			for (auto i_fde = fde_begin(); i_fde != fde_end(); ++i_fde)
			{
				fde_offsets_by_cie_offset[i_fde->get_cie_offset()].insert(i_fde->get_fde_offset());
				lib::Dwarf_Signed index;
				lib::Dwarf_Cie cie;
				int cie_ret = dwarf_get_cie_of_fde(i_fde->raw_handle(), &cie, &core::current_dwarf_error);
				assert(cie_ret == DW_DLV_OK);
				int index_ret = dwarf_get_cie_index(cie, &index, &core::current_dwarf_error);
				assert(index_ret == DW_DLV_OK);
				cie_offsets_by_index[index] = i_fde->get_cie_offset();
			}

			// do we have any orphan CIEs?
			assert(cie_offsets_by_index.size() == (unsigned) cie_element_count);
		}
		inline FrameSection::fde_iterator FrameSection::find_fde_for_pc(Dwarf_Addr pc) const
		{
			Dwarf_Addr lopc;
			Dwarf_Addr hipc;
			Dwarf_Fde fde;
			int ret = dwarf_get_fde_at_pc(fde_data, pc, &fde, &lopc, &hipc, &core::current_dwarf_error);
			if (ret == DW_DLV_NO_ENTRY) return fde_end();
			assert(ret == DW_DLV_OK);
			auto found = std::find(fde_data, fde_data + fde_element_count, fde);
			assert(found != fde_data + fde_element_count);
			// assert that this FDE's range is consistent with what we asked for
			Fde f(*this, fde);
			assert(lopc >= f.get_low_pc() && lopc <  f.get_low_pc() + f.get_func_length());
			assert(hipc >= f.get_low_pc() && hipc <= f.get_low_pc() + f.get_func_length());
			return fde_iterator(found, fde_transformer);
		}
		inline FrameSection::cie_iterator Cie::iterator_here() const
		{
			auto found = std::find(owner.cie_begin(), owner.cie_end(), *this);
			assert(found != owner.cie_end());
			return found;
		}
	}
}

#endif
