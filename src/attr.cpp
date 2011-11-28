#include "attr.hpp"
#include "encap.hpp"
#include "spec_adt.hpp" /* for basic_die methods */

namespace dwarf
{
	namespace encap
    {
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
				
				default: 
					s << "FIXME! (not present)";
					break;
			
			}			
		} // end attribute_value::print	

		const attribute_value::form attribute_value::dwarf_form_to_form(const Dwarf_Half form)
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
					assert(false);
					return dwarf::encap::attribute_value::NO_ATTR;
			}	
		}
	
    	boost::shared_ptr<spec::basic_die> attribute_value::get_refdie() const
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
            for (auto i_r = rl.begin(); i_r != rl.end(); i_r++)
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
		attribute_value::attribute_value(spec::abstract_dieset& ds, const dwarf::lib::attribute& a)
        	: p_ds(&ds)
		{
			int retval;
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
					this->f = LOCLIST;
					this->v_loclist = new loclist(dwarf::lib::loclist(a));
					break;
				case spec::interp::rangelistptr: {
                	this->f = RANGELIST;
                    retval = a.formudata(&u); assert(retval == DW_DLV_OK);
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
					throw Not_supported("unrecognosed attribute");
					/* NOTE: this Not_supportd doesn't happen in some cases, because often
					 * we have successfully guessed an interp:: class for the attribute
					 * anyway. FIXME: remember how this works, and see if we can do better. */
					break;
			}
		}
 		
		attribute_value::attribute_value(spec::abstract_dieset& ds, 
				boost::shared_ptr<spec::basic_die> ref_target)
		 : p_ds(&ds), orig_form(DW_FORM_ref_addr), f(REF), 
		   v_ref(new weak_ref(ref_target->get_ds(), 
		        ref_target->get_offset(), false,
				/* HACK! */ std::numeric_limits<lib::Dwarf_Off>::max(),
						    std::numeric_limits<lib::Dwarf_Half>::max())) {}
		
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
            this->off = r.off;
            this->abs = r.abs;
            this->referencing_off = r.referencing_off;
            this->referencing_attr = r.referencing_attr;
            return *this;
        }

        attribute_value::weak_ref::weak_ref(const attribute_value::weak_ref& r)
        {
        	this->p_ds = r.p_ds;
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
			if (ds.backrefs().find(off) != ds.backrefs().end())
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
		
// 		//const attribute_value *attribute_value::dne_val;
//         boost::optional<std::pair<Dwarf_Off, int> >
//         arangelist::find_addr(Dwarf_Off cu_relative_address)
//         {
//             iterator found = this->end();
//             Dwarf_Off offset = 0UL;
//             iterator i;
//             for (i = this->begin(); i != this->end(); i++)
//             {
//                 std::cerr << "Considering arange beginning at " << std::hex << i->start
//                     << ", length " << i->length << std::endl;
//                 if (cu_relative_address >= i->start
//                     && cu_relative_address < i->start + i->length)
//                 {
//                     std::cerr << "Matches..." << std::endl;
//                     found = i;
//                     offset += cu_relative_address - i->start;
//                 }
//                 else if (i->start < cu_relative_address)
//                 {
//                     std::cerr << "Precedes." << std::endl;
//                     offset += i->length;
//                 }
//             }
//             if (found == this->end()) 
//             {
//                 std::cerr << "No match." << std::endl;
// 	            return 0;
//             }
//             else return std::make_pair(offset, i - this->begin());
//         }
        
        boost::optional<std::pair<Dwarf_Off, long int> >
        rangelist::find_addr(Dwarf_Off dieset_relative_address)
        {
            iterator found = this->end();
            Dwarf_Off offset = 0UL;
            iterator i;
            //Dwarf_Off current_cu_relative_base = 0UL; // FIXME: use this. 
            // FIXME: are the base addresses file-relative or CU-relative?
            
            for (i = this->begin(); i != this->end(); i++)
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
            else return std::make_pair(offset, i - this->begin());
        }


    }
}
