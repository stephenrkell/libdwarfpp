/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * encap.cpp: transparently-allocated, mutable representations 
 *			of libdwarf-like structures.
 *
 * Copyright (c) 2008--11, Stephen Kell.
 */

#include "encap.hpp"
/* #include "encap_adt.hpp" */
#include <iostream>
#include <limits>
#include <algorithm>
#include <new>
#include <srk31/algorithm.hpp>
#include <srk31/indenting_ostream.hpp>
#include <gelf.h>

using std::cerr;
using std::endl;
using std::make_pair;
using boost::dynamic_pointer_cast;
using boost::optional;
using boost::shared_ptr;
using srk31::count;

namespace dwarf
{
	namespace encap
	{
		using namespace ::dwarf::lib;

		void dieset::create_toplevel_entry()
		{
			// create a fake toplevel parent die
			shared_ptr<die> spd(new file_toplevel_die(*this));
			// HACK: can't use make_shared because this is a protected constructor

			this->insert(
				make_pair(
					0UL, 
					spd
				)
			);
		}
		
	   	file::file(int fd, Dwarf_Unsigned access /*= DW_DLC_READ*/) 
				: dwarf::lib::file(fd, access, 
				/*errarg = */0, /*errhand = */dwarf::lib::default_error_handler, 
				/*error =*/ 0)
		{
			// We have to explicitly loop through CU headers, 
			// to set the CU context when getting dies.
			Dwarf_Unsigned cu_header_length;
			Dwarf_Half version_stamp;
			Dwarf_Unsigned abbrev_offset;
			Dwarf_Half address_size;
			Dwarf_Unsigned next_cu_header;

			int retval;
			while(DW_DLV_OK == this->next_cu_header(&cu_header_length, &version_stamp,
				&abbrev_offset, &address_size, &next_cu_header));

			// we terminated the above loop with either DW_DLV_NO_ENTRY or DW_DLV_ERROR.
			// if the latter, the next call should return an error as well.
			// if the former, that's good; the next call should be okay
			retval = this->next_cu_header(&cu_header_length, &version_stamp,
				&abbrev_offset, &address_size, &next_cu_header);
			if (retval != DW_DLV_OK)
			{
				cerr << "Warning: couldn't get first CU header! no debug information imported" << endl;
			}
			else
			{
				int old_version_stamp = -1;
				//m_ds.p_spec = 0;
				for (// already loaded the first CU header!
						;
						retval != DW_DLV_NO_ENTRY; // termination condition (negated)
						retval = this->next_cu_header(&cu_header_length, &version_stamp,
							&abbrev_offset, &address_size, &next_cu_header))
				{
 					dwarf::lib::die first(*this); // this is the CU header (really! "first" is relative to CU context)

					/* We process the version stamp and address_size here.
					 * (libdwarf may abstract the address_size for us?)
					 *
					 * For the version stamp, we remember the version stamps of
					 * each CU, and flag an error if we can't deal with them.
					 */

					// FIXME: understand why DWARF 3 has version stamp 2, then clean this up
					assert(m_ds.p_spec); // we now always initialize this to DEFAULT_DWARF_SPEC
					if (/*m_ds.p_spec == 0 ||*/ m_ds.p_spec == &spec::DEFAULT_DWARF_SPEC)
					{	// HACK: now we only use DEFAULT_DWARF_SPEC, we can't distinguish
						// "no spec seen yet" from "dwarf3 seen". Sort this out! 
						// Probably it's easiest to have two copies of the dwarf3 spec....
						switch (version_stamp)
						{
							// FIXME: more here
							case 2: m_ds.p_spec = &dwarf::spec::dwarf3; break;
							default: throw std::string("Unsupported DWARF version stamp!");
						}
					}
					
					if (old_version_stamp != -1 && 
						version_stamp != old_version_stamp) throw std::string(
						"Can't support differing DWARF versions in the same file, for now.");
					/* We *could*, but note that references can cross CUs... we'd have to ensure
					 * that when following such a reference, we switch our spec accordingly. 
					 * This will take some careful code inspection. */
					 
					encapsulate_die(first, /* parent = */0UL); // this *doesn't* explore siblings of CU header DIEs!

					old_version_stamp = version_stamp;
				} // end for each CU
			}
			
			// record the last monotonic offset
			m_ds.last_monotonic_offset = (--m_ds.map_end())->first;
			
			// check referential integrity of dieset
			this->get_ds().all_compile_units()->integrity_check();
			
			// add aranges
//			 int arange_idx = 0;
//			 try
//			 {
// 				this->get_ds().m_aranges.push_back(
//				 	encap::arangelist(this->get_aranges(), arange_idx++)
//					 );
//			 } catch (No_entry) {}

			// sanity check
			/* FIXME: we shouldn't have *both* the dieset and the file with their 
			 * own spec pointer. Get rid of the file one. */
			//assert(p_spec);
			assert(m_ds.p_spec);
		}
		
		/* This is the super-factory. */
		void file::encapsulate_die(lib::die& d, Dwarf_Off parent_off)
		{
			Dwarf_Half tag; d.tag(&tag);
			Dwarf_Off offset; d.offset(&offset);

			// Record the relationship with our parent.
			// NOTE: this will create m_ds[parent_off] using the default constructor,
			// generating a warning, if it hasn't been created already. Typically this is
			// when passing 0UL as the toplevel parent, without creating it first.
			
			// this is now done by attach_child, called from encapsulate_die
			//m_ds.super::operator[](parent_off)->children().push_back(offset);

			shared_ptr<die> p_encap_d;
			p_encap_d = encap::factory::for_spec(m_ds.get_spec()).encapsulate_die(
				tag, m_ds, d, parent_off);
			m_ds.insert(make_pair(offset, p_encap_d));

			// depth-first: explore children
			try
			{
				dwarf::lib::die next(d); // get the *first* child only
				encapsulate_die(next, offset); // recursively process the child and its siblings
			} catch (No_entry) {}
			// now try exploring siblings
			try
			{
				dwarf::lib::die next(*this, d); // calls dwarf_sibling
				encapsulate_die(next, parent_off);
			}
			catch (dwarf::lib::No_entry e) {}				
		}
		
		void file::add_imported_function_descriptions()
		{
			/* Much of the following code is gratefully pasted from examples
			 * written by Joseph Koshy. */
			//dwarf_elf_handle dwarf_e;
			/*::*/Elf *e;
			int retval = get_elf(&/*dwarf_*/e);
			assert(retval == DW_DLV_OK);
			//e = reinterpret_cast< ::Elf*>(dwarf_e);

			int elf_class;
			char *elf_ident;
			Elf_Kind ek;
			ek = elf_kind (e);

			switch (ek) {
				case ELF_K_ELF :
					// okay, it's a single file
					break ;
				case ELF_K_AR: // archive
				case ELF_K_NONE: // data
				default :
					assert(false); // sorry, not supported yet
					break;
			}

			GElf_Ehdr ehdr; 
			if (gelf_getehdr (e, &ehdr) == 0) throw Error(0, 0);
			if ((elf_class = gelf_getclass(e)) == ELFCLASSNONE) throw Error(0, 0);
			if ((elf_ident = elf_getident(e, 0)) == 0) throw Error(0, 0);

			Elf_Scn *scn = 0;
			GElf_Shdr shdr;
			while ((scn = elf_nextscn(e, scn)) != NULL) 
			{
				if (gelf_getshdr(scn , &shdr) != &shdr) throw Error(0, 0);

				// we don't care about the name; does it contain symbols?
				if (shdr.sh_type == SHT_SYMTAB /*|| shdr.sh_type == SHT_DYNSYM*/)
				{
					//cerr << "Found a symtab!" << endl;
					// Now look for UND symbols.
					// We really only want UND symbols that are
					// called by the referencing code. But that's
					// too much effort right now. Instead, we take any
					// symbol that doesn't appear in our DWARF info,
					// i.e. we assume that globals do get declarations
					// (which is at least true of *used globals* in gcc).
					
					Elf_Data *data = elf_getdata(scn, NULL);
					unsigned count = shdr.sh_size / shdr.sh_entsize;

					auto p_all_cus = ds().toplevel();
					
					// insert a fake wordsized type
					auto p_first_cu = *p_all_cus->compile_unit_children_begin();
					shared_ptr<base_type_die> base_type = dynamic_pointer_cast<base_type_die>(
						encap::factory::for_spec(m_ds.get_spec()).create_die(DW_TAG_base_type,
							dynamic_pointer_cast<encap::basic_die>(p_first_cu) /*,
						std::string("__cake_wordsize_integer_type")*/));
					base_type->set_byte_size((elf_class == ELFCLASS32) ? 4
									  : (elf_class == ELFCLASS64) ? 8
									  : 0);
					base_type->set_encoding(DW_ATE_signed);
					assert (base_type->get_byte_size() != 0);

					for (unsigned ii = 0; ii < count; ++ii) {
						GElf_Sym sym;
						gelf_getsym(data, ii, &sym);
						char *symname = elf_strptr(e, shdr.sh_link, sym.st_name);
						if (sym.st_shndx == SHN_UNDEF
							&& strlen(symname) > 0
							&& !ds().resolve_die_path(symname))
						{
							auto child_count = srk31::count(
								p_all_cus->children_begin(), p_all_cus->children_end());
							//cerr << "dies size before: " << ds().size() << endl;
							//cerr << "all_cus.children() size before: " << child_count
							//	<< endl;
							assert(ds().size() > 0 && child_count > 0);
							shared_ptr<subprogram_die> subprogram
							 = dynamic_pointer_cast<subprogram_die>(
								encap::factory::for_spec(m_ds.get_spec()).create_die(
									DW_TAG_subprogram,
									dynamic_pointer_cast<encap::basic_die>(p_first_cu),
									std::string(symname)
								)
							);
							//cerr << "dies size after: " << ds().size() << endl;

							// IMPORTANT: it's a declaration, not a definition
							subprogram->set_external(true);
							subprogram->set_prototyped(true);							
							subprogram->set_declaration(true);
							// we don't know anything about the arguments, so create DW_TAG_unspecified_parameters
							encap::factory::for_spec(m_ds.get_spec()).create_die(
								DW_TAG_unspecified_parameters, 
								subprogram
							);
							// we don't know anything about the return type...
							// so use our magic word-sized base type
							//if ((*all_cus.compile_units_begin())->named_child(std::string("int")))
							//{
								subprogram->set_type(
									dynamic_pointer_cast<spec::type_die>(base_type)
								);
							//}
						}
					}
				}
			}
		}
	 
		shared_ptr<file_toplevel_die> dieset::all_compile_units()
		{ return dynamic_pointer_cast<file_toplevel_die>( (*this)[0UL] ); }

		shared_ptr<dwarf::spec::basic_die> 
		dieset::operator[](dwarf::lib::Dwarf_Off off) const
		{ 
			auto found = this->super::find(off);
			assert(found != this->super::end());
			return found->second;
		}
		shared_ptr<spec::file_toplevel_die> 
		dieset::toplevel()
		{ 
			return dynamic_pointer_cast<spec::file_toplevel_die>(operator[](0UL));
		}	
//		 encap::rangelist dieset::rangelist_at(Dwarf_Unsigned i) const
//		 {
//		 	return m_aranges.at(i);
//		 }

		/* Copy constructor. */
		dieset& dieset::operator=(const dieset& arg)
		{
			cerr << "Copying from dieset at " << &arg << " to dieset at " << this << endl;
			// pretend we're destructing, so that DIE destructors
			// don't complain about loss of referential integrity during clear().
			this->destructing = true;
			this->map::clear();
			this->last_monotonic_offset = arg.last_monotonic_offset;
			this->destructing = arg.destructing;
			this->p_spec = arg.p_spec;
			for (auto i = arg.map_begin(); i != arg.map_end(); ++i)
			{
				auto p_d = dynamic_pointer_cast<encap::basic_die>(i->second);
				auto cloned_die = factory::for_spec(*arg.p_spec).clone_die(
						*this, 
						p_d);
				/* We have to update the p_ds pointer in each weak_ref 
				 * so that it points to the new dieset. */
				die::attribute_map& cloned_attrs = cloned_die->attrs();
				die::attribute_map& orig_attrs = i->second->attrs();
				auto i_orig_attr = orig_attrs.begin();
				for (auto i_clone_attr = cloned_attrs.begin(); i_clone_attr != cloned_attrs.end(); 
					++i_clone_attr, ++i_orig_attr)
				{
					i_clone_attr->second.p_ds = this;
					if (i_clone_attr->second.get_form() == attribute_value::REF)
					{
						cerr << "Copied a ref, from ref obj at addr " << i_orig_attr->second.v_ref
							<< " to ref obj at attr " << i_clone_attr->second.v_ref << endl;
						i_clone_attr->second.v_ref->p_ds = this;
					}
					// for all reference attributes, assert that if we follow them,
					// we are still within the same dieset.
					assert(&cloned_die->get_ds() == this);
					assert((*cloned_die)[i_clone_attr->first].get_form() != attribute_value::REF
					   ||  (*cloned_die)[i_clone_attr->first].get_ref().p_ds == this);
				}
				
				this->insert(make_pair(i->first, cloned_die));
				assert(this->find(i->first) != this->end()
					&& &(this->map::find(i->first)->second->m_ds) == this);
			}
			
			for (auto i_die = this->begin(); i_die != this->end(); ++i_die)
			{
				assert(&(*i_die)->get_ds() == this);
				for (auto i_attr = dynamic_pointer_cast<encap::die>(*i_die)->attrs().begin(); 
					i_attr != dynamic_pointer_cast<encap::die>(*i_die)->attrs().end();
					++i_attr)
				{
					// for all reference attributes, assert that if we follow them,
					// we are still within the same dieset.
					assert((*dynamic_pointer_cast<encap::die>(*i_die))[i_attr->first].get_form() != attribute_value::REF
					   ||  (*dynamic_pointer_cast<encap::die>(*i_die))[i_attr->first].get_ref().p_ds == this);
				}
			}

			this->all_compile_units()->integrity_check();
			return *this;
		}
		
		void 
		dieset::build_path_from_root
			(std::deque<spec::abstract_dieset::position>& path, 
			map_iterator current)
		{
			assert(current != this->map_end());
			//if (current == this->map_end()) return;
			do 
			{
				path.push_front((spec::abstract_dieset::position){this, current->second->get_offset()});
				current = this->map_find(current->second->get_parent()->get_offset());
			} while (current->second->get_offset() != 0UL);
		}
		std::deque<spec::abstract_dieset::position> 
		dieset::path_from_root(Dwarf_Off off)
		{
			std::deque<position> path;
			build_path_from_root(path, this->map_find(off));
			return path;
		}

// 		struct Print_Action
// 		{
// 			std::ostream& o;
// 			srk31::indenting_ostream wrapped_stream;
// 			Print_Action(std::ostream& o) : o(o), wrapped_stream(o) {}
// 			std::ostream& operator()(const dieset::value_type& d)
// 			{
// 				// work out our indent level
// 				int indent_level = 0;
// 				for (Dwarf_Off parent = d.second->get_offset(); 
// 						parent != 0UL; 
// 						parent = d.second->get_ds().map_find(parent)->second->parent_offset()) indent_level++;
// 
// 				// HACK: set our indent level in the stream
// 				if (indent_level > wrapped_stream.level()) 
// 						while (wrapped_stream.level() < indent_level) wrapped_stream.inc_level();
// 				else while (wrapped_stream.level() > indent_level) wrapped_stream.dec_level();
// 				
// 				// output, preserving indent level across newlines
// 				// Note: happily, this doesn't suffer the bug of
// 				// wrapping the wrapped stream again, because
// 				// child DIEs aren't printed by printing sub-diesets recursively; 
// 				// instead, the recursion happens in the depthfirst walker,
// 				// and all activations of that share the same Print_Action.
// 				wrapped_stream << *(d.second);
// 				return o;
// 			}
// 		};
// 		std::ostream& operator<<(std::ostream& o, const dieset& ds)
// 		{
// 			Print_Action action(o);
// 			dieset::Match_All match_all(&ds);
// 			ds.walk_depthfirst_const(0UL, action, match_all, match_all);
// 			o << endl;
// 			return o;
// 		}		
// 		std::ostream& print_artificial(std::ostream& o, const dieset& ds)
// 		{
// 			Print_Action action(o);
// 			dieset::Match_Offset_Greater_Equal match(&ds, 1<<30 /* FIXME: modularise this*/);
// 			ds.walk_depthfirst_const(0UL, action, match, match);
// 			o << endl;
// 			return o;
// 		}	   
		
		die::die(const die& d) 
			: m_ds(d.m_ds), m_parent(d.m_parent), m_tag(d.m_tag), m_offset(d.m_offset), 
				cu_offset(d.cu_offset),
				m_attrs(d.m_attrs), m_children(d.m_children)
		{ /*cerr << "Copy constructing an encap::die" << endl;*/ }
				
		die::die(dieset& ds, lib::die& d, Dwarf_Off parent_off) 
			: m_ds(ds), m_parent(parent_off)
		{ initialize_from_lib_die(d); }
		die::die(dieset& ds, shared_ptr<lib::die> p_d, Dwarf_Off parent_off) 
			: m_ds(ds), m_parent(parent_off)
		{ initialize_from_lib_die(*p_d); }
		
		void die::initialize_from_lib_die(lib::die& d)
		{
			/* To encapsulate a DIE, we copy all its properties/attributes,
			 * and encode references to other DIEs and to child DIEs
			 * by their die_offset (i.e. within .debug_info of file f).
			 *
			 * To encapsulate all DIEs in a file, therefore, we build a
			 * map of offsets to encapsulated DIEs. */

			d.tag(&m_tag);
			d.offset(&m_offset);
			d.CU_offset(&cu_offset);

			// store a backref denoting the parent--child relationship, using the magic DW_AT_ 0
			m_ds.backrefs()[m_parent].push_back(make_pair(m_offset, 0));			

			// now for the awkward squad: name and other attributes
			int retval;
			attribute_array arr(d);
			for (int i = 0; i < arr.count(); i++)
			{
				Dwarf_Half attr; 
				try
				{
					dwarf::lib::attribute a = arr.get(i);
					retval = a.whatattr(&attr);
					m_attrs.insert(make_pair(attr, attribute_value(m_ds, a)));
				}
				catch (Not_supported) 
				{
					/* We have already warned about this -- so continue. */
					continue;
				}

			} // end for

			// we're done -- don't encode children now; they must be encapsulated separately
		}
		
		die::~die()
		{
			/* If we are getting destructed from the dieset destructor,
			 * there's a possible subtlety here: is calling find() 
			 * on the backrefs map defined?
			 * It *should* be okay, but valgrind is showing that
			 * the map is accessing nodes after they've been
			 * deleted. */
			
			/* HACK: this was horribly broken because if we're getting called
			 * from the std::map destructor, the derived class (including the
			 * backrefs vector) has already been destructed. So test whether
			 * the destructor is running and if so, don't do anything to access
			 * backrefs (its memory may have been deallocated). */
			
			// if the backrefs list still contains data on our parent...
			if (!m_ds.destructing && m_ds.backrefs().find(m_parent) != m_ds.backrefs().end())
			{
				// .. and includes us in the list of backrefs...
 				dieset::backref_list::iterator found = std::find(
 					m_ds.backrefs()[m_parent].begin(),
 					m_ds.backrefs()[m_parent].end(),
 					make_pair(m_offset, (Dwarf_Half) 0));
 				if (found != m_ds.backrefs()[m_parent].end())
 				{
					// ... erase that record
 					m_ds.backrefs()[m_parent].erase(found);
 				}
			}
			else
			{
				// don't assert(false) -- this *might* happen, because
				// (1) the std::map red-black tree structure won't mirror the DWARF tree structure
				// so parents might get destroyed before children
				// (2) the toplevel node is special -- it gets constructed by default,
				// and we can remove it from the map if we assign to the dieset.
				if (!(m_ds.destructing || m_offset == 0UL))
				{
					cerr << "WARNING: inexplicable destructing of " << *this
						<< endl;
				}
			}
		}
		
		void die::attach_child(boost::shared_ptr<encap::basic_die> p)
		{ 
			assert(m_ds.find(p->get_offset()) == m_ds.end());
			assert(p->parent_offset() == m_offset);
			m_ds.super::operator[](p->get_offset()) = p;
			m_children.push_back(p->get_offset());
		}
							
// 		// TODO: remove this now that we have operator<< on spec::basic_die
// 		std::ostream& operator<<(std::ostream& o, const dwarf::encap::die& d)
// 		{
// 			o 	<< "DIE, child of 0x" << std::hex << d.m_parent << std::dec 
// 				<< ", tag: " << d.get_ds().spec().tag_lookup(d.m_tag) 
// 				<< ", offset: 0x" << std::hex << d.m_offset << std::dec 
// 				<< ", name: "; if (d.has_attr(DW_AT_name)) o << d[DW_AT_name]; else o << "(no name)"; o << endl;
// 
// 			for (std::map<Dwarf_Half, encap::attribute_value>::const_iterator p 
// 					= d.const_attrs().begin();
// 				p != d.const_attrs().end(); p++)
// 			{
// 				o << "\t";
// 				o << "Attribute " << d.get_ds().get_spec().attr_lookup(p->first) << ", value: ";
// 				p->second.print_as(o, d.get_ds().get_spec().get_interp(p->first, p->second.orig_form));
// 				o << endl;	
// 			}			
// 
// 			return o;
// 		}
		
		opt<die&> dieset::resolve_die_path(const Dwarf_Off start, 
			const pathname& path, pathname::const_iterator pos)
		{
			opt<die&> none;
#define CAST_TO_DIE(arg) \
	dynamic_pointer_cast<die, spec::basic_die>((arg))
			if (pos == path.end()) { return opt<die&>(*(CAST_TO_DIE((*this)[start]))); }
			else
			{
				//cerr << "Looking for a DWARF element named " << *pos 
				//	<< " starting from offset 0x" << std::hex << start << std::dec
				//	<< ", " << dies[start] << endl;
				if (spec().tag_is_type(CAST_TO_DIE((*this)[start])->get_tag()) && 
					!spec().tag_has_named_children(CAST_TO_DIE((*this)[start])->get_tag()))
				{
					// this is a chained type qualifier -- recurse
					assert((CAST_TO_DIE((*this)[start])->has_attr(DW_AT_type)));
					return resolve_die_path(CAST_TO_DIE((*this)[start])->get_attr(DW_AT_type).get_ref().off,
						path, pos);
				}
				else if (spec().tag_has_named_children(CAST_TO_DIE((*this)[start])->get_tag()) || start == 0UL)
				{
					// this is a compilation unit or type or variable or subprogram with named children
					for (die_off_list::iterator iter = CAST_TO_DIE((*this)[start])->children().begin();
						iter != CAST_TO_DIE((*this)[start])->children().end();
						iter++)
					{
						if (CAST_TO_DIE((*this)[*iter])->has_attr(DW_AT_name) && 
							CAST_TO_DIE((*this)[*iter])->get_attr(DW_AT_name).get_string() == *pos)
						{
							return resolve_die_path(*iter, path, pos + 1);
						}
						// else found an anonymous child -- continue
					} // end for
					// failed
					return none; 
				}
				else 
				{	
					return none;
				} // end if tag_is_type
			} // end if pos
		} // end resolve_pathname
		
		/* This is our main interface for setting/mutating attributes. 
		 * Usually it's trivial. But in the case of references, we convert
		 * weak references into strong references so that we can track
		 * backrefs, referential integrity etc.. */
		attribute_value& die::put_attr(Dwarf_Half attr, attribute_value val)
		{ 
			if (val.get_form() != attribute_value::REF)
			{
				/* Trivial version. */
				m_attrs.erase(attr);
				m_attrs.insert(make_pair(attr, val)); 
				return m_attrs.find(attr)->second; 
			}
			else
			{
				/* Sanity checks. */
				assert(&m_ds == &val.get_refdie()->get_ds());
				/* Convert weak ref into strong ref. */
				attribute_value::ref r(*val.get_ref().p_ds, val.get_ref().off,
					val.get_ref().abs, 
					this->m_offset, attr);
				/* ^ note that our weak ref may not have referencing_off and referencing_attr! 
				 * i.e. they may be set to numeric_limits<...>::max(). */
				/* Now proceed as above. */
				m_attrs.erase(attr);
				m_attrs.insert(make_pair(attr, attribute_value(m_ds, r))); 
				return m_attrs.find(attr)->second; 
			}
		}
//		 attribute_value& die::put_attr(Dwarf_Half attr, 
//			 	shared_ptr<basic_die> target)
//		 {
//		 	attribute_value::ref r(
//				 target->m_ds, 
//				 target->m_offset, false,
//				 this->m_offset, attr);
//			 return put_attr(attr, attribute_value(
//			 		target->m_ds,
//					 r));
//		 }
	} // end namespace encap
} // end namespace dwarf
