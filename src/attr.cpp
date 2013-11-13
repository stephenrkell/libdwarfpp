#include "attr.hpp"
#include "encap.hpp"
#include "spec_adt.hpp" /* for basic_die methods */
#include "expr.hpp"

#include <utility>
using std::make_pair;

namespace dwarf
{
	namespace encap
    {
		std::shared_ptr<spec::type_die> attribute_value::get_refdie_is_type() const 
		{ return std::dynamic_pointer_cast<spec::type_die>(get_refdie()); }
		
		void attribute_value::print_raw(std::ostream& s) const
		{
			switch (f)
			{
				case NO_ATTR:
					s << "(not present)";
					break;
				case FLAG:
					s << "(flag) " << (v_flag ? "true" : "false");
					break;				
				case UNSIGNED:
					s << "(unsigned) " << v_u;
					break;				
				case SIGNED:
					s << "(signed) " << v_s;
					break;
				case BLOCK:
					s << "(block) ";
					for (std::vector<unsigned char>::iterator p = v_block->begin(); p != v_block->end(); p++)
					{
						//s.setf(std::ios::hex);
						s << std::hex << (int) *p << std::dec << " ";
						//s.unsetf(std::ios::hex);
					}
					break;
				case STRING:
					s << "(string) " << *v_string;
					break;
				
				case REF:
					s << "(reference, " << (v_ref->abs ? "global) " : "nonglobal) ");
					s << "0x" << std::hex << v_ref->off << std::dec;
					
					break;
				
				case ADDR:
					s << "(address) 0x" << std::hex << v_addr << std::dec;
					break;
				
				case LOCLIST:
					print_as(s, spec::interp::loclistptr);
					break;
					
				case RANGELIST:
					print_as(s, spec::interp::rangelistptr);
					break;
				
				case UNRECOG:
					s << "(unrecognised attribute)";
					break;
				
				default: 
					s << "FIXME! (not present)";
					break;
			
			}			
		} // end attribute_value::print	

		attribute_value::form attribute_value::dwarf_form_to_form(const Dwarf_Half form)
		{
			switch (form)
			{
				case DW_FORM_addr:
					return dwarf::encap::attribute_value::ADDR;
				case DW_FORM_block2:
				case DW_FORM_block4:
				case DW_FORM_data2:
				case DW_FORM_data4:
				case DW_FORM_data8:
				case DW_FORM_block:
				case DW_FORM_block1:
				case DW_FORM_data1:
				case DW_FORM_udata:
					return dwarf::encap::attribute_value::UNSIGNED;
				case DW_FORM_string:
				case DW_FORM_strp:
					return dwarf::encap::attribute_value::STRING;
				case DW_FORM_sdata:
					return dwarf::encap::attribute_value::SIGNED;
				case DW_FORM_flag:
				case DW_FORM_ref_addr:
				case DW_FORM_ref1:
				case DW_FORM_ref2:
				case DW_FORM_ref4:
				case DW_FORM_ref8:
				case DW_FORM_ref_udata:
				case DW_FORM_indirect:
				default:
					cerr << "Warning: unknown attribute form 0x" 
						<< std::hex << form << std::dec << endl;
					return dwarf::encap::attribute_value::NO_ATTR;
			}	
		}
	
    	std::shared_ptr<spec::basic_die> attribute_value::get_refdie() const
        //spec::basic_die& attribute_value::get_refdie() const
	    { assert(f == REF); 
		  assert(p_ds);
          return /* * */(*p_ds)[v_ref->off]; }

		std::ostream& operator<<(std::ostream& s, const attribute_value v)
		{
			v.print_raw(s);
			return s;
		}
        
        std::ostream& operator<<(std::ostream& s, const rangelist& rl)
        {
        	s << "rangelist { ";
            for (auto i_r = rl.begin(); i_r != rl.end(); ++i_r)
            {
            	if (i_r != rl.begin()) s << ", ";
            	s << *i_r;
            }
            s << "}";
            return s;
        }
        std::ostream& operator<<(std::ostream& s, const attribute_value::address& a)
        {
        	s << a.addr;
            return s;
        }
        bool operator==(Dwarf_Addr arg, attribute_value::address a)
        {
        	return arg == a.addr;
        }
        bool operator!=(Dwarf_Addr arg, attribute_value::address a)
        {
        	return arg != a.addr;
        }
        bool operator<(Dwarf_Addr arg, attribute_value::address a)
        {
        	return arg < a.addr;
		}        
        bool operator<=(Dwarf_Addr arg, attribute_value::address a)
        {
        	return arg <= a.addr;
        }
        bool operator>(Dwarf_Addr arg, attribute_value::address a)
        {
        	return arg > a.addr;
        }
        bool operator>=(Dwarf_Addr arg, attribute_value::address a)
        {
        	return arg >= a.addr;
        }
        Dwarf_Addr operator-(Dwarf_Addr arg, attribute_value::address a)
        {
        	return arg - a.addr;
		}
        Dwarf_Addr operator-(attribute_value::address a, Dwarf_Addr arg)
        {
        	return a.addr - arg;
		}             
    }
    namespace lib
    {
        std::ostream& operator<<(std::ostream& s, const Dwarf_Ranges& rl)
        {
        	switch (rl.dwr_type)
            {
            	case DW_RANGES_ENTRY:
        	        s << "[0x" << std::hex << rl.dwr_addr1 
            	        << ", 0x" << std::hex << rl.dwr_addr2 << ")";
                break;
                case DW_RANGES_ADDRESS_SELECTION:
                	assert(rl.dwr_addr1 == 0xffffffff || rl.dwr_addr1 == 0xffffffffffffffffULL);
                    s << "set base 0x" << std::hex << rl.dwr_addr2;
                break;
                case DW_RANGES_END:
                	assert(rl.dwr_addr1 == 0 && rl.dwr_addr2 == 0);
                    s << "end";
                break;
                default: assert(false); break;
            }
            return s;
        }
    }
    namespace encap {
		void attribute_value::print_as(std::ostream& s, int cls) const
		{
			switch(cls)
			{
				case spec::interp::address: switch(f)
				{
					case ADDR:
						print_raw(s);
						break;
					default: assert(false);
				} break;				
				case spec::interp::block: switch(f)
				{
					case BLOCK:
						print_raw(s);
						break;
					default: assert(false);
				} break;		
				case spec::interp::constant: switch(f)
				{
					case UNSIGNED:
					case SIGNED:
						print_raw(s);
						break;
					default: assert(false);
				} break;		
				case spec::interp::lineptr: switch(f)
				{
					case UNSIGNED: // and specifically data4 or data8
						s << "(lineptr) 0x" << std::hex << v_u << std::dec;
						break;					
					default: assert(false);
				} break;
				case spec::interp::block_as_dwarf_expr:		
				case spec::interp::loclistptr: switch(f)
				{
					case LOCLIST:
						s << *v_loclist; 
						break;
					default: assert(false);
				} break;		
				case spec::interp::macptr: switch(f)
				{
					case UNSIGNED: // specifically data4 or data8
						s << "(macptr) 0x" << std::hex << v_u << std::dec;
						break;
					default: assert(false);
				} break;		
				case spec::interp::rangelistptr: switch(f)
				{
					case RANGELIST: // specifically data4 or data8
						//s << "(rangelist) 0x" << std::hex << v_u << std::dec;
                        s << *v_rangelist;
						break;
					default: assert(false);
				} break;
				case spec::interp::string: switch(f)
				{
					case STRING: // string or strp, we don't care
						print_raw(s);
						break;
					default: assert(false);
				} break;		
				case spec::interp::flag: switch(f)
				{
					case FLAG:
						print_raw(s);
						break;
					default: assert(false);
				} break;		
				case spec::interp::reference: switch(f)
				{
					case REF:
						print_raw(s);
						break;
					default: assert(false);
				} break;
				default: 
					s << "(raw) ";
					print_raw(s);
			}			
		} // end attribute_value::print_as
		
		// temporary HACK: copy  (... increasingly less like a copy)
		attribute_value::attribute_value(const dwarf::core::Attribute& a, 
			const core::Die& d,
			root_die& r)
		{
			int retval;
			orig_form = 0;
			retval = dwarf_whatform(a.handle.get(), &orig_form, &core::current_dwarf_error);

			Dwarf_Unsigned u;
			Dwarf_Signed s;
			Dwarf_Bool flag;
			Dwarf_Off o;
			Dwarf_Addr addr;
			char *str;
			int cls = spec::interp::EOL; // dummy initialization
			
			// find our dwarf spec
			Dwarf_Off cu_offset = d.enclosing_cu_offset_here();
			dwarf::spec::abstract_def& spec = r.cu_pos(cu_offset).spec_here();

			if (retval != DW_DLV_OK) goto fail; // retval set by whatform() above
			Dwarf_Half attr; 
			retval = dwarf_whatattr(a.handle.get(), &attr, &core::current_dwarf_error);
			if (retval != DW_DLV_OK) goto fail;
			
			cls = spec.get_interp(attr, orig_form);
			switch(cls)
			{
				case spec::interp::string:
					dwarf_formstring(a.handle.get(), &str, &core::current_dwarf_error);
					this->f = STRING; 
					this->v_string = new string(str);
					break;
				case spec::interp::flag:
					dwarf_formflag(a.handle.get(), &flag, &core::current_dwarf_error);
					this->f = FLAG;
					this->v_flag = flag;
					break;
				case spec::interp::address:
					dwarf_formaddr(a.handle.get(), &addr, &core::current_dwarf_error);
					this->f = ADDR;
					this->v_addr.addr = addr;
					break;
				case spec::interp::block:
					{
						core::Block b(a);
						this->f = BLOCK;
						this->v_block = new vector<unsigned char>(
							(unsigned char *) b.handle->bl_data, 
							((unsigned char *) b.handle->bl_data) + b.handle->bl_len);
					}
					break;
				case spec::interp::reference: {
					this->f = REF;
					Dwarf_Off referencing_off = d.offset_here();
					Dwarf_Half referencing_attr = a.attr_here();
					int ret = dwarf_global_formref(a.handle.get(), &o, &core::current_dwarf_error);
					assert(ret == DW_DLV_OK);
					this->v_ref = new weak_ref(r, o, true, 
						referencing_off, referencing_attr);
					break;
				}
				as_if_unsigned:
				case spec::interp::constant:
					if (orig_form == DW_FORM_sdata
					 || orig_form == DW_FORM_data1
					 || orig_form == DW_FORM_data2
					 || orig_form == DW_FORM_data4
					 || orig_form == DW_FORM_data8
					 )
					{
						int ret = dwarf_formsdata(a.handle.get(), &s, &core::current_dwarf_error);
						assert(ret == DW_DLV_OK);
						this->f = SIGNED;
						this->v_s = s;					
					}
					else if (orig_form == DW_FORM_udata) // NOTE: there is no FORM_udata{1,2,4,9}
					{
						int ret = dwarf_formudata(a.handle.get(), &u, &core::current_dwarf_error);
						assert(ret == DW_DLV_OK);
						this->f = UNSIGNED;
						this->v_u = u;
					}
					else if (orig_form == DW_FORM_sec_offset)
						// TODO: need to handle ref{1,2,4,8,_udata} here?
					{
						Dwarf_Off ref;
						int ret = dwarf_global_formref(a.handle.get(), &ref, &core::current_dwarf_error); 
						assert(ret == DW_DLV_OK);
						u = ref;
						this->f = UNSIGNED;
						this->v_u = u;
					}
					else assert(false);
					break;
				case spec::interp::block_as_dwarf_expr: // dwarf_loclist_n works for both of these
				case spec::interp::loclistptr:
					try
					{
						this->f = LOCLIST;
						// replaced lib::loclist with core::LocdescList
						//this->v_loclist = new loclist(dwarf::lib::loclist(a, a.get_dbg()));
						core::LocdescList ll(core::LocdescList::try_construct(a)); // why not just ll(a)? 
						this->v_loclist = new loclist(ll);
						break;
					}
					catch (...)
					{
						/* This can happen if the loclist includes opcodes that our libdwarf
						 * doesn't recognise. Treat it as a not-supported case.*/
						goto fail;
					}
				case spec::interp::exprloc: { // like above, but simpler: use dwarf_formexprloc
					try
					{
						this->f = LOCLIST; 
						Dwarf_Unsigned exprlen;
						Dwarf_Ptr block_ptr;
						int ret = dwarf_formexprloc(a.handle.get(), &exprlen, &block_ptr, 
							&core::current_dwarf_error);
						assert(ret == DW_DLV_OK);
						this->v_loclist = new loclist(
							loc_expr(
								std::vector<expr_instr>(
									static_cast<Dwarf_Loc*>(block_ptr), // HMM
									static_cast<Dwarf_Loc*>(block_ptr) + exprlen 
								)
								
// 								vector<Dwarf_Locdesc>(1, 
// 									(Dwarf_Locdesc) {
// 										/* ld_lopc */ 0, /* see libdwarf2.1.pdf sec 2.3.2 */
// 										/* ld_hipc */ 0, 
// 										/* ld_cents */ exprlen, 
// 										/* ld_s */ (Dwarf_Loc*) block_ptr, // HMM
// 										/* ld_from_loclist */ 0, // FIXME
// 										/* ld_section_offset */ 0 // FIXME
// 									}
// 								)
							)
						);
					}
					catch (...)
					{
						/* This can happen if the loclist includes opcodes that our libdwarf
						 * doesn't recognise. Treat it as a not-supported case.*/
						goto fail;
					}
				}
				case spec::interp::rangelistptr: {
					this->f = RANGELIST;
					this->v_rangelist = new rangelist(core::RangeList(a, d));
				} break;
				case spec::interp::lineptr:
				case spec::interp::macptr:
					goto as_if_unsigned;
				fail:
				default:
					// FIXME: we failed to case-catch, or handle, the FORM; do something
					std::cerr << "FIXME: didn't know how to handle an attribute "
						<< "numbered 0x" << std::hex << attr << std::dec << " of form "
						<< /*ds.toplevel()->get_spec()*/spec.form_lookup(orig_form) 
						<< ", skipping." << std::endl;
					this->f = UNRECOG;
					//throw Not_supported("unrecognised attribute");
					/* NOTE: this Not_supportd doesn't happen in some cases, because often
					 * we have successfully guessed an interp:: class for the attribute
					 * anyway. FIXME: remember how this works, and see if we can do better. */
					//break;
			}
			
		}
		core::iterator_df<> attribute_value::get_refiter() const // { assert(f == REF); return v_ref->off; }
		{
			/* To make an iterator, we need
			 * - a root    \ normally these are in the weak_ref... just extend that?
			 * - an offset / 
			 * - (a depth, but we have to get that by searching in our case)
			 */
			assert(f == REF);
			assert(v_ref->p_root);
			return v_ref->p_root->find(v_ref->off);
			
			/* A possible solution: 
			 * - all DIEs have a reference to their enclosing compile unit DIE (sticky)
			 * - all compile unit DIEs have a "default root" 
			 *   -- this can be set by the user, but defaults to the physical root
			 * -- what does this achieve? 
			 * -- all functions which currently need a "root" argument still have one
			 *    ... but it can be defaulted.
			 *    QUESTION: how do we default it? opt<root_die&> p_root = opt<root_die&>()
			 * -- for get_<type> attribute accessors, what do we need to thread in?
			 *    -- the compile_unit_die would be cleanest, 
			 *       because it includes both spec and default root 
			 *       (but doesn't let us choose an alternative root!)
			 *    -- root_die would be clearest in the API design, but 
			 *       leaves the spec undecided. Maybe that's okay -- we've already
			 *       decided the spec when we *create* the attribute value? 
			 *    -- spec is not enough
			 *    -- AH but we've solved this already with weak_ref and ref. 
			 *       We need to update them to the "core" way of doing things. 
			 *    -- Summary: when creating an attribute value, pass a 
			 *       spec *and* a root? 
			 *       When getting an attribute value, no need to pass anything.
			 *    -- This means copy_attrs will need a root, as before. 
			 *       The root is obligatory because without it we can only get the CU offset,
			 *       not the actual CU iterator or DIE.
			 * 
			 * Regarding the problem with get_<>() now requiring a root_die& , 
			 * it might make things easier if every Die has a "constructing root"; 
			 * does this supplant the "default root" idea? 
			 * No because "constructing" is fixed, whereas we might want to set the
			 * default root on CUs that participate in a stacking dieset.
			 *
			 * We seem to have the worst of both worlds now -- we need a root_die
			 * *both* at every get_<>() *and* on constructing attribute_values. 
			 * Whereas we should only need one or the other (i.e. eagerly encapsulate
			 * the root on copy_attrs, or lazily pass it in when actually getting an
			 * attr. 
			 * But hmm, copy_attrs and get_<>() work differently -- 
			 * when we consume attrs we use either/or, not both. So I think we do 
			 * need both mechanisms. 
			 * 
			 * NOTE: as a first stage, I have implemented the optional_root_arg 
			 * trick, but not the in-CU "default root". Therefore, 
			 * all use of the defaulted argument will result in the constructing root
			 * being used. It's likely we'll want the other behaviour once
			 * stacking diesets are around, but it's unimplemented for now.
			 * */
			 
		}
		core::iterator_df<core::type_die> attribute_value::get_refiter_is_type() const // { assert(f == REF); return v_ref->off; }
		{
			return get_refiter();
		}
		
		attribute_map::attribute_map(const core::AttributeList& l, const core::Die& d, 
			root_die& r, spec::abstract_def& spec /* = 0 */)
		{
			for (auto i = l.copied_list.begin(); i != l.copied_list.end(); ++i)
			{
				this->insert(make_pair(i->attr_here(), attribute_value(*i, d, r)));
			}
		}
		std::ostream& operator<<(std::ostream& s, const attribute_map& m)
		{
			for (auto i = m.begin(); i != m.end(); ++i)
			{
				s << spec::DEFAULT_DWARF_SPEC.attr_lookup(i->first) 
					<< ": " << i->second << endl;
			}
			return s;
		}
		
		attribute_value::attribute_value(spec::abstract_dieset& ds, const dwarf::lib::attribute& a)
        	: p_ds(&ds)
		{
			int retval;
			orig_form = 0;
			retval = a.whatform(&orig_form);

			Dwarf_Unsigned u;
			Dwarf_Signed s;
			Dwarf_Bool flag;
			Dwarf_Off o;
			Dwarf_Addr addr;
			char *str;
			int cls = spec::interp::EOL; // dummy initialization
			if (retval != DW_DLV_OK) goto fail; // retval set by whatform() above
			Dwarf_Half attr; retval = a.whatattr(&attr);
			if (retval != DW_DLV_OK) goto fail;
			cls = ds.get_spec().get_interp(attr, orig_form);
						
			switch(cls)
			{
				case spec::interp::string:
					a.formstring(&str);
					this->f = STRING; 
					this->v_string = new std::string(str);
					break;
				case spec::interp::flag:
					a.formflag(&flag);
					this->f = FLAG;
					this->v_flag = flag;
					break;
				case spec::interp::address:
					a.formaddr(&addr);
					this->f = ADDR;
					this->v_addr.addr = addr;
					break;
				case spec::interp::block:
					{
						block b(a);
						this->f = BLOCK;
						this->v_block = new std::vector<unsigned char>(
		 					(unsigned char *) b.data(), ((unsigned char *) b.data()) + b.len());
					}
					break;
				case spec::interp::reference:
					this->f = REF;
					Dwarf_Off  referencing_off; 
					a.get_containing_array().get_containing_die().offset(&referencing_off);
					Dwarf_Half referencing_attr;
					a.whatattr(&referencing_attr);
					//if (orig_form == DW_FORM_ref_addr)
					//{
						a.formref_global(&o);
                        // o is a section-relative offset
                        // HACK: if ds is encap, create a strong ref, else weak
                        if (dynamic_cast<encap::dieset *>(p_ds) == 0)
                        {
							this->v_ref = new weak_ref(ds, o, true, referencing_off, referencing_attr);
                        }
                        else this->v_ref = new ref(ds, o, true, referencing_off, referencing_attr);
					//}
					//else
					//{
					//	a.formref(&o);
                    //    // o is a CU-relative offset
					//	this->v_ref = new ref(ds, o, false, referencing_off, referencing_attr);
					//}
					break;
				as_if_unsigned:
				case spec::interp::constant:
					if (orig_form == DW_FORM_sdata)
					{
						a.formsdata(&s);
						this->f = SIGNED;
						this->v_s = s;					
					}
					else
					{
						a.formudata(&u);
						this->f = UNSIGNED;
						this->v_u = u;
					}
					break;
				case spec::interp::block_as_dwarf_expr: // dwarf_loclist_n works for both of these
				case spec::interp::loclistptr:
					try
					{
						this->f = LOCLIST;
						this->v_loclist = new loclist(dwarf::lib::loclist(a));
						break;
					}
					catch (...)
					{
						/* This can happen if the loclist includes opcodes that our libdwarf
						 * doesn't recognise. Treat it as a not-supported case.*/
						goto fail;
					}
				case spec::interp::rangelistptr: {
					this->f = RANGELIST;
					switch (orig_form) 
					{
						case DW_FORM_udata:
							retval = a.formudata(&u); 
							assert(retval == DW_DLV_OK);
							break;
						case DW_FORM_data4: // FIXME: really?
							retval = a.formsdata(reinterpret_cast<Dwarf_Signed*>(&u));
							assert(retval == DW_DLV_OK);
							break;
						case DW_FORM_sec_offset: 
							retval = a.formref(static_cast<Dwarf_Off*>(&u)); 
							assert(retval == DW_DLV_OK);
							break;
						default:
							assert(false);
					}
					dwarf::lib::ranges rs(a, u);
					this->v_rangelist = new rangelist(rs.begin(), rs.end());
				} break;
				case spec::interp::lineptr:
				case spec::interp::macptr:
					goto as_if_unsigned;
				fail:
				default:
					// FIXME: we failed to case-catch, or handle, the FORM; do something
					std::cerr << "FIXME: didn't know how to handle an attribute "
						<< "numbered 0x" << std::hex << attr << std::dec << " of form "
						<< ds.toplevel()->get_spec().form_lookup(orig_form) 
						<< ", skipping." << std::endl;
					throw Not_supported("unrecognised attribute");
					/* NOTE: this Not_supportd doesn't happen in some cases, because often
					 * we have successfully guessed an interp:: class for the attribute
					 * anyway. FIXME: remember how this works, and see if we can do better. */
					break;
			}
		}
		
		/* Constructors we couldn't define inline for dependency reasons. */
		attribute_value::attribute_value(spec::abstract_dieset& ds, 
				std::shared_ptr<spec::basic_die> ref_target)
		 : p_ds(&ds), orig_form(DW_FORM_ref_addr), f(REF), 
		   v_ref(new weak_ref(ref_target->get_ds(), 
		        ref_target->get_offset(), false,
				/* HACK! */ std::numeric_limits<lib::Dwarf_Off>::max(),
						    std::numeric_limits<lib::Dwarf_Half>::max())) {}

		attribute_value::attribute_value(spec::abstract_dieset& ds, const encap::loclist& l) 
		: p_ds(&ds), orig_form(DW_FORM_data4), f(LOCLIST), v_loclist(new encap::loclist(l)) {}
		attribute_value::attribute_value(spec::abstract_dieset& ds, const encap::rangelist& l)
		 : p_ds(&ds), orig_form(DW_FORM_data4), f(RANGELIST), v_rangelist(new encap::rangelist(l)) {}

		attribute_value::attribute_value(const attribute_value& av) : p_ds(av.p_ds), f(av.f)
		{
			assert(this->p_ds == av.p_ds);
			this->orig_form = av.orig_form;
			switch (f)
			{
				case FLAG:
					v_flag = av.v_flag;
				break;
				case UNSIGNED:
					v_u = av.v_u;
				break;
				case SIGNED:
					v_s = av.v_s;
				break;
				case BLOCK:
					//std::cerr << "Copy constructing a block attribute value from vector at 0x" << std::hex << (unsigned) v_block << std::dec << std::endl;
					v_block = new std::vector<unsigned char>(*av.v_block);
					//std::cerr << "New block is at " << std::hex << (unsigned) v_block << std::dec << std::endl;						
				break;
				case STRING:
					//std::cerr << "Copy constructing a string attribute value from string at 0x" << std::hex << (unsigned) v_string << std::dec << std::endl;
					v_string = new std::string(*av.v_string);
					//std::cerr << "New string is at " << std::hex << (unsigned) v_string << std::dec << std::endl;
				break;
				case REF:
					v_ref = /*new ref(av.v_ref->ds, av.v_ref->off, av.v_ref->abs,
						av.v_ref->referencing_off, av.v_ref->referencing_attr);*/
						av.v_ref->clone();
				break;
				case ADDR:
					v_addr = av.v_addr;
				break;
				case LOCLIST:
					v_loclist = new loclist(*av.v_loclist);
				break;
				case RANGELIST:
					v_rangelist = new rangelist(*av.v_rangelist);
				break;
				case UNRECOG:
					std::cerr << "Warning: copy-constructing a dwarf::encap::attribute_value of unknown form " << f << std::endl;
				break; 
				default: 
					assert(false);
					break;
			} // end switch				
		}

		
		/* Ditto operators. */
		bool attribute_value::operator==(const attribute_value& v) const { 
			if (this->f != v.f) return false;
			// else this->f == v.f
			switch (f)
			{
				case NO_ATTR:
					return true;
				case FLAG:
					return this->v_flag == v.v_flag;
				case UNSIGNED:
					return this->v_u == v.v_u;
				case SIGNED:
					return this->v_s == v.v_s;
				case BLOCK:
					return this->v_block == v.v_block;
				case STRING:
					return *(this->v_string) == *(v.v_string);
				case REF:
					return this->v_ref == v.v_ref;
				case ADDR:
					return this->v_addr == v.v_addr;
				case LOCLIST:
					return *(this->v_loclist) == *(v.v_loclist);
                case RANGELIST:
                    return *(this->v_rangelist) == *(v.v_rangelist);
				default: 
					std::cerr << "Warning: comparing a dwarf::encap::attribute_value of unknown form " << v.f << std::endl;
					return false;
			} // end switch
		}
		attribute_value::~attribute_value() {
			switch (f)
			{
				case FLAG:
				case UNSIGNED:
				case SIGNED:
				case ADDR:
					// nothing allocated
				break;
				case BLOCK:
					//std::cerr << "Destructing a block attribute_value with vector at 0x" << std::hex << (unsigned) v_block << std::dec << std::endl;
					delete v_block;
				break;
				case STRING:
					delete v_string;
				break;
				case REF:
					delete v_ref;
				break;
				case LOCLIST:
					delete v_loclist;
				break;
				case RANGELIST:
					delete v_rangelist;
				break;
				default: break;
			} // end switch
			} // end ~attribute_value

		attribute_value::ref::ref(spec::abstract_dieset& ds, Dwarf_Off off, bool abs, 
			Dwarf_Off referencing_off, Dwarf_Half referencing_attr)	
				: weak_ref(ds, off, abs, referencing_off, referencing_attr), 
                  ds(dynamic_cast<encap::dieset&>(ds))
		{
			this->ds.backrefs()[off].push_back(
            	std::make_pair(referencing_off, referencing_attr));
		}

		attribute_value::weak_ref& 
        attribute_value::weak_ref::operator=(const attribute_value::weak_ref& r)
        {
        	assert(r.p_ds == this->p_ds);
			assert(r.p_root == this->p_root);
            this->off = r.off;
            this->abs = r.abs;
            this->referencing_off = r.referencing_off;
            this->referencing_attr = r.referencing_attr;
            return *this;
        }

        attribute_value::weak_ref::weak_ref(const attribute_value::weak_ref& r)
        {
        	this->p_ds = r.p_ds;
			this->p_root = r.p_root;
            *this = r;
        }

		attribute_value::ref& 
        attribute_value::ref::operator=(const attribute_value::weak_ref& r)
        {
        	// we destruct and then construct ourselves again
            this->~ref();
            new (this) ref(*r.p_ds, r.off, r.abs, r.referencing_off, r.referencing_attr);
            return *this;
        }
		attribute_value::ref::~ref()
		{
			/* Find the corresponding backreference entry (associated with 
			 * the target of this reference),
			 * and remove it if found.
			 * 
			 * Sometimes, off will not be found in backrefs.  How can this happen? 
			 * When destroying a dieset, the backrefs map is destroyed first.
			 * This simply removes each vector of <Half, Half> pairs in some order.
			 * Then the attribute values are destroyed. These are still trying to
			 * preserve backrefs integrity---but they can't, because backrefs has
			 * deallocated all its state. The backrefs object is still functional
			 * though, even though its destructor has completed... so we can
			 * hopefully test for presence of off. */
			if (!ds.is_destructing() && ds.backrefs().find(off) != ds.backrefs().end())
			{
 				dieset::backref_list::iterator found = std::find(
 					ds.backrefs()[off].begin(),
 					ds.backrefs()[off].end(),
 					std::make_pair(referencing_off, referencing_attr));
 				if (found != ds.backrefs()[off].end())
 				{
 					ds.backrefs()[off].erase(found);
 				}
			}
		}
		attribute_value::ref::ref(const ref& r) : weak_ref((assert(r.p_ds), *r.p_ds), r.off, r.abs,
        	r.referencing_off, r.referencing_attr), ds(dynamic_cast<encap::dieset&>(*r.p_ds))  // copy constructor
		{
			ds.backrefs()[off].push_back(std::make_pair(referencing_off, referencing_attr));
		}
		
		boost::optional<std::pair<Dwarf_Off, long int> >
		rangelist::find_addr(Dwarf_Off dieset_relative_address)
		{
			iterator found = this->end();
			Dwarf_Off offset = 0UL;
			iterator i;
			//Dwarf_Off current_cu_relative_base = 0UL; // FIXME: use this. 
			// FIXME: are the base addresses file-relative or CU-relative?
			
			long int dist_moved = 0;
			for (i = this->begin(); i != this->end(); ++dist_moved, ++i)
			{
				switch(i->dwr_type)
				{
					case DW_RANGES_ENTRY:
						//std::cerr << "Considering range " << *i << std::endl;
						if (dieset_relative_address >= i->dwr_addr1
							&& dieset_relative_address < i->dwr_addr2)
						{
							//std::cerr << "Matches..." << std::endl;
							found = i;
							offset += dieset_relative_address - i->dwr_addr1;
						}
						else if (i->dwr_addr2 <= dieset_relative_address)
						{
							//std::cerr << "Precedes." << std::endl;
							offset += i->dwr_addr2 - i->dwr_addr1;
						}
					break;
					case DW_RANGES_ADDRESS_SELECTION: {
						assert(i->dwr_addr1 == 0xffffffff || i->dwr_addr1 == 0xffffffffffffffffULL);
						//current_cu_relative_base = i->dwr_addr2 - ;
						assert(false);
					} break;
					case DW_RANGES_END: 
						assert(i->dwr_addr1 == 0);
						assert(i+1 == this->end()); 
						break;
					default: assert(false); break;
				}
			}
			if (found == this->end()) 
			{
				//std::cerr << "No match." << std::endl;
				return 0;
			}
			else return std::make_pair<dwarf::lib::Dwarf_Off, long int>(
				(Dwarf_Off) offset, 
				(long int) dist_moved
			);
		}
	}
}
