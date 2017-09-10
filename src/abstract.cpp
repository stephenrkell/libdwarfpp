/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * abstract.cpp: base class and factory definitions
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#include "dwarfpp/abstract.hpp"
#include "dwarfpp/abstract-inl.hpp"
#include "dwarfpp/libdwarf-handles.hpp"
#include "dwarfpp/root.hpp"
#include "dwarfpp/root-inl.hpp"
#include "dwarfpp/iter.hpp"
#include "dwarfpp/iter-inl.hpp"
#include "dwarfpp/dies.hpp"
#include "dwarfpp/dies-inl.hpp"

#include <sstream>

namespace dwarf
{
	namespace core
	{
		string abstract_die::summary() const
		{
			std::ostringstream s;
			s << "At 0x" << std::hex << get_offset() << std::dec
						<< ", tag " << dwarf::spec::DEFAULT_DWARF_SPEC.tag_lookup(get_tag());
			return s.str();
		}
		dwarf_current_factory_t dwarf_current_factory;
		basic_die *dwarf_current_factory_t::make_non_cu_payload(abstract_die&& h, root_die& r)
		{
			basic_die *p;
			Die d(std::move(dynamic_cast<Die&&>(h)));
			assert(d.tag_here() != DW_TAG_compile_unit);
			switch (d.tag_here())
			{
#define factory_case(name, ...) \
case DW_TAG_ ## name: p = new name ## _die(d.spec_here(), std::move(d.handle)); break; // FIXME: not "basic_die"...
#include "dwarf-current-factory.h"
#undef factory_case
				default: p = new basic_die(d.spec_here(), std::move(d.handle)); break;
			}
			return p;
		}
		
		compile_unit_die *factory::make_cu_payload(abstract_die&& h, root_die& r)
		{
			// Key point: we're not allowed to call Die::spec_here() for this handle. 
			// We could write libdwarf-level code to grab the version stamp and 
			// so on... for now, just construct the thing.
			Die d(std::move(dynamic_cast<Die&&>(h)));
			Dwarf_Off off = d.offset_here();
			auto p = new compile_unit_die(dwarf::spec::dwarf_current, std::move(d.handle));
			/* fill in the CU fields -- this code would be shared by all 
			 * factories, so we put it here (but HMM, if our factories were
			 * a delegation chain, we could just put it in the root). */

			bool ret = r.set_cu_context(off);
			assert(ret);

			p->cu_header_length = *r.last_seen_cu_header_length;
			p->version_stamp = *r.last_seen_version_stamp;
			p->abbrev_offset = *r.last_seen_abbrev_offset;
			p->address_size = *r.last_seen_address_size;
			p->offset_size = *r.last_seen_offset_size;
			p->extension_size = *r.last_seen_extension_size;
			p->next_cu_header = *r.last_seen_next_cu_header;
			
			return p;
		}
		
		// like make_cu_payload, but we're called by the root_die to make a whole fresh CU
		// FIXME: this 
		compile_unit_die *factory::make_new_cu(root_die& r, std::function<compile_unit_die*()> constructor)
		{
			auto p = constructor();
			//new compile_unit_die(dwarf::spec::dwarf_current, Die::handle_type(nullptr, nullptr));
			// FIXME FIXME FIXME FIXME FIXME FIXME FIXME
			p->cu_header_length = 1;
			p->version_stamp = 2;
			p->abbrev_offset = 0;
			p->address_size = sizeof (void*);
			p->offset_size = sizeof (void*);
			p->extension_size = sizeof (void*);
			p->next_cu_header = 0;
			
			// NOTE: the DIE we just created does not even have an offset
			// until it is added to the sticky set of the containing root DIE
			
			return p;
		}
		
		basic_die *factory::make_new(const iterator_base& parent, Dwarf_Half tag)
		{
			root_die& r = parent.root();
// declare all the in-memory structs as local classes (for now)
#define factory_case(name, ...) \
			struct in_memory_ ## name ## _die : public in_memory_abstract_die, \
			     public name ## _die \
			{ \
				in_memory_ ## name ## _die(const iterator_base& parent) \
				: \
					/* initialize basic_die directly, since it's a virtual base */ \
					basic_die(parent.depth() >= 1 ? parent.spec_here() : DEFAULT_DWARF_SPEC, \
					    parent.root()), \
					in_memory_abstract_die(parent.root(), \
							  parent.is_root_position() ? \
							  parent.root().fresh_cu_offset() \
							: parent.root().fresh_offset_under(/*parent.root().enclosing_cu(parent)*/parent), \
						parent.is_root_position() ? 0 : parent.enclosing_cu_offset_here(), \
						DW_TAG_ ## name), \
					name ## _die(parent.depth() >= 1 ? parent.spec_here() : DEFAULT_DWARF_SPEC) \
				{ \
					if (parent.is_root_position()) m_cu_offset = m_offset; \
					parent.root().live_dies.insert( \
					    make_pair(this->in_memory_abstract_die::get_offset(), this) \
					); \
				 } \
				root_die& get_root() const \
				{ return *p_root; } \
				/* We also (morally redundantly) override all the abstract_die methods 
				 * to call the in_memory_abstract_die versions, in order to 
				 * provide a unique final overrider. */ \
				Dwarf_Off get_offset() const \
				{ return this->in_memory_abstract_die::get_offset(); } \
				Dwarf_Half get_tag() const \
				{ return this->in_memory_abstract_die::get_tag(); } \
				opt<string> get_name() const \
				{ return this->in_memory_abstract_die::get_name(); } \
				Dwarf_Off get_enclosing_cu_offset() const \
				{ return this->in_memory_abstract_die::get_enclosing_cu_offset(); } \
				bool has_attr(Dwarf_Half attr) const \
				{ return this->in_memory_abstract_die::has_attr(attr); } \
				encap::attribute_map copy_attrs() const \
				{ return this->in_memory_abstract_die::copy_attrs(); } \
				inline spec& get_spec(root_die& r) const \
				{ return this->in_memory_abstract_die::get_spec(r); } \
				/* also override the wider attribute API from basic_die */ \
				/* get all attrs in one go */ \
				virtual encap::attribute_map all_attrs() const \
				{ return copy_attrs(); } \
				/* get a single attr */ \
				virtual encap::attribute_value attr(Dwarf_Half a) const \
				{ if (has_attr(a)) return m_attrs.find(a)->second; else assert(false); } \
				/* get all attrs in one go, seeing through abstract_origin / specification links */ \
				/* -- this one should work already: virtual encap::attribute_map find_all_attrs() const; */ \
				/* get a single attr, seeing through abstract_origin / specification links */ \
				/* -- this one should work already: virtual encap::attribute_value find_attr(Dwarf_Half a) const; */ \
			};
#include "dwarf-current-factory.h"
#undef factory_case

			if (tag == DW_TAG_compile_unit)
			{
				return make_new_cu(r, [parent](){ return new in_memory_compile_unit_die(parent); });
			}
			
			//Dwarf_Off parent_off = parent.offset_here();
			//Dwarf_Off new_off = /*parent.is_root_position() ? r.fresh_cu_offset() : */ r.fresh_offset_under(r.enclosing_cu(parent));
			//Dwarf_Off cu_off = /*(tag == DW_TAG_compile_unit) ? new_off : */ parent.enclosing_cu_offset_here();
			
			basic_die *ret = nullptr;
			switch (tag)
			{
#define factory_case(name, ...) \
case DW_TAG_ ## name: \
			ret = new in_memory_ ## name ## _die(parent); break;
#include "dwarf-current-factory.h"
				default: return nullptr;
			}
#undef factory_case
			return ret;
			
			if (parent.depth() == 1)
			{
				auto it = r.pos(ret->get_offset(), parent.depth() + 1);
				if (it.global_name_here())
				{
					r.visible_named_grandchildren_cache.insert(make_pair(*it.global_name_here(), ret->get_offset()));
				}
			}
		}
		
		basic_die *dwarf_current_factory_t::dummy_for_tag(Dwarf_Half tag)
		{
			static basic_die dummy_basic(dwarf::spec::dwarf_current);
#define factory_case(name, ...) \
static name ## _die dummy_ ## name(dwarf::spec::dwarf_current); 
#include "dwarf-current-factory.h"
#undef factory_case
			switch (tag)
			{
#define factory_case(name, ...) \
case DW_TAG_ ## name: return &dummy_ ## name;
#include "dwarf-current-factory.h"
#undef factory_case
				default: return &dummy_basic;
			}
		}
		
		void in_memory_abstract_die::attribute_map::update_cache_on_insert(
			attribute_map::iterator inserted
		)
		{
			if (inserted->first == DW_AT_name)
			{
				auto found = p_owner->p_root->pos(p_owner->m_offset);
				assert(found);
				if (found.depth() == 2 && found.global_name_here())
				{
					// we can either invalidate the whole thing...
					// this->visible_named_grandchildren_is_complete = false;
					// ... or we can preserve the completeness invariant if it holds!
					p_owner->p_root->visible_named_grandchildren_cache.insert(
						make_pair(*found.name_here(), p_owner->m_offset)
					);
				}
			}
		}
	}
}
