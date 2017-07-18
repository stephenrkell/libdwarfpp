// abstract_die and factory
#ifndef DWARFPP_ABSTRACT_HPP_
#define DWARFPP_ABSTRACT_HPP_

#include "util.hpp"
#include "spec.hpp"
#include "opt.hpp"
#include "attr.hpp"

#include "libdwarf.hpp"

namespace dwarf
{
	using std::string;
	
	namespace core
	{
		using namespace dwarf::lib;
#ifndef NO_TLS
		extern __thread Dwarf_Error current_dwarf_error;
#else
#warning "No TLS, so DWARF error reporting is not thread-safe."
		extern Dwarf_Error current_dwarf_error;
#endif
		// forward declarations
		struct root_die;
		struct iterator_base;
		struct dwarf_current_factory_t;
		struct basic_die;
		struct compile_unit_die;
		using dwarf::spec::opt;
		using dwarf::spec::spec;
		using dwarf::spec::DEFAULT_DWARF_SPEC; // FIXME: ... or get rid of spec:: namespace?

		/* This is a small interface designed to be implementable over both 
		 * libdwarf Dwarf_Die handles and whatever other representation we 
		 * choose. */
		struct abstract_die
		{
			// basically the "libdwarf methods" on core::Die...
			// ... but note that we can change the interface a little, e.g. see get_name()
			// ... to make it more generic but perhaps a little less efficient
			
			virtual Dwarf_Off get_offset() const = 0;
			virtual Dwarf_Half get_tag() const = 0;
			virtual opt<string> get_name() const = 0;
			// we can't do this because 
			// - it fixes string deletion behaviour to libdwarf-style, 
			// - it creates a circular dependency with the contents of libdwarf-handles.hpp
			//virtual unique_ptr<const char, string_deleter> get_raw_name() const = 0;
			virtual Dwarf_Off get_enclosing_cu_offset() const = 0;
			virtual bool has_attr(Dwarf_Half attr) const = 0;
			inline bool has_attribute(Dwarf_Half attr) const { return has_attr(attr); }
			virtual encap::attribute_map copy_attrs() const = 0;
			/* HMM. Can we make this non-virtual? 
			 * Just move the code from Die (and get rid of that method)? */
			virtual spec& get_spec(root_die& r) const = 0;
			string summary() const;
		};
		
		/* the in-memory version, for synthetic (non-library-backed) DIEs. */
		struct in_memory_abstract_die: public virtual abstract_die
		{
			root_die *p_root;
			Dwarf_Off m_offset;
			Dwarf_Off m_cu_offset;
			Dwarf_Half m_tag;
			encap::attribute_map m_attrs;
			
			Dwarf_Off get_offset() const { return m_offset; }
			Dwarf_Half get_tag() const { return m_tag; }
			opt<string> get_name() const 
			{ return has_attr(DW_AT_name) ? m_attrs.find(DW_AT_name)->second.get_string() : opt<string>(); }
			Dwarf_Off get_enclosing_cu_offset() const 
			{ return m_cu_offset; }
			bool has_attr(Dwarf_Half attr) const 
			{ return m_attrs.find(attr) != m_attrs.end(); }
			encap::attribute_map copy_attrs() const
			{ return m_attrs; }
			encap::attribute_map& attrs() 
			{ return m_attrs; }
			inline spec& get_spec(root_die& r) const;
			root_die& get_root() const
			{ return *p_root; }
			
			in_memory_abstract_die(root_die& r, Dwarf_Off offset, Dwarf_Off cu_offset, Dwarf_Half tag)
			 : p_root(&r), m_offset(offset), m_cu_offset(cu_offset), m_tag(tag)
			{}
		};

		// now we can define factory
		struct factory
		{
			/* This mess is caused by spec bootstrapping. We have to construct 
			 * the CU payload to read its spec info, so this doesn't work in
			 * the case of CUs. All factories behave the same for CUs, 
			 * calling the make_cu_payload method. */
		protected:
			virtual basic_die *make_non_cu_payload(abstract_die&& h, root_die& r) = 0;
			compile_unit_die *make_cu_payload(abstract_die&& , root_die& r);
			compile_unit_die *make_new_cu(root_die& r, std::function<compile_unit_die*()> constructor);
		public:
			inline basic_die *make_payload(abstract_die&& h, root_die& r);
			basic_die *make_new(const iterator_base& parent, Dwarf_Half tag);
			
			static inline factory& for_spec(dwarf::spec::spec& def);
			virtual basic_die *dummy_for_tag(Dwarf_Half tag) = 0;
		};
		struct dwarf_current_factory_t : public factory
		{
			basic_die *make_non_cu_payload(abstract_die&& h, root_die& r);
			basic_die *dummy_for_tag(Dwarf_Half tag);
		};
		extern dwarf_current_factory_t dwarf_current_factory;
		inline factory& factory::for_spec(dwarf::spec::abstract_def& def)
		{
			if (&def == &dwarf::spec::dwarf_current) return dwarf_current_factory;
			assert(false); // FIXME support more specs
		}
		struct in_memory_abstract_die;
	}
}

#endif
