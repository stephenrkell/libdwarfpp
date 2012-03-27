/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * lib.cpp: basic C++ wrapping of libdwarf C API.
 *
 * Copyright (c) 2008--9, Stephen Kell.
 */

#include "dwarfpp/lib.hpp"
#include "dwarfpp/adt.hpp"
#include "dwarfpp/encap.hpp" // re-use some formatting logic in encap, for convenience
	// FIXME: flip the above around, so that the formatting logic is in here!

#include <sstream>
#include <libelf.h>

namespace dwarf
{
	namespace lib
	{
		void default_error_handler(Dwarf_Error e, Dwarf_Ptr errarg) 
		{ 
			//fprintf(stderr, "DWARF error!\n"); /* this is the C version */
			/* NOTE: when called by a libdwarf function,
			 * errarg is always the Dwarf_Debug concerned with the error */
        	std::cerr << "libdwarf error!" << std::endl;
			throw Error(e, errarg); /* Whoever catches this should also dwarf_dealloc the Dwarf_Error_s. */
		}

		file::file(int fd, Dwarf_Unsigned access /*= DW_DLC_READ*/,
			Dwarf_Ptr errarg /*= 0*/,
			Dwarf_Handler errhand /*= default_error_handler*/,
			Dwarf_Error *error /*= 0*/)
		{
    		if (error == 0) error = &last_error;
			if (errarg == 0) errarg = this;
			int retval;
			retval = dwarf_init(fd, access, errhand, errarg, &dbg, error);
			have_cu_context = false;
            switch (retval)
            {
            case DW_DLV_ERROR:
            	/* From libdwarf docs:
                 * "An Dwarf_Error returned from dwarf_init() or dwarf_elf_init() 
                 * in case of a failure cannot be freed using dwarf_dealloc(). 
                 * The only way to free the Dwarf_Error from either of those calls
                 * is to use free(3) directly. Every Dwarf_Error must be freed by 
                 * dwarf_dealloc() except those returned by dwarf_init() or 
                 * dwarf_elf_init()." 
                 * 
                 * This means we shouldn't pass the Dwarf_Error through the 
                 * exception that our error handler generates, because the
                 * catching code will try to dwarf_dealloc() it when it should
                 * instead free() it. HACK: for now, free() it here.
                 * ANOTHER HACK: no, don't, because this is causing problems
                 * in libprocessimage. */
                /* free(error);*/
            	default_error_handler(NULL, errarg); // throws
                break;
            case DW_DLV_NO_ENTRY:
            	throw No_entry();
            default:
            	break; // fall through
            }
            free_elf = false;
            elf = 0;
            try
            {
            	this->p_aranges = new aranges(*this);
            }
            catch (No_entry)
            {
            	// the file must not have a .debug_aranges section
                this->p_aranges = 0;
            }
		}

		file::~file()
		{
			//Elf *elf; // maybe this Elf business is only for dwarf_elf_open?
			//int retval;
			//retval = dwarf_get_elf(dbg, &elf, &last_error);
			if (dbg != 0) dwarf_finish(dbg, &last_error);
			if (free_elf) elf_end(reinterpret_cast< ::Elf*>(elf));
            if (p_aranges) delete p_aranges;
		}

    	int file::next_cu_header(
	    	Dwarf_Unsigned *cu_header_length,
	    	Dwarf_Half *version_stamp,
	    	Dwarf_Unsigned *abbrev_offset,
	    	Dwarf_Half *address_size,
	    	Dwarf_Unsigned *next_cu_header,
	    	Dwarf_Error *error /*=0*/)
    	{
	    	if (error == 0) error = &last_error;
			int retval = dwarf_next_cu_header(dbg, cu_header_length, version_stamp,
        			abbrev_offset, address_size, next_cu_header, error); // may allocate **error
			have_cu_context = true;
			return retval;
    	}

		int file::clear_cu_context(cu_callback_t cb, void *arg)
		{
			if (!have_cu_context) return DW_DLV_OK;
		
			int retval;
			Dwarf_Unsigned cu_header_length;
			Dwarf_Half version_stamp;
			Dwarf_Unsigned abbrev_offset;
			Dwarf_Half address_size;
			Dwarf_Unsigned next_cu_header;
			//std::cerr << "Resetting CU state..." << std::endl;
			while(DW_DLV_OK == (retval = advance_cu_context(
				&cu_header_length, &version_stamp, 
				&abbrev_offset, &address_size, 
				&next_cu_header,
				cb, arg)));

			have_cu_context = false;
			//std::cerr << "next_cu_header returned " << retval << std::endl;
			if (retval == DW_DLV_NO_ENTRY)
			{
				/* This is okay -- means we iterated to the end of the CUs
				 * and are now back in the beginning state, which is what we want. */
				return DW_DLV_OK;
			}
			else return retval;
		}
		
		int file::advance_cu_context(Dwarf_Unsigned *cu_header_length,
				Dwarf_Half *version_stamp,
				Dwarf_Unsigned *abbrev_offset,
				Dwarf_Half *address_size, 
				Dwarf_Unsigned *next_cu_header,
				cu_callback_t cb, void *arg)
		{
			/* All the output parameters are optional. 
			 * BUT we *always* call the callback with the full set! 
			 * So we need to dummy the pointers. */
			Dwarf_Unsigned dummy_cu_header_length,
			 *real_cu_header_length = cu_header_length ? cu_header_length : &dummy_cu_header_length;
			
			Dwarf_Half dummy_version_stamp,
			 *real_version_stamp = version_stamp ? version_stamp : &dummy_version_stamp;
			
			Dwarf_Unsigned dummy_abbrev_offset,
			 *real_abbrev_offset = abbrev_offset ? abbrev_offset : &dummy_abbrev_offset;
			
			Dwarf_Half dummy_address_size,
			 *real_address_size = address_size ? address_size : &dummy_address_size;
			
			Dwarf_Unsigned dummy_next_cu_header,
			 *real_next_cu_header = next_cu_header ? next_cu_header : &dummy_next_cu_header;
			
			int retval = this->next_cu_header(
				real_cu_header_length, real_version_stamp, 
				real_abbrev_offset, real_address_size, 
				real_next_cu_header);
			if (retval == DW_DLV_OK)
			{
				have_cu_context = true;
				//std::cerr << "next_cu_header returned DW_DLV_OK" << std::endl;
				die d(*this); // get the CU die
				Dwarf_Off off; 
				int retval = d.offset(&off); // this should not fail
				assert(retval == DW_DLV_OK);
				if (cb) cb(arg, off, 
					*real_cu_header_length, *real_version_stamp,
					*real_abbrev_offset, *real_address_size, *real_next_cu_header);
			}
			else if (retval == DW_DLV_NO_ENTRY) have_cu_context = false;
			
			if (cu_header_length) *cu_header_length = *real_cu_header_length;
			if (version_stamp) *version_stamp = *real_version_stamp;
			if (abbrev_offset) *abbrev_offset = *real_abbrev_offset;
			if (address_size) *address_size = *real_address_size;
			if (next_cu_header) *next_cu_header = *real_next_cu_header;
			
			return retval;
		}


		int file::get_elf(Elf **out_elf, Dwarf_Error *error /*= 0*/)        	
        { 
        	if (elf != 0) { *out_elf = elf; return DW_DLV_OK; }
        	if (error == 0) error = &last_error;
            int retval = dwarf_get_elf(dbg, 
            	reinterpret_cast<dwarf::lib::dwarf_elf_handle*>(&elf), 
                error);
            *out_elf = elf;
            return retval;
        }

		int file::siblingof(
			die& d,
			die *return_sib,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = &last_error;
			return dwarf_siblingof(dbg,
				d.my_die,
				&(return_sib->my_die),
				error); // may allocate **error, allocates **return_sib? 
		}

		int file::first_die(
			die *return_die,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = &last_error;
			ensure_cu_context();
			return dwarf_siblingof(dbg,
				NULL,
				&(return_die->my_die),
				error); // may allocate **error, allocates **return_sib? 
		} // special case of siblingof

		int file::offdie(
			Dwarf_Off offset,
			die *return_die,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = &last_error;
			return dwarf_offdie(dbg,
				offset,
				&(return_die->my_die),
				error); // may allocate **error, allocates **return_die?
		}

	// 	int file::get_globals(
	// 		global_array *& globarr, // the client passes 
	// 		Dwarf_Error *error /*= 0*/)
	// 	{
	// 		if (error == 0) error = &last_error;
	// 		Dwarf_Signed cnt;
	// 		Dwarf_Global *globals;
	// 		int retval = dwarf_get_globals(
	// 			dbg, &globals, &cnt, error
	// 		);
	// 		assert(retval == DW_DLV_OK);
	// 		globarr = new global_array(dbg, globals, cnt);
	// 		return retval;
	// 	} // allocates globarr, else allocates **error

		int file::get_cu_die_offset_given_cu_header_offset(
			Dwarf_Off in_cu_header_offset,
			Dwarf_Off * out_cu_die_offset,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = &last_error;
			return DW_DLV_ERROR; // TODO: implement this
		}
		
		//file die::dummy_file; // static, should be const (FIXME)

		die::die(file& f, Dwarf_Die d, Dwarf_Error *perror) : f(f), p_last_error(perror)
		{
			this->my_die = d;
		}
		die::~die()
		{
			dwarf_dealloc(f.dbg, my_die, DW_DLA_DIE);
		}

		int die::first_child(
			die *return_kid,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			/* If we have a null my_die, it means we are a file_toplevel_die
			 * and are being asked for the first CU. */
			if (!my_die)
			{
				f.clear_cu_context();
				int retval = f.advance_cu_context();
				if (retval == DW_DLV_NO_ENTRY) throw No_entry();
				// now at first CU
				retval = f.first_die(return_kid, error);
				assert(retval != DW_DLV_NO_ENTRY); // CU header found implies CU DIE found
				return retval;
			}
			else return dwarf_child(my_die, &(return_kid->my_die), error);
		} // may allocate **error, allocates *(return_kid->my_die) on return

		int die::tag(
			Dwarf_Half *tagval,
			Dwarf_Error *error /*= 0*/) const
		{
			if (error == 0) error = p_last_error;
			return dwarf_tag(my_die, tagval, error);
		} // may allocate **error

		int die::offset(
			Dwarf_Off * return_offset,
			Dwarf_Error *error /*= 0*/) const
		{
			if (error == 0) error = p_last_error;
			return dwarf_dieoffset(my_die, return_offset, error);
		} // may allocate **error

		int die::CU_offset(
			Dwarf_Off *return_offset,
			Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_die_CU_offset(my_die, return_offset, error);
		} // may allocate **error

		int die::name(
			std::string *return_name,
			Dwarf_Error *error /*= 0*/) const
		{
			if (error == 0) error = p_last_error;
			char *returned_name_chars;
			int retval = dwarf_diename(my_die, &returned_name_chars, error);
			if (retval == DW_DLV_OK)
			{
				*return_name = returned_name_chars; // HACK: copying string is not okay,
				 // but we are undertaking to provide a RAII inerface here
				 // -- arguably we should provide a class dwarf::lib::diename
				dwarf_dealloc(f.dbg, returned_name_chars, DW_DLA_STRING);
			}
			//std::cerr << "Retval from dwarf_diename is " << retval << std::endl;
			return retval;
		} // may allocate **name, else may allocate **error

	// 	int die::attrlist(
	// 		attribute_array *& attrbuf, // on return, attrbuf points to an attribute_array
	// 		Dwarf_Error *error /*= 0*/)
	// 	{
	// 		if (error == 0) error = p_last_error;
	// 		Dwarf_Signed cnt;
	// 		Dwarf_Attribute *attrs;
	// 		int retval = dwarf_attrlist(
	// 			my_die, &attrs, &cnt, error
	// 		);
	// 		assert(retval == DW_DLV_OK);
	// 		attrbuf = new attribute_array(f.get_dbg(), attrs, cnt);
	// 		return retval;
	// 	} // allocates attrbuf; else may allocate **error
	// 	/* TODO: delete this, now we have an attribute_array instead */

		int die::hasattr(Dwarf_Half attr, Dwarf_Bool *return_bool, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_hasattr(my_die, attr, return_bool, error);		
		} // may allocate **error

	// 	int die::attr(Dwarf_Half attr, attribute *return_attr, Dwarf_Error *error /*= 0*/)
	// 	{
	// 		if (error == 0) error = p_last_error;
	// 		Dwarf_Attribute tmp_attr;
	// 		int retval = dwarf_attr(my_die, attr, &tmp_attr, error);
	// 		assert(retval == DW_DLV_OK);
	// 		return_attr = new attribute(tmp_attr);
	// 		return retval;
	// 	} // allocates *return_attr
	// TODO: document this: we only support getting *all* attributes, not individual ones

		int die::lowpc(Dwarf_Addr * return_lowpc, Dwarf_Error * error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_lowpc(my_die, return_lowpc, error);		
		} // may allocate **error

		int die::highpc(Dwarf_Addr * return_highpc, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_highpc(my_die, return_highpc, error);
		} // may allocate **error

		int die::bytesize(Dwarf_Unsigned *return_size, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_bytesize(my_die, return_size, error);
		} // may allocate **error

		int die::bitsize(Dwarf_Unsigned *return_size, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_bitsize(my_die, return_size, error);
		} // may allocate **error

		int die::bitoffset(Dwarf_Unsigned *return_size, Dwarf_Error *error /*= 0*/)	
		{
			if (error == 0) error = p_last_error;
			return dwarf_bitoffset(my_die, return_size, error);
		} // may allocate **error

		int die::srclang(Dwarf_Unsigned *return_lang, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_srclang(my_die, return_lang, error);		
		}

		int die::arrayorder(Dwarf_Unsigned *return_order, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = p_last_error;
			return dwarf_arrayorder(my_die, return_order, error);
		}

		/*
		 * methods defined on global
		 */
		int global::get_name(char **return_name, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;
			return dwarf_globname(a.p_globals[i], return_name, error);		
		}	// TODO: string destructor

		int global::get_die_offset(Dwarf_Off *return_offset, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;
			return dwarf_global_die_offset(a.p_globals[i], return_offset, error);
		}	
		int global::get_cu_offset(Dwarf_Off *return_offset, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;	
			return dwarf_global_cu_offset(a.p_globals[i], return_offset, error);
		}
		int global::cu_die_offset_given_cu_header_offset(Dwarf_Off in_cu_header_offset,
			Dwarf_Off *out_cu_die_offset, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;	
			return dwarf_get_cu_die_offset_given_cu_header_offset(a.f.get_dbg(),
				in_cu_header_offset, out_cu_die_offset, error);
		}
		int global::name_offsets(char **return_name, Dwarf_Off *die_offset, 
			Dwarf_Off *cu_offset, Dwarf_Error *error /*= 0*/)
		{
			if (error == 0) error = a.p_last_error;
			return dwarf_global_name_offsets(a.p_globals[i], return_name,
				die_offset, cu_offset, error);
		}

		attribute::attribute(Dwarf_Half attr, attribute_array& a, Dwarf_Error *error /*= 0*/)
         : a(a)
        {
			if (error == 0) error = a.p_last_error;
            
			Dwarf_Bool ret = false; 
            if (!(a.d.hasattr(attr, &ret), ret)) throw No_entry(); 
            
            for (int i = 0; i < a.cnt; i++)
            {
                Dwarf_Half out;
                if ((dwarf_whatattr(a.p_attrs[i], &out, error), out) == attr)
                {
                	this->i = i;
                    return;
                }
            }
            assert(false); // shouldn't happen, because we checked
        }

		/*
		 * methods defined on attribute
		 */
		int attribute::hasform(Dwarf_Half form, Dwarf_Bool *return_hasform,
			Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = a.p_last_error;
			return dwarf_hasform(a.p_attrs[i], form, return_hasform, error);
		}
		int attribute::whatform(Dwarf_Half *return_form, Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = a.p_last_error;
			return dwarf_whatform(a.p_attrs[i], return_form, error);
		}
		int attribute::whatform_direct(Dwarf_Half *return_form,	Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = a.p_last_error;
			return dwarf_whatform_direct(a.p_attrs[i], return_form, error);
		}
		int attribute::whatattr(Dwarf_Half *return_attr, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = a.p_last_error;
			return dwarf_whatattr(a.p_attrs[i], return_attr, error);
		}
		int attribute::formref(Dwarf_Off *return_offset, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = a.p_last_error;
			return dwarf_formref(a.p_attrs[i], return_offset, error);
		}
		int attribute::formref_global(Dwarf_Off *return_offset, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = a.p_last_error;
			return dwarf_global_formref(a.p_attrs[i], return_offset, error);
		}
		int attribute::formaddr(Dwarf_Addr * return_addr, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = a.p_last_error;
			return dwarf_formaddr(a.p_attrs[i], return_addr, error);
		}
		int attribute::formflag(Dwarf_Bool * return_bool, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = a.p_last_error;
			return dwarf_formflag(a.p_attrs[i], return_bool, error);
		}
		int attribute::formudata(Dwarf_Unsigned * return_uvalue, Dwarf_Error * error /*=0*/) const
		{		
			if (error == 0) error = a.p_last_error;
			return dwarf_formudata(a.p_attrs[i], return_uvalue, error);
		}
		int attribute::formsdata(Dwarf_Signed * return_svalue, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = a.p_last_error;
			return dwarf_formsdata(a.p_attrs[i], return_svalue, error);
		}
		int attribute::formblock(Dwarf_Block ** return_block, Dwarf_Error * error /*=0*/) const
		{		
			if (error == 0) error = a.p_last_error; // TODO: fix this to be RAII
			return dwarf_formblock(a.p_attrs[i], return_block, error);
		}
		int attribute::formstring(char ** return_string, Dwarf_Error *error /*=0*/) const
		{		
			if (error == 0) error = a.p_last_error; // TODO: fix this to be RAII
			return dwarf_formstring(a.p_attrs[i], return_string, error);
		}
		int attribute::loclist_n(Dwarf_Locdesc ***llbuf, Dwarf_Signed *listlen, Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = a.p_last_error; // TODO: fix this to be RAII
			return dwarf_loclist_n(a.p_attrs[i], llbuf, listlen, error);
		}
		int attribute::loclist(Dwarf_Locdesc **llbuf, Dwarf_Signed *listlen, Dwarf_Error *error /*=0*/) const
		{
			if (error == 0) error = a.p_last_error; // TODO: fix this to be RAII
			return dwarf_loclist(a.p_attrs[i], llbuf, listlen, error);
		}
        
        /* methods defined on aranges */
     	int aranges::get_info(int i, Dwarf_Addr *start, Dwarf_Unsigned *length, Dwarf_Off *cu_die_offset,
				Dwarf_Error *error/* = 0*/)
        {
        	if (error == 0) error = p_last_error; // TODO: fix
            if (i >= cnt) throw No_entry();
            return dwarf_get_arange_info(p_aranges[i], start, length, cu_die_offset, error);
        }
		
		std::ostream& operator<<(std::ostream& s, const Dwarf_Locdesc& ld)
		{
			s << dwarf::encap::loc_expr(ld);
			return s;
		}	
		std::ostream& operator<<(std::ostream& s, const Dwarf_Loc& l)
		{
        	// HACK: we can't infer the DWARF standard from the Dwarf_Loc we've been passed,
            // so use the default.
			s << "0x" << std::hex << l.lr_offset << std::dec
				<< ": " << dwarf::spec::DEFAULT_DWARF_SPEC.op_lookup(l.lr_atom);
			std::ostringstream buf;
			std::string to_append;
           
			switch (dwarf::spec::DEFAULT_DWARF_SPEC.op_operand_count(l.lr_atom))
			{
				case 2:
					buf << ", " << dwarf::encap::attribute_value(
						l.lr_number2, 
						dwarf::spec::DEFAULT_DWARF_SPEC.op_operand_form_list(l.lr_atom)[1]
					);
					to_append += buf.str();
				case 1:
					buf.clear();
					buf << "(" << dwarf::encap::attribute_value(
						l.lr_number, 
						dwarf::spec::DEFAULT_DWARF_SPEC.op_operand_form_list(l.lr_atom)[0]
					);
					to_append.insert(0, buf.str());
					to_append += ")";
				case 0:
					s << to_append;
					break;
				default: s << "(unexpected number of operands) ";
			}
			s << ";";
			return s;			
		}
		std::ostream& operator<<(std::ostream& s, const loclist& ll)
		{
			s << "(loclist) {";
			for (int i = 0; i < ll.len(); i++)
			{
				if (i > 0) s << ", ";
				s << ll[i];
			}
			s << "}";
			return s;
		}
		
		void evaluator::eval()
		{
			//std::vector<Dwarf_Loc>::iterator i = expr.begin();
            if (i != expr.end() && i != expr.begin())
            {
            	/* This happens when we stopped at a DW_OP_piece argument. 
                 * Advance the opcode iterator and clear the stack. */
                ++i;
                while (!m_stack.empty()) m_stack.pop();
			}
            boost::optional<std::string> error_detail;
			while (i != expr.end())
			{
				switch(i->lr_atom)
				{
					case DW_OP_constu:
						m_stack.push(i->lr_number);
						break;
                    case DW_OP_plus_uconst: {
                    	int tos = m_stack.top();
                        m_stack.pop();
                        m_stack.push(tos + i->lr_number);
                    } break;
                    case DW_OP_fbreg: {
                    	if (!frame_base) goto logic_error;
                        m_stack.push(*frame_base + i->lr_number);
                    } break;
                    case DW_OP_piece: {
                    	/* Here we do something special: leave the opcode iterator
                         * pointing at the piece argument, and return. This allow us
                         * to probe the piece size (by getting *i) and to resume by
                         * calling eval() again. */
                         ++i;
                    }    return;
                    case DW_OP_breg0:
                    case DW_OP_breg1:
                    case DW_OP_breg2:
                    case DW_OP_breg3:
                    case DW_OP_breg4:
                    case DW_OP_breg5:
                    case DW_OP_breg6:
                    case DW_OP_breg7:
                    case DW_OP_breg8:
                    case DW_OP_breg9:
                    case DW_OP_breg10:
                    case DW_OP_breg11:
                    case DW_OP_breg12:
                    case DW_OP_breg13:
                    case DW_OP_breg14:
                    case DW_OP_breg15:
                    case DW_OP_breg16:
                    case DW_OP_breg17:
                    case DW_OP_breg18:
                    case DW_OP_breg19:
                    case DW_OP_breg20:
                    case DW_OP_breg21:
                    case DW_OP_breg22:
                    case DW_OP_breg23:
                    case DW_OP_breg24:
                    case DW_OP_breg25:
                    case DW_OP_breg26:
                    case DW_OP_breg27:
                    case DW_OP_breg28:
                    case DW_OP_breg29:
                    case DW_OP_breg30:
                    case DW_OP_breg31:
                    {
						/* the breg family get the contents of a register and add an offset */ 
                    	if (!p_regs) goto logic_error;
                    	int regnum = i->lr_atom - DW_OP_breg0;
                        m_stack.push(p_regs->get(regnum) + i->lr_number);
                    } break;
                    case DW_OP_addr:
                    {
                    	m_stack.push(i->lr_number);
                    } break;
					case DW_OP_reg0:
					case DW_OP_reg1:
					case DW_OP_reg2:
					case DW_OP_reg3:
					case DW_OP_reg4:
					case DW_OP_reg5:
					case DW_OP_reg6:
					case DW_OP_reg7:
					case DW_OP_reg8:
					case DW_OP_reg9:
					case DW_OP_reg10:
					case DW_OP_reg11:
					case DW_OP_reg12:
					case DW_OP_reg13:
					case DW_OP_reg14:
					case DW_OP_reg15:
					case DW_OP_reg16:
					case DW_OP_reg17:
					case DW_OP_reg18:
					case DW_OP_reg19:
					case DW_OP_reg20:
					case DW_OP_reg21:
					case DW_OP_reg22:
					case DW_OP_reg23:
					case DW_OP_reg24:
					case DW_OP_reg25:
					case DW_OP_reg26:
					case DW_OP_reg27:
					case DW_OP_reg28:
					case DW_OP_reg29:
					case DW_OP_reg30:
					case DW_OP_reg31:
					{
						/* the reg family just get the contents of the register */
						if (!p_regs) goto logic_error;
						int regnum = i->lr_atom - DW_OP_reg0;
						m_stack.push(p_regs->get(regnum));
					} break;
					
					default:
						std::cerr << "Error: unrecognised opcode: " << spec.op_lookup(i->lr_atom) << std::endl;
						assert(false);
                    logic_error:
                    	std::cerr << "Logic error in DWARF expression evaluator";
                        if (error_detail) std::cerr << ": " << *error_detail;
                        std::cerr << std::endl;
                        assert(false);
                        throw Not_supported(error_detail ? *error_detail : "unknown");
				}
			i++;
			}
		}
        Dwarf_Unsigned eval(const encap::loclist& loclist,
        	Dwarf_Addr vaddr,
            Dwarf_Signed frame_base,
            boost::optional<regs&> regs,
	        const ::dwarf::spec::abstract_def& spec,
            const std::stack<Dwarf_Unsigned>& initial_stack)
        {
        	assert(false); return 0UL;
        }
		
		bool operator==(const /*dwarf::encap::expr_instr*/Dwarf_Loc& e1, 
			const /*dwarf::encap::expr_instr*/Dwarf_Loc& e2)
		{
			return e1.lr_atom == e2.lr_atom
				&& e1.lr_number == e2.lr_number
				&& e1.lr_number2 == e2.lr_number2
				&& e1.lr_offset == e2.lr_offset;
		}
		bool operator!=(const /*dwarf::encap::expr_instr*/Dwarf_Loc& e1,
			const /*dwarf::encap::expr_instr*/Dwarf_Loc& e2)
		{
			return !(e1 == e2);
		}
		bool operator==(const /*dwarf::encap::expr_instr*/Dwarf_Ranges& e1, 
			const /*dwarf::encap::expr_instr*/Dwarf_Ranges& e2)
		{
			return e1.dwr_addr1 == e2.dwr_addr1
				&& e1.dwr_addr2 == e2.dwr_addr2
				&& e1.dwr_type == e2.dwr_type;
		}
		bool operator!=(const /*dwarf::encap::expr_instr*/Dwarf_Ranges& e1,
			const /*dwarf::encap::expr_instr*/Dwarf_Ranges& e2)
		{
			return !(e1 == e2);
		}

	} // end namespace lib
} // end namespace dwarf
