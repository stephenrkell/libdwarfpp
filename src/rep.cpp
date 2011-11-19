#include "spec_adt.hpp"

/* FIXME: this logic (and the is_rep_compatible() API call) doesn't really belong
 * in libdwarfpp. Instead, once clients can thread factories to diesets, 
 * clients which need this sort of horizontal extension should be able to add it
 * in their factory implementations. */

namespace dwarf
{
	namespace spec
	{
		// utility shared between class, struct and (HACK) union types
		static
		bool
		is_structurally_rep_compatible(
			boost::shared_ptr<type_die> arg1, boost::shared_ptr<type_die> arg2)
		{
			auto arg1_with_named_children = boost::dynamic_pointer_cast<with_named_children_die>(arg1);
			auto arg2_with_named_children = boost::dynamic_pointer_cast<with_named_children_die>(arg2);
			if (!(arg1_with_named_children && arg2_with_named_children)) return false;
			
			// HACK: approximately, we require same tags
			// (both structs, or both classes, or both unions...)
			if (arg1->get_tag() != arg2->get_tag()) return false;
		
			// if we don't know the byte size, we're not compatible
			if (!arg1->calculate_byte_size() || !arg2->calculate_byte_size()
			|| *arg1->calculate_byte_size() != *arg2->calculate_byte_size())
			{
				std::cerr << "Warning: encountered DWARF structured type with indeterminate size."
					<< std::endl;
				return false;
			}
			
			// HACK: our rep-compatibility relation is slightly asymmetric here
			// in that if the arg defines extra fields that don't interfere with
			// ours, we don't consider it incompatible. But it would consider
			// us incompatible with it....

			for (auto i_child = arg1_with_named_children->children_begin();
				i_child != arg1_with_named_children->children_end();
				i_child++)
			{
				if ((*i_child)->get_tag() != DW_TAG_member) continue;
				
				auto p_member = boost::dynamic_pointer_cast<member_die>(*i_child);
				auto i_member = &p_member;
				
				if (!(*i_member)->get_name())
				{
					std::cerr << "Warning: encountered DWARF structured type with nameless members."
						<< std::endl;
					return false;
				}
				auto like_named_element = 
				 arg2_with_named_children->named_child(*(*i_member)->get_name());
				if (!like_named_element)
				{
					// other type doesn't have a like-named member, so false
					return false;
					// FIXME: support a name-mapping in here, so that field renamings
					// can recover rep-compatibility
				}
				auto like_named_member = boost::dynamic_pointer_cast<member_die>(
					like_named_element);
				if (!like_named_member) return false;
				
				auto like_named_byte_offset
				= like_named_member->byte_offset_in_enclosing_type();
				
				if (!like_named_byte_offset
				|| !(*i_member)->byte_offset_in_enclosing_type()
				|| *like_named_byte_offset != *(*i_member)->byte_offset_in_enclosing_type())
				{ return false; }
				
				if (!like_named_member->get_type() || !(*i_member)->get_type()
				|| !like_named_member->get_type()->is_rep_compatible((*i_member)->get_type()))
				{ return false; }
				
				// else we're good so far
			}
			return true;
		}

	
        bool type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
        {
        	// first, try to make ourselves concrete to
			// get rid of typedefs and qualifiers
			if (this->get_concrete_type() != this->get_this()
			||  arg->get_concrete_type() != arg)
			{
				return this->get_concrete_type()->is_rep_compatible(arg->get_concrete_type());
			}
			// if we're already concrete, default is not rep-compatible
			// -- overrides will refine this appropriately
			std::cerr << "Warning: is_rep_compatible bailing out with default false." << std::endl;
			return false;
        }
		bool array_type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
		{
        	// first, try to make arg concrete
			if (arg->get_concrete_type() != arg) return this->is_rep_compatible(
				arg->get_concrete_type());			
			// HMM: do we want singleton arrays to be rep-compatible wit
			// non-array single objects? Not so at present.
			auto arg_array_type = boost::dynamic_pointer_cast<array_type_die>(arg);
			if (!arg_array_type) return false;
			
			return this->calculate_byte_size() && arg_array_type->calculate_byte_size()
				&& *this->calculate_byte_size() == *arg_array_type->calculate_byte_size()
				&& this->get_type() && arg_array_type->get_type()
				&& this->get_type()->is_rep_compatible(arg_array_type->get_type());
		}
		bool pointer_type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
		{
        	// first, try to make arg concrete
			if (arg->get_concrete_type() != arg) return this->is_rep_compatible(
				arg->get_concrete_type());			
			// HMM: do we want pointers and references to be mutually
			// rep-compatible? Not so at present.
			auto arg_pointer_type = boost::dynamic_pointer_cast<pointer_type_die>(arg);
			if (!arg_pointer_type) return false;
			else return true; // all pointers are rep-compatible
		}
		bool reference_type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
		{
        	// first, try to make arg concrete
			if (arg->get_concrete_type() != arg) return this->is_rep_compatible(
				arg->get_concrete_type());			
			// HMM: do we want pointers and references to be mutually
			// rep-compatible? Not so at present.
			auto arg_reference_type = boost::dynamic_pointer_cast<reference_type_die>(arg);
			if (!arg_reference_type) return false;
			else return true; // all references are rep-compatible		
		}
		bool base_type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
		{
        	// first, try to make arg concrete
			if (arg->get_concrete_type() != arg) return this->is_rep_compatible(
				arg->get_concrete_type());			
			auto arg_base_type = boost::dynamic_pointer_cast<base_type_die>(arg);
			if (!arg_base_type) return false;
			
			return arg_base_type->get_encoding() == this->get_encoding()
				&& arg_base_type->get_byte_size() == this->get_byte_size()
				&& arg_base_type->get_bit_size () == this->get_bit_size()
				&& arg_base_type->get_bit_offset() == this->get_bit_offset();
		}
		bool structure_type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
		{
        	// first, try to make arg concrete
			if (arg->get_concrete_type() != arg) return this->is_rep_compatible(
				arg->get_concrete_type());			
			auto nonconst_this = const_cast<structure_type_die *>(this); // HACK: remove
			return is_structurally_rep_compatible(
				boost::dynamic_pointer_cast<type_die>(nonconst_this->get_this()), 
				arg);
		}
		bool union_type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
		{
        	// first, try to make arg concrete
			if (arg->get_concrete_type() != arg) return this->is_rep_compatible(
				arg->get_concrete_type());			
			auto nonconst_this = const_cast<union_type_die *>(this); // HACK: remove
			return is_structurally_rep_compatible(
				boost::dynamic_pointer_cast<type_die>(nonconst_this->get_this()), 
				arg);
		}
		bool class_type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
		{
        	// first, try to make arg concrete
			if (arg->get_concrete_type() != arg) return this->is_rep_compatible(
				arg->get_concrete_type());			
			auto nonconst_this = const_cast<class_type_die *>(this); // HACK: remove
			return is_structurally_rep_compatible(
				boost::dynamic_pointer_cast<type_die>(nonconst_this->get_this()), 
				arg);
		}
		bool enumeration_type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
		{
        	// first, try to make arg concrete
			if (arg->get_concrete_type() != arg) return this->is_rep_compatible(
				arg->get_concrete_type());			
			auto arg_enumeration_type = boost::dynamic_pointer_cast<enumeration_type_die>(arg);
			auto arg_base_type = boost::dynamic_pointer_cast<base_type_die>(arg);
			if (!arg_enumeration_type && !arg_base_type) return false;
			if (arg_enumeration_type) 
				return this->get_type() && arg_enumeration_type->get_type()
				 && this->get_type()->is_rep_compatible(arg_enumeration_type->get_type());
			else return this->get_type()->is_rep_compatible(arg_base_type);
		}
		bool subroutine_type_die::is_rep_compatible(boost::shared_ptr<type_die> arg) const
		{
        	// first, try to make arg concrete
			if (arg->get_concrete_type() != arg) return this->is_rep_compatible(
				arg->get_concrete_type());			
			return false; // FIXME
		}
	}
}
