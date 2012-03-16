#include <deque>
#include <utility>
#include <sstream>
#include <algorithm>
#include <boost/make_shared.hpp>

#include "spec_adt.hpp"
#include "adt.hpp"
#include "attr.hpp"

#include <srk31/algorithm.hpp>
#include <srk31/indenting_ostream.hpp>

namespace dwarf
{
	using boost::dynamic_pointer_cast;
	using boost::optional;
	using boost::shared_ptr;
	using std::string;
	using std::ostringstream;
	using std::pair;
	using std::endl;
	using std::cerr;
	using std::clog;
	using std::vector;
	using namespace dwarf::lib;

	namespace spec
    {
/* from spec::basic_die */
		string basic_die::to_string() const
		{
			ostringstream s;
			s << *this;
			return s.str();
		}
		string basic_die::summary() const
		{
			ostringstream s;
			s << "DIE at 0x" << std::hex << get_offset() << std::dec
				<< ", tag " << get_spec().tag_lookup(get_tag())
				<< ", name " << (get_name() ? *get_name() : "(anonymous)");
			return s.str();
		}
		void basic_die::print_to_stderr() const
		{
			cerr << *this;
		}
        boost::shared_ptr<basic_die> basic_die::get_this()
        { return this->get_ds()[this->get_offset()]; }
        boost::shared_ptr<basic_die> basic_die::get_this() const
        { return this->get_ds()[this->get_offset()]; }

        opt<std::vector<std::string> >
        basic_die::ident_path_from_root() const
        {
            if (get_offset() == 0) return std::vector<std::string>(); // empty
            else if (get_name())
            {
	            opt<std::vector<std::string> > built 
                 = const_cast<basic_die*>(this)->get_parent()->ident_path_from_root();
                if (!built) return 0;
                else
                {
	                (*built).push_back(*get_name());
    	            return built;
                }
            }
            else return 0;
        }

        opt<std::vector<std::string> >
        basic_die::ident_path_from_cu() const
        {
            if (get_offset() == 0) return 0; // error
            if (get_tag() == DW_TAG_compile_unit) return std::vector<std::string>(); // empty
            else if (get_name()) // recursive case
            {
                // try to build our parent's path
	            opt<std::vector<std::string> >
                    built = const_cast<basic_die*>(this)->get_parent()->ident_path_from_cu();
                if (!built) return 0;
                else // success, so just add our own name to the path
                {
	                (*built).push_back(*get_name());
    	            return built;
                }
            }
            else return 0; // error -- we have no name
        }

		std::vector< opt<std::string> >
		basic_die::opt_ident_path_from_root() const
		{
			if (get_offset() == 0) return std::vector<opt<std::string> >(); // empty
			else 
			{
				// assert that we have a parent but it's not us
				assert(this->get_parent() && this->get_parent()->get_offset() != get_offset());
				std::vector< opt<std::string> > built 
				 = const_cast<basic_die*>(this)->get_parent()->opt_ident_path_from_root();
				built.push_back(get_name());
				return built;
			}
		}

		std::vector < opt<std::string> >
		basic_die::opt_ident_path_from_cu() const
		{
			if (get_offset() == 0) return std::vector < opt<std::string> >(); // error
			if (get_tag() == DW_TAG_compile_unit)
			{
				return std::vector<opt<std::string> >(); // empty
			}
			else // recursive case
			{
				// assert that we have a parent but it's not us
				
				// assertion pre-failure debugging aid
				if (!(this->get_parent() && this->get_parent()->get_offset() != get_offset()))
				{
					std::cerr << *this;
				}
				assert(this->get_parent() && this->get_parent()->get_offset() != get_offset());
				// try to build our parent's path
				std::vector< opt< std::string> >
				built = const_cast<basic_die*>(this)->get_parent()->opt_ident_path_from_cu();
				built.push_back(get_name());
				return built;
			}
		}
        boost::shared_ptr<spec::basic_die> 
        basic_die::nearest_enclosing(Dwarf_Half tag) const
		{
			return const_cast<basic_die *>(this)->nearest_enclosing(tag);
		}
        boost::shared_ptr<spec::basic_die> 
        basic_die::nearest_enclosing(Dwarf_Half tag) 
        {
            /*if (get_tag() == 0 || get_offset() == 0UL) return NULL_SHARED_PTR(spec::basic_die);
            else if (get_parent()->get_tag() == tag) 
            {
				return get_parent();
            }
            else return get_parent()->nearest_enclosing(tag);*/
            
            // instead use more efficient (and hopefully correct!) iterative approach
            // -- this avoids recomputing the path for each recursive step
            auto my_iter = this->get_ds().find(this->get_offset()); // computes path
            assert(my_iter.base().path_from_root.size() >= 1);
            for (auto path_iter = ++my_iter.base().path_from_root.rbegin(); 
            	path_iter != my_iter.base().path_from_root.rend();
                path_iter++)
            {
            	if (this->get_ds()[path_iter->off]->get_tag() == tag)
                {
                	return this->get_ds()[path_iter->off];
                }
			}
            return boost::shared_ptr<spec::basic_die>();
        }

        boost::shared_ptr<compile_unit_die> 
        basic_die::enclosing_compile_unit()
        {
        	// HACK: special case: compile units enclose themselves (others don't)
            // -- see with_static_location_die::contains_addr for motivation
            return boost::dynamic_pointer_cast<compile_unit_die>(
            	this->get_tag() == DW_TAG_compile_unit ?
                	this->get_this()
            	:	nearest_enclosing(DW_TAG_compile_unit));
		}
            
        boost::shared_ptr<spec::basic_die>
        basic_die::find_sibling_ancestor_of(boost::shared_ptr<spec::basic_die> p_d) 
        {
            // search upward from the argument die to find a sibling of us
            if (p_d.get() == dynamic_cast<spec::basic_die*>(this)) return p_d;
            else if (p_d->get_offset() == 0UL) return NULL_SHARED_PTR(spec::basic_die); // reached the top without finding anything
            else if (this->get_offset() == 0UL) return NULL_SHARED_PTR(spec::basic_die); // we have no siblings
            else if (p_d->get_parent() == this->get_parent()) // we are siblings
            {
                return p_d;
            }
            else return find_sibling_ancestor_of(p_d->get_parent()); // recursive case
        }
        
        std::ostream& operator<<(std::ostream& o, const ::dwarf::spec::basic_die& d)
        {
			/* we skip printing parents until navigation makes this not ridiculously
			   expensive. */
			
			o 	<< "DIE" /*child of 0x" 
            	<< std::hex << ((d.get_parent()) 
                				? d.get_parent()->get_offset()
			                	: 0UL)
                    << std::dec */
				<< ", tag: " << d.get_ds().get_spec().tag_lookup(d.get_tag()) 
				<< ", offset: 0x" << std::hex << d.get_offset() << std::dec 
				<< ", name: "; 
            auto attrs = const_cast<basic_die&>(d).get_attrs();
            if (attrs.find(DW_AT_name) != attrs.end()) o << attrs.find(DW_AT_name)->second; 
            else o << "(no name)"; 
            o << std::endl;

			for (std::map<Dwarf_Half, encap::attribute_value>::const_iterator p 
					= attrs.begin();
				p != attrs.end(); p++)
			{
				o << "\t";
				o << "Attribute " << d.get_ds().get_spec().attr_lookup(p->first) << ", value: ";
				p->second.print_as(o, d.get_ds().get_spec().get_interp(
                	p->first, p->second.orig_form));
				o << std::endl;	
			}			

			return o;
        }
        std::ostream& operator<<(std::ostream& s, const abstract_dieset& ds)
		{
			srk31::indenting_ostream wrapped_stream(s);
			// HACK
			abstract_dieset& nonconst_ds = const_cast<abstract_dieset&>(ds);
			assert(nonconst_ds.begin().base().path_from_root.size() == 1);
			for (auto i_dfs = nonconst_ds.begin(); i_dfs != nonconst_ds.end(); ++i_dfs)
			{
				// fix up our indent level
				int indent_level = (int) i_dfs.base().path_from_root.size() - 1;
				if (indent_level > wrapped_stream.level()) 
				{
 					while (wrapped_stream.level() < indent_level) 
					{ wrapped_stream.inc_level(); }
				}
 				else while (wrapped_stream.level() > indent_level)
				{ wrapped_stream.dec_level(); }
				
				wrapped_stream << **i_dfs;
			}
			
			// return the indent level to zero
 			while (wrapped_stream.level() > 0) { wrapped_stream.dec_level(); }
			
			return s;
		}
/* from spec::with_static_location_die */
		opt<Dwarf_Off> // returns *offset within the element*
        with_static_location_die::contains_addr(Dwarf_Addr file_relative_address,
        	sym_binding_t (*sym_resolve)(const std::string& sym, void *arg), 
			void *arg /* = 0 */) const
        {
        	// FIXME: get rid of the const_casts
            auto nonconst_this = const_cast<with_static_location_die *>(this);
        	auto attrs = nonconst_this->get_attrs();

        	// HACK: if we're a local variable, return false. This function
            // only deals with static storage. Mostly the restriction is covered
            // by the fact that only certain tags are with_static_location_dies,
            // but both locals and globals show up with DW_TAG_variable.
            if (this->get_tag() == DW_TAG_variable &&
            	!dynamic_cast<const variable_die *>(this)->has_static_storage())
                return opt<Dwarf_Off>();

            auto found_low_pc = attrs.find(DW_AT_low_pc);
            auto found_high_pc = attrs.find(DW_AT_high_pc);
           	auto found_ranges = attrs.find(DW_AT_ranges);
           	auto found_location = attrs.find(DW_AT_location);
            auto found_mips_linkage_name = attrs.find(DW_AT_MIPS_linkage_name); // HACK: MIPS should...
            auto found_linkage_name = attrs.find(DW_AT_linkage_name); // ... be in a non-default spec
			/* We like ranges best, as they're the most precise.
			 * Also, my gcc-4.6.2-produced executables on NetBSD have 0 for hipc and lopc,
			 * but has precise information in ranges. */
            if (found_ranges != attrs.end())
            {
            	auto rangelist = found_ranges->second.get_rangelist();
                //std::cerr << "DIE at 0x" << std::hex << this->get_offset()
                //	<< " has rangelist " << rangelist << std::endl;
                /* auto nonconst_this = const_cast<with_static_location_die *>(this); */
                // rangelist::find_addr() requires a dieset-relative (i.e. file-relative) address
                /*assert(nonconst_this->enclosing_compile_unit()->get_low_pc());*/
             	auto range_found = rangelist.find_addr(
                 	file_relative_address /*- 
                     *(nonconst_this->enclosing_compile_unit()->get_low_pc())*/);
                 if (!range_found) return opt<Dwarf_Off>();
                 else 
                 {
                 	return range_found->first;
                 }
            }
            else if (found_low_pc != attrs.end()
            	&& found_high_pc != attrs.end())
            {
            	//std::cerr << "DIE at 0x" << std::hex << this->get_offset()
                //	<< " has low/high PC " << found_low_pc->second.get_address() << ", "
                //    << found_high_pc->second.get_address() << std::endl;
            	if (file_relative_address >= found_low_pc->second.get_address()
                	&& file_relative_address < found_high_pc->second.get_address())
                {
                	return file_relative_address - found_low_pc->second.get_address();
                }
                else return opt<Dwarf_Off>();
			}
            else if (found_location != attrs.end())
            {
            	/* Location lists can be vaddr-dependent, where vaddr is the 
                 * offset of the current PC within the containing subprogram.
                 * However, this is only for parameters and locals, and even
                 * then, they are usually relative to a DW_AT_frame_base, and
                 * only that is vaddr-dependent. In any case, because we only
                 * deal with variables and subprograms and 
                 * lexical blocks and inlined subprogram
                 * instances here, I don't *think* we need to worry about this. */
                
                /* FIXME: if we *do* need to worry about that, work out
                 * whether lexical block DIEs can ever have
                 * vaddr-dependent location lists, and if so, whether
                 * they use subprogram-relative or block-relative vaddrs. */
                
            	/* Here we need a location and a size, which may come
                 * directly, or from the type. First calculate the byte size (easier). */
                Dwarf_Unsigned byte_size;
                //std::cerr << "DIE at 0x" << std::hex << this->get_offset()
                //	<< " has location list " << found_location->second.get_loclist() << std::endl;
                auto found_byte_size = attrs.find(DW_AT_byte_size);
                if (found_byte_size != attrs.end())
                {
                	byte_size = found_byte_size->second.get_unsigned();
                }
                else
                {	
                	/* Look for the type. "Type" means something different
                     * for a subprogram, which should be covered by the
                     * high_pc/low_pc and ranges cases, so assert that
	                 * we don't have one of those. */
                    assert(this->get_tag() != DW_TAG_subprogram);
                    auto found_type = attrs.find(DW_AT_type);
                    if (found_type == attrs.end()) return opt<Dwarf_Off>();
                    else
                    {
                    	auto calculated_byte_size = boost::dynamic_pointer_cast<spec::type_die>(
                    		this->get_ds()[found_type->second.get_ref().off])->calculate_byte_size();
                        assert(calculated_byte_size);
                        byte_size = *calculated_byte_size;
                    }
				}
                
                auto loclist = found_location->second.get_loclist();
                auto expr_pieces = loclist.loc_for_vaddr(0).pieces();
                Dwarf_Off current_offset_within_object = 0UL;
                for (auto i = expr_pieces.begin(); i != expr_pieces.end(); i++)
                {
                	/* Evaluate this pieces and see if it spans the address
                     * we're interested in. */
	                Dwarf_Unsigned location = dwarf::lib::evaluator(i->first,
	                    this->get_spec()).tos();
                    if (file_relative_address >= location
                    	&& file_relative_address < location + i->second)
                        // match
                        return current_offset_within_object + (file_relative_address - location);
                    else current_offset_within_object += i->second;
                }
            }
            else if (sym_resolve &&
            	(found_mips_linkage_name != attrs.end()
            	|| found_linkage_name != attrs.end()))
            {
            	std::string linkage_name;
                
            	// prefer the DWARF 4 attribute to the MIPS/GNU/... extension
                if (found_linkage_name != attrs.end()) linkage_name 
                 = found_linkage_name->second.get_string();
                else 
                { 
                	assert(found_mips_linkage_name != attrs.end());
                	linkage_name = found_mips_linkage_name->second.get_string();
                }
                
                std::cerr << "DIE at 0x" << std::hex << this->get_offset()
                	<< " has linkage name " << linkage_name << std::endl;
                
                sym_binding_t binding;
                try
                {
		            binding = sym_resolve(linkage_name, arg);
                    if (file_relative_address >= binding.file_relative_start_addr
                	    && file_relative_address < binding.file_relative_start_addr + binding.size)
                    {
                    	// slight HACK: assume objects located only by DW_AT_linkage_address...
                        // ... are contiguous in memory
                	    return opt<Dwarf_Off>(
                        	file_relative_address -
                            	binding.file_relative_start_addr);
                    }
                    // else fall through to final return statement
                }
                catch (lib::No_entry)
                {
                	std::cerr << "Warning: couldn't resolve linkage name " << linkage_name
                    	<< " for DIE " << *this << std::endl;
                }
            }
            return opt<Dwarf_Off>();
        }
/* helpers */        
        static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc, Dwarf_Addr high_pc);
        static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc, Dwarf_Addr high_pc)
        {
            Dwarf_Unsigned opcodes[] 
            = { DW_OP_constu, low_pc, 
                DW_OP_piece, high_pc - low_pc };
			/* FIXME: I don't think we should be using the max Dwarf_Addr here -- 
			 * the libdwarf manual claims we should set them both to zero. */
            encap::loclist list(encap::loc_expr(opcodes, 0, std::numeric_limits<Dwarf_Addr>::max())); 
            return list;
        }
        static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc);
        static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc)
        {
            Dwarf_Unsigned opcodes[] 
            = { DW_OP_constu, low_pc };
			/* FIXME: I don't think we should be using the max Dwarf_Addr here -- 
			 * the libdwarf manual claims we should set them both to zero. */
            encap::loclist list(encap::loc_expr(opcodes, 0, std::numeric_limits<Dwarf_Addr>::max())); 
            return list;
        }
		encap::loclist with_static_location_die::get_static_location() const
        {
        	auto attrs = const_cast<with_static_location_die *>(this)->get_attrs();
            if (attrs.find(DW_AT_location) != attrs.end())
            {
            	return attrs.find(DW_AT_location)->second.get_loclist();
            }
            else
        	/* This is a dieset-relative address. */
            if (attrs.find(DW_AT_low_pc) != attrs.end() 
            	&& attrs.find(DW_AT_high_pc) != attrs.end())
            {
            	return loclist_from_pc_values(attrs.find(DW_AT_low_pc)->second.get_address().addr,
                	attrs.find(DW_AT_high_pc)->second.get_address().addr);
            }
            else
            {
            	assert(attrs.find(DW_AT_low_pc) != attrs.end());
        	    return loclist_from_pc_values(
                	attrs.find(DW_AT_low_pc)->second.get_address().addr);
            }
        }
/* from spec::subprogram_die */
		opt< std::pair<Dwarf_Off, boost::shared_ptr<with_dynamic_location_die> > >
        subprogram_die::contains_addr_as_frame_local_or_argument( 
            	    Dwarf_Addr absolute_addr, 
                    Dwarf_Off dieset_relative_ip, 
                    Dwarf_Signed *out_frame_base,
                    dwarf::lib::regs *p_regs/* = 0*/) const
        {
        	/* auto nonconst_this = const_cast<subprogram_die *>(this); */
        	assert(this->get_frame_base());
        	/* First we calculate our frame base address. */
            auto frame_base_addr = dwarf::lib::evaluator(
                *this->get_frame_base(),
                dieset_relative_ip - *this->enclosing_compile_unit()->get_low_pc(), // this is the vaddr which selects a loclist element
                this->get_ds().get_spec(),
                p_regs).tos();
            if (out_frame_base) *out_frame_base = frame_base_addr;
            
            if (!this->first_child_offset()) return 0;
			/* Now we walk children
			 * (not just immediate children, because more might hide under lexical_blocks), 
			 * looking for with_dynamic_location_dies, and 
			 * call contains_addr on what we find.
			 * We skip contained DIEs that do not contain objects located in this frame. 
			 */
			struct stack_location_subtree_policy :  public spec::abstract_dieset::bfs_policy
			{
				typedef spec::abstract_dieset::bfs_policy super;
				int increment(spec::abstract_dieset::position& pos,
					spec::abstract_dieset::path_type& path)
				{
					/* If our current DIE is 
					 * a with_dynamic_location_die
					 * OR
					 * is in the "interesting set"
					 * of DIEs that have no location but might contain such DIEs,
					 * we increment *with* enqueueing children.
					 * Otherwise we increment without enqueueing children.
					 */
					auto p_die = (*pos.p_ds)[pos.off];
					if (dynamic_pointer_cast<spec::with_dynamic_location_die>(p_die))
					{
						return super::increment(pos, path);
					}
					else
					{
						switch (p_die->get_tag())
						{
							case DW_TAG_lexical_block:
								return super::increment(pos, path);
								break;
							default:
								return super::increment_skipping_subtree(pos, path);
								break;
						}
					}
				}
			} bfs_state;
			//abstract_dieset::bfs_policy bfs_state;
            abstract_dieset::iterator start_iterator
             = this->get_first_child()->iterator_here(bfs_state);
            std::cerr << "Exploring stack-located children of " << this->summary() << std::endl;
            unsigned initial_depth = start_iterator.base().path_from_root.size();
            for (auto i_bfs = start_iterator;
            		i_bfs.base().path_from_root.size() >= initial_depth;
                    ++i_bfs)
            {
            	std::cerr << "Considering whether DIE has stack location: " << (*i_bfs)->summary() << std::endl;
            	auto with_stack_loc = boost::dynamic_pointer_cast<spec::with_dynamic_location_die>(
                	*i_bfs);
                if (!with_stack_loc) continue;
                
                opt<Dwarf_Off> result = with_stack_loc->contains_addr(absolute_addr,
                	frame_base_addr,
                    dieset_relative_ip,
                    p_regs);
                if (result) return std::make_pair(*result, with_stack_loc);
            }
            return opt< std::pair<Dwarf_Off, boost::shared_ptr<with_dynamic_location_die> > >();
        }
        bool subprogram_die::is_variadic() const
        {
    	    try
            {
    	        for (auto i_child = this->get_first_child(); i_child;  // term'd by exception
            	    i_child = i_child->get_next_sibling())
                {
                    if (i_child->get_tag() == DW_TAG_unspecified_parameters)
                    {
            	        return true;
                    }
                }
        	}
            catch (No_entry) {}
            return false;
        }
/* from spec::with_dynamic_location_die */
		boost::shared_ptr<spec::program_element_die> 
		with_dynamic_location_die::get_instantiating_definition() const
		{
			/* We want to return a parent DIE describing the thing whose instances
			 * contain instances of us. 
			 *
			 * If we're a member or inheritance, it's our nearest enclosing type.
			 * If we're a variable or fp, it's our enclosing subprogram.
			 * This might be null if we're actually a static variable. */
			// HACK: this should arguably be in overrides for formal_parameter and variable
			if (this->get_tag() == DW_TAG_formal_parameter
			||  this->get_tag() == DW_TAG_variable) 
			{
				return boost::dynamic_pointer_cast<dwarf::spec::program_element_die>(
					nearest_enclosing(DW_TAG_subprogram));
			}
			else
			{
				boost::shared_ptr<dwarf::spec::basic_die> candidate = this->get_parent();
				while (candidate 
					&& !boost::dynamic_pointer_cast<dwarf::spec::type_die>(candidate))
				{
					candidate = candidate->get_parent();
				}
				return boost::dynamic_pointer_cast<dwarf::spec::program_element_die>(candidate);
			}
		}

/* from spec::with_dynamic_location_die */
		opt<Dwarf_Off> with_dynamic_location_die::contains_addr_on_stack(
                    Dwarf_Addr absolute_addr,
                    Dwarf_Signed frame_base_addr,
                    Dwarf_Off dieset_relative_ip,
                    dwarf::lib::regs *p_regs) const
        {
        	auto attrs = const_cast<with_dynamic_location_die *>(this)->get_attrs();
            auto base_addr = calculate_addr_on_stack(
				frame_base_addr,
				dieset_relative_ip,
				p_regs);
			std::cerr << "Calculated that an instance of DIE" << this->summary()
				<< " has base addr 0x" << std::hex << base_addr << std::dec;
            assert(attrs.find(DW_AT_type) != attrs.end());
            auto size = *(attrs.find(DW_AT_type)->second.get_refdie_is_type()->calculate_byte_size());
			std::cerr << " and size " << size
				<< ", to be tested against absolute addr 0x"
				<< std::hex << absolute_addr << std::dec << std::endl;
            if (absolute_addr >= base_addr
            &&  absolute_addr < base_addr + size)
            {
 				return absolute_addr - base_addr;
            }
            return opt<Dwarf_Off>();
        }
/* from with_dynamic_location_die, stack-based cases */
		encap::loclist formal_parameter_die::get_dynamic_location() const
		{
			/* These guys are probably relative to a frame base. 
			   If they're not, it's an error. So we rewrite the loclist
			   so that it's relative to a frame base. */
			absolute_loclist_to_additive_loclist(
				*this->get_location());
		
		}
		encap::loclist variable_die::get_dynamic_location() const
		{
			// we need an enclosing subprogram or lexical_block
			auto p_lexical = this->nearest_enclosing(DW_TAG_lexical_block);
			auto p_subprogram = this->nearest_enclosing(DW_TAG_subprogram);
			if (!p_lexical && !p_subprogram) throw No_entry();
			
			return 	absolute_loclist_to_additive_loclist(
				*this->get_location());
		}
/* from spec::with_dynamic_location_die */
		opt<Dwarf_Off> with_dynamic_location_die::contains_addr_in_object(
                    Dwarf_Addr absolute_addr,
                    Dwarf_Signed object_base_addr,
                    Dwarf_Off dieset_relative_ip,
                    dwarf::lib::regs *p_regs) const
        {
        	auto attrs = const_cast<with_dynamic_location_die *>(this)->get_attrs();
            auto base_addr = calculate_addr_in_object(
				object_base_addr, dieset_relative_ip, p_regs);
            assert(attrs.find(DW_AT_type) != attrs.end());
            auto size = *(attrs.find(DW_AT_type)->second.get_refdie_is_type()->calculate_byte_size());
            if (absolute_addr >= base_addr
            &&  absolute_addr < base_addr + size)
            {
 				return absolute_addr - base_addr;
            }
            return opt<Dwarf_Off>();
        }
/* from with_dynamic_location_die, object-based cases */
		encap::loclist member_die::get_dynamic_location() const
		{
			/* These guys have loclists that adding to what on the
			   top-of-stack, which is what we want. */
			return *this->get_data_member_location();
		}
		encap::loclist inheritance_die::get_dynamic_location() const
		{
			return *this->get_data_member_location();
		}
/* from spec::with_dynamic_location_die */
		Dwarf_Addr 
		with_dynamic_location_die::calculate_addr_on_stack(
				Dwarf_Addr frame_base_addr,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs/* = 0*/) const
		{
        	auto attrs = const_cast<with_dynamic_location_die *>(this)->get_attrs();
            assert(attrs.find(DW_AT_location) != attrs.end());
			return (Dwarf_Addr) dwarf::lib::evaluator(
				attrs.find(DW_AT_location)->second.get_loclist(),
				dieset_relative_ip // needs to be CU-relative
				 - (this->enclosing_compile_unit()->get_low_pc() ? 
				    this->enclosing_compile_unit()->get_low_pc()->addr : 0 ),
				this->get_ds().get_spec(),
				p_regs,
				frame_base_addr).tos();
		}
		Dwarf_Addr
		with_dynamic_location_die::calculate_addr_in_object(
				Dwarf_Addr object_base_addr,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs /*= 0*/) const
		{
        	auto attrs = const_cast<with_dynamic_location_die *>(this)->get_attrs();
            assert(attrs.find(DW_AT_data_member_location) != attrs.end());
			return (Dwarf_Addr) dwarf::lib::evaluator(
				attrs.find(DW_AT_data_member_location)->second.get_loclist(),
				dieset_relative_ip // needs to be CU-relative
				 - (this->enclosing_compile_unit()->get_low_pc() ? 
				 	this->enclosing_compile_unit()->get_low_pc()->addr : (Dwarf_Addr)0),
				this->get_ds().get_spec(),
				p_regs,
				object_base_addr, // ignored
				std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, object_base_addr))).tos();
		}
/* from spec::with_named_children_die */
        boost::shared_ptr<spec::basic_die>
        with_named_children_die::named_child(const std::string& name) 
        { 
			try
            {
            	for (auto current = this->get_first_child();
                		; // terminates by exception
                        current = current->get_next_sibling())
                {
                	if (current->get_name() 
                    	&& *current->get_name() == name) return current;
                }
            }
            catch (No_entry) { return NULL_SHARED_PTR(spec::basic_die); }
        }

        boost::shared_ptr<spec::basic_die> 
        with_named_children_die::resolve(const std::string& name) 
        {
            std::vector<std::string> multipart_name;
            multipart_name.push_back(name);
            return resolve(multipart_name.begin(), multipart_name.end());
        }

        boost::shared_ptr<spec::basic_die> 
        with_named_children_die::scoped_resolve(const std::string& name) 
        {
            std::vector<std::string> multipart_name;
            multipart_name.push_back(name);
            return scoped_resolve(multipart_name.begin(), multipart_name.end());
        }
/* from spec::compile_unit_die */
		opt<Dwarf_Unsigned> compile_unit_die::implicit_array_base() const
		{
			switch(this->get_language())
			{
				/* See DWARF 3 sec. 5.12! */
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99:
					return opt<Dwarf_Unsigned>(0UL);
				case DW_LANG_Fortran77:
				case DW_LANG_Fortran90:
				case DW_LANG_Fortran95:
					return opt<Dwarf_Unsigned>(1UL);
				default:
					return opt<Dwarf_Unsigned>();
			}
		}
		shared_ptr<type_die> compile_unit_die::implicit_enum_base_type() const
		{
			auto nonconst_this = const_cast<compile_unit_die *>(this);
			switch(this->get_language())
			{
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99: {
					const char *attempts[] = { "signed int", "int" };
					auto total_attempts = sizeof attempts / sizeof attempts[0];
					for (auto i_attempt = 0; i_attempt < total_attempts; ++i_attempt)
					{
						auto found = nonconst_this->named_child(attempts[i_attempt]);
						if (found) return dynamic_pointer_cast<type_die>(found);
					}
					assert(false && "enum but no int or signed int");
				}
				default:
					return shared_ptr<type_die>();
			}
		}
/* from spec::type_die */
		opt<Dwarf_Unsigned> type_die::calculate_byte_size() const
		{
			//return opt<Dwarf_Unsigned>();
			if (this->get_byte_size()) return *this->get_byte_size();
			else return opt<Dwarf_Unsigned>();
		}
		boost::shared_ptr<type_die> type_die::get_concrete_type() const
		{
			// by default, our concrete self is our self
			return boost::dynamic_pointer_cast<type_die>(
				const_cast<type_die *>(this)->get_ds()[this->get_offset()]);
		} 
		boost::shared_ptr<type_die> type_die::get_concrete_type()
		{
			return const_cast<const type_die *>(this)->get_concrete_type();
		}
		boost::shared_ptr<type_die> type_die::get_unqualified_type() const
		{
			// by default, our unqualified self is our self
			return boost::dynamic_pointer_cast<type_die>(
				const_cast<type_die *>(this)->get_ds()[this->get_offset()]);
		} 
/* from spec::qualified_type_die */
		boost::shared_ptr<type_die> qualified_type_die::get_unqualified_type() const
		{
			// for qualified types, our unqualified self is our get_type, recursively unqualified
			if (!this->get_type()) return shared_ptr<type_die>();
			return this->get_type()->get_unqualified_type();
		} 
		boost::shared_ptr<type_die> qualified_type_die::get_unqualified_type()
		{
			return const_cast<const qualified_type_die *>(this)->get_unqualified_type();
		}
/* from spec::type_chain_die */
		opt<Dwarf_Unsigned> type_chain_die::calculate_byte_size() const
		{
			// Size of a type_chain is always the size of its concrete type
			// which is *not* to be confused with its pointed-to type!
			if (this->get_concrete_type()) return this->get_concrete_type()->calculate_byte_size();
			else return opt<Dwarf_Unsigned>();
		}
        boost::shared_ptr<type_die> type_chain_die::get_concrete_type() const
        {
        	// pointer and reference *must* override us -- they do not follow chain
        	assert(this->get_tag() != DW_TAG_pointer_type
            	&& this->get_tag() != DW_TAG_reference_type);

            if (!this->get_type()) 
            {
            	return //boost::dynamic_pointer_cast<type_die>(get_this()); // broken chain
					boost::shared_ptr<type_die>();
            }
            else return const_cast<type_chain_die*>(this)->get_type()->get_concrete_type();
        }
/* from spec::pointer_type_die */  
        boost::shared_ptr<type_die> pointer_type_die::get_concrete_type() const 
        {
        	return boost::dynamic_pointer_cast<pointer_type_die>(get_this()); 
        }
        opt<Dwarf_Unsigned> pointer_type_die::calculate_byte_size() const 
        {
			if (this->get_byte_size()) return this->get_byte_size();
			else return this->enclosing_compile_unit()->get_address_size();
        }
/* from spec::reference_type_die */  
        boost::shared_ptr<type_die> reference_type_die::get_concrete_type() const 
        {
        	return boost::dynamic_pointer_cast<reference_type_die>(get_this()); 
        }
        opt<Dwarf_Unsigned> reference_type_die::calculate_byte_size() const 
        {
			if (this->get_byte_size()) return this->get_byte_size();
			else return this->enclosing_compile_unit()->get_address_size();
        }
/* from spec::array_type_die */
		opt<Dwarf_Unsigned> array_type_die::element_count() const
        {
        	assert(this->get_type());
            opt<Dwarf_Unsigned> count;
            
			try
            {
				for (auto child = this->get_first_child(); ; child = child->get_next_sibling())
                {
                	if (child->get_tag() == DW_TAG_subrange_type)
                    {
            	        auto subrange = boost::dynamic_pointer_cast<subrange_type_die>(child);
                        if (subrange->get_count()) 
                        {
                        	count = *subrange->get_count();
                            break;
                        }
                        else
                        {
                	        Dwarf_Unsigned lower_bound;
                            if (subrange->get_lower_bound()) lower_bound = *subrange->get_lower_bound();
                            else if (subrange->enclosing_compile_unit()->implicit_array_base())
                            {
                            	lower_bound = *subrange->enclosing_compile_unit()->implicit_array_base();
                            }
                            else break; // give up
                            if (!subrange->get_upper_bound()) break; // give up
                            
                            opt<Dwarf_Unsigned> upper_bound_optional = 
                            	static_cast<opt<Dwarf_Unsigned> >(
                                	subrange->get_upper_bound());

                            assert(*subrange->get_upper_bound() < 10000000); // detects most garbage
                            Dwarf_Unsigned upper_bound = *subrange->get_upper_bound();
                            count = upper_bound - lower_bound + 1;
                        }
                    } // end if it's a subrange type
                    /* FIXME: also handle enumeration types, because
                     * DWARF allows array indices to be specified by
                     * an enumeration as well as a subrange. */
                } // end for
            } // end try
            catch (No_entry) {} // termination of loop
            
            return count;
    	}

        opt<Dwarf_Unsigned> array_type_die::calculate_byte_size() const
        {
        	assert(this->get_type());
            opt<Dwarf_Unsigned> count = this->element_count();
            opt<Dwarf_Unsigned> calculated_byte_size
             = this->get_type()->calculate_byte_size();
            if (count && calculated_byte_size) return *count * *calculated_byte_size;
            else return opt<Dwarf_Unsigned>();
		}
		
		shared_ptr<type_die> array_type_die::ultimate_element_type() const
		{
			auto nonconst_this = const_cast<array_type_die *>(this);
			shared_ptr<type_die> cur
			 = dynamic_pointer_cast<type_die>(nonconst_this->shared_from_this());
			while (cur->get_concrete_type()
				 && cur->get_concrete_type()->get_tag() == DW_TAG_array_type)
			{
				cur = dynamic_pointer_cast<array_type_die>(cur->get_concrete_type())->get_type();
			}
			return cur;
		}
		
/* from spec::structure_type_die */
		opt<Dwarf_Unsigned> structure_type_die::calculate_byte_size() const
		{
			// HACK: for now, do nothign
			// We should make this more reliable,
			// but it's difficult because overall size of a struct is
			// language- and implementation-dependent.
			return this->type_die::calculate_byte_size();
		}
/* from spec::with_data_members_die */
		shared_ptr<type_die> with_data_members_die::find_my_own_definition() const
		{
			auto nonconst_this = const_cast<with_data_members_die *>(this);
			if (!get_declaration() || !*get_declaration()) 
			{
				return dynamic_pointer_cast<type_die>(get_this());
			}
			cerr << "Looking for definition of declaration " << this->summary() << endl;
			
			// if we don't have a name, we have no way to proceed
			if (!get_name()) goto return_no_result;
			else
			{
				string my_name = *get_name();

				/* Otherwise, we search forwards from our position, for siblings
				 * that have the same name but no "declaration" attribute. */
				auto iter = nonconst_this->iterator_here();
				abstract_dieset::position_and_path pos_and_path = iter.base();

				/* Are we a CU-toplevel DIE? We only handle this case at the moment. */
				auto cu_pos_and_path
				 = nonconst_this->enclosing_compile_unit()->iterator_here().base();
				if (cu_pos_and_path.path_from_root.size() != pos_and_path.path_from_root.size() - 1)
				{
					goto return_no_result;
				}

				abstract_dieset::iterator i_sibling(pos_and_path, 
					abstract_dieset::siblings_policy_sg);

				for (++i_sibling /* i.e. don't check ourselves */; 
					i_sibling != nonconst_this->get_ds().end(); ++i_sibling)
				{
					auto p_d = dynamic_pointer_cast<with_data_members_die>(*i_sibling);
					if (p_d && p_d->get_name() && *p_d->get_name() == my_name
						&& (!p_d->get_declaration() || !*p_d->get_declaration()))
					{
						cerr << "Found definition " << p_d->summary() << endl;
						return dynamic_pointer_cast<type_die>(p_d);
					}
				}
			}
		return_no_result:
			cerr << "Failed to find definition of declaration " << this->summary() << endl;
			return shared_ptr<type_die>();
		}
/* from spec::variable_die */        
		bool variable_die::has_static_storage() const
        {
			auto nonconst_this = const_cast<variable_die *>(this);
            if (nonconst_this->nearest_enclosing(DW_TAG_subprogram))
            {
            	// we're either a local or a static -- skip if local
                auto attrs = nonconst_this->get_attrs();
            	if (attrs.find(DW_AT_location) != attrs.end())
                {
                	// HACK: only way to work out whether it's static
                    // is to test for frame-relative addressing in the location
                    // --- *and* since we can't rely on the compiler to generate
                    // DW_OP_fbreg for every frame-relative variable (since it
                    // might just use %ebp or %esp directly), rule out any
                    // register-relative location whatsoever. FIXME: this might
                    // break some code on segmented architectures, where even
                    // static storage is recorded in DWARF using 
                    // register-relative addressing....
                    auto loclist = attrs.find(DW_AT_location)->second.get_loclist();
                    for (auto i_loc_expr = loclist.begin(); i_loc_expr != loclist.end(); ++i_loc_expr)
                    {
                    	for (auto i_instr = i_loc_expr->begin(); 
                        	i_instr != i_loc_expr->end();
                            ++i_instr)
                        {
                        	if (this->get_spec().op_reads_register(i_instr->lr_atom)) return false;
                        }
                    }
                }
            }
            return true;
		}
/* from spec::member_die */        
		opt<Dwarf_Unsigned> 
        member_die::byte_offset_in_enclosing_type() const
        {
        	auto nonconst_this = const_cast<member_die *>(this); // HACK: eliminate
        
        	auto enclosing_type_die = boost::dynamic_pointer_cast<type_die>(
            	this->get_parent());
            if (!enclosing_type_die) return opt<Dwarf_Unsigned>();
            
			if (!this->get_data_member_location())
			{
				// if we don't have a location for this field,
				// we tolerate it iff it's the first one in a struct/class
				// OR contained in a union
                // HACK: support class types (and others) here
				if (
				(  (enclosing_type_die->get_tag() == DW_TAG_structure_type ||
					enclosing_type_die->get_tag() == DW_TAG_class_type)
                 && /*static_cast<abstract_dieset::position>(*/nonconst_this->iterator_here().base()/*)*/ == 
                 	/*static_cast<abstract_dieset::position>(*/
                    	boost::dynamic_pointer_cast<structure_type_die>(enclosing_type_die)
                 			->member_children_begin().base().base().base()/*)*/
				) || enclosing_type_die->get_tag() == DW_TAG_union_type)
				
				{
					return opt<Dwarf_Unsigned>(0U);
				}
				else 
				{
					// error
					std::cerr << "Warning: encountered DWARF type lacking member locations: "
						<< *enclosing_type_die << std::endl;
					return opt<Dwarf_Unsigned>();
				}
			}
			else if (this->get_data_member_location()->size() != 1)
			{
				// error
				std::cerr << "Warning: encountered DWARF type with member locations I didn't understand: "
					<< *enclosing_type_die << std::endl;
				return opt<Dwarf_Unsigned>();
			}
			else
			{
				return dwarf::lib::evaluator(
					this->get_data_member_location()->at(0), 
					this->get_ds().get_spec(),
					std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, 0UL))).tos();
			}
        }
/* from spec::inheritance_die */
		opt<Dwarf_Unsigned> 
        inheritance_die::byte_offset_in_enclosing_type() const
        {
        	// FIXME
            return opt<Dwarf_Unsigned>();
        }
    }
    namespace lib
    {
/* from lib::basic_die */
//         basic_die::basic_die(dieset& ds, boost::shared_ptr<lib::die> p_d)
//          : p_d(p_d), p_ds(&ds)
//         {
//         	/*if (p_d)
//             {
// 	        	Dwarf_Half ret; p_d->tag(&ret);
//     	        if (ret == DW_TAG_compile_unit) m_parent_offset = 0UL;
//                 else m_parent_offset = p_ds->find_parent_offset_of(this->get_offset());
//             }
//             else*/ m_parent_offset = 0UL;
//         }

		/* This constructor is used by (friend) file_toplevel_die only.
		 */
		// super-special: "no Dwarf_Die"
		basic_die::basic_die(dieset& ds, int dummy)
		 : die((assert(ds.p_f), *ds.p_f), dummy), p_ds(&ds) {}

		// "first DIE in current CU context"
		basic_die::basic_die(dieset& ds)
		 : die((assert(ds.p_f), *ds.p_f)), p_ds(&ds) {}

		// "next sibling"
		basic_die::basic_die(dieset& ds, shared_ptr<basic_die> p_d)
		 : lib::die(p_d->f, *p_d), p_ds(&ds), m_parent_offset(0UL) 
		{ assert(p_d->p_ds == &ds); }

		// "first child"
		basic_die::basic_die(shared_ptr<basic_die> p_d)
		 : lib::die(*p_d), p_ds(p_d->p_ds), m_parent_offset(0UL) {}

		// "specific offset"
		basic_die::basic_die(dieset& ds, Dwarf_Off off)
		 : lib::die(*ds.p_f, (assert(off != 0UL), off)), p_ds(&ds), m_parent_offset(0UL)
		{}
		 
		Dwarf_Off 
        basic_die::get_offset() const
        {
        	Dwarf_Off ret; this->offset(&ret); return ret;
        }
        
        Dwarf_Half 
        basic_die::get_tag() const
        {
        	Dwarf_Half ret; this->tag(&ret); return ret;
        }
        
		boost::shared_ptr<spec::basic_die> 
		basic_die::get_parent() 
		{
			// if we're toplevel, we have no parent
			if (this->get_offset() == 0UL) throw No_entry();
			
			// if we're a CU, our parent is toplevel
			Dwarf_Half ret; this->tag(&ret);
			// if we're not a CU, and have nonzero parent, it's cached
			if (ret != DW_TAG_compile_unit)
			{
				if (m_parent_offset == 0UL)
				{
					// not cached, so fill
					m_parent_offset = p_ds->find_parent_offset_of(this->get_offset());
					assert(m_parent_offset != 0UL);
				}
			} 
			else 
			{
				assert(m_parent_offset == 0UL); // ensure initialized
			}
			
			return p_ds->get(m_parent_offset);
		}

		/* Problem with inheriting from lib::die is that we don't know
		 * what type to construct until we've constructed a lib::die.
		 * So the factory in lib::dieset takes a const lib::die&,
		 * constructed as a temporary, and uses that. 
		 * Ideally we would handle/body-ify lib::die to avoid this
		 * allocation/deallocation overhead. */
        boost::shared_ptr<spec::basic_die> 
        basic_die::get_first_child() 
        {
			return p_ds->get(lib::die(*this));
        }

        boost::shared_ptr<spec::basic_die> 
        basic_die::get_next_sibling() 
        {
            return p_ds->get(lib::die(*p_ds->p_f, *this));
        }
        
        Dwarf_Off basic_die::get_first_child_offset() const
        //{ Dwarf_Off off; boost::make_shared<die>(*p_d)->offset(&off); return off; }
        //{ return const_cast<basic_die *>(this)->get_first_child()->get_offset(); }
		{ 
			Dwarf_Off off; 
			int retval; 
			return (retval = lib::die(*this).offset(&off), assert(retval == DW_DLV_OK), off);
		}

        Dwarf_Off basic_die::get_next_sibling_offset() const
        //{ Dwarf_Off off; boost::make_shared<die>(*p_ds->p_f, *p_d)->offset(&off); return off; }
        //{ return const_cast<basic_die *>(this)->get_next_sibling()->get_offset(); }
		{
			Dwarf_Off off;
			int retval;
			return (retval = lib::die(*p_ds->p_f, *this).offset(&off), 
				assert(retval == DW_DLV_OK), off);
		}
       
        opt<string> 
        basic_die::get_name() const
        {
 			string s;
			int retval = name(&s);
            if (retval != DW_DLV_OK) return opt<string>(); 
			return s;
        }
        
        const spec::abstract_def& 
        basic_die::get_spec() const
        {
        	return spec::dwarf3; // FIXME
        }

        const abstract_dieset& basic_die::get_ds() const
        {
        	return *p_ds;
        }
        abstract_dieset& basic_die::get_ds()
        {
        	return *p_ds;
        }
        
        std::map<Dwarf_Half, encap::attribute_value> basic_die::get_attrs() 
        {
        	std::map<Dwarf_Half, encap::attribute_value> ret;
            //if (this->p_d)
            //{
			    attribute_array arr(*const_cast<basic_die*>(this));
                int retval;
			    for (int i = 0; i < arr.count(); i++)
			    {
				    Dwarf_Half attr; 
				    dwarf::lib::attribute a = arr.get(i);
				    retval = a.whatattr(&attr);
					try
					{
				    	ret.insert(std::make_pair(attr, encap::attribute_value(this->get_ds(), a)));
					} catch (dwarf::lib::Not_supported)
					{
						/* This means we didn't recognise the attribute_value. Skip over it silently. */
						// we could print a warning....
					}
			    } // end for
            //}
            return ret;
        }

/* from lib::compile_unit_die */
		Dwarf_Half compile_unit_die::get_address_size() const
		{
			/* We can't easily add/initialize new fields to the compile_unit_die.
			 * So instead, ask the file_toplevel_die. */
			//auto parent = const_cast<const compile_unit_die *>(this)->get_parent();
			// HACK: why can't I use get_parent() const ?
			auto parent_toplevel = boost::dynamic_pointer_cast<file_toplevel_die>(
				const_cast<compile_unit_die*>(this)->get_parent());
			return parent_toplevel->get_address_size_for_cu(
				boost::const_pointer_cast<compile_unit_die>(
					dynamic_pointer_cast<const compile_unit_die>(
						shared_from_this())));
		}
		
		std::string compile_unit_die::source_file_name(unsigned o) const
		{
			/* We have the same problem here: we want to get per-CU
			 * data from libdwarf, but where to store it? Use the toplevel. */
			auto nonconst_this = const_cast<compile_unit_die *>(this);
			auto nonconst_toplevel = dynamic_pointer_cast<lib::file_toplevel_die>(
				nonconst_this->get_ds().toplevel());
			return nonconst_toplevel->source_file_name_for_cu(
					dynamic_pointer_cast<compile_unit_die>(nonconst_this->shared_from_this()), 
					o);
		}
		
		unsigned compile_unit_die::source_file_count() const
		{
			auto nonconst_this = const_cast<compile_unit_die *>(this);
			auto nonconst_toplevel = dynamic_pointer_cast<lib::file_toplevel_die>(
				nonconst_this->get_ds().toplevel());
			return nonconst_toplevel->source_file_count_for_cu(
					dynamic_pointer_cast<compile_unit_die>(nonconst_this->shared_from_this()));
		}

		
/*		Dwarf_Unsigned 
        compile_unit_die::get_language() const
        {
        	Dwarf_Bool ret;
            assert((p_d->hasattr(DW_AT_language, &ret), ret));
            return DW_LANG_C; // FIXME
        }*/

/* from lib::subprogram_die */
/*       	opt<boost::shared_ptr<spec::basic_die> > 
        subprogram_die::get_type() const
        {
        	Dwarf_Off type_off;
            lib::attribute_array arr(*p_d);
            lib::attribute attr = arr[DW_AT_type];
            encap::attribute_value val(*p_ds, attr);
            type_off = val.get_ref().off;
        	return (*p_ds)[type_off];
        }*/

/* from lib::dieset */
//         std::deque< abstract_dieset::position >
//         dieset::path_from_root(Dwarf_Off off)
//         {
//         	/* This is an iterative, depth-first walk of a tree with no
//              * up-links. 
//              * We keep a list of our current path from the root. */
//         	std::deque< position > current_path;
//             
//             /* We can't use the factory in this function because factory-constructed
//              * classes try to recover their parent in the constructur, which calls
//              * this method and sets up an infinite loop. */
//             boost::shared_ptr<lib::die> first_child = boost::make_shared<die>(*p_f);
//             Dwarf_Off first_child_offset; first_child->offset(&first_child_offset);
// 	            //toplevel()->get_first_child();
//             //boost::make_shared<die>(*p_f);
//             abstract_dieset::position tmp = {this, first_child_offset};
//             current_path.push_back(tmp);
//             
// 			boost::shared_ptr<lib::die> current = first_child;
//             while (true)
//             {
//                 /* Keep on trying to move down */
//                 while (true)
//                 {
//                     boost::shared_ptr<lib::die> lower;
//                     boost::shared_ptr<lib::die> next;
//                     try
//                     {
//                 	    //lower = boost::make_shared<die>(*current);
//                         //lower = current->get_first_child();
//                         // *** test begins here
//                         //Dwarf_Off lower_offset;
//                         //lower->offset(&lower_offset);
//                         Dwarf_Off lower_offset = current->get_first_child_offset();
//                        	abstract_dieset::position tmp = {this, lower_offset};
// 						current_path.push_back(tmp);
//                         
//                         if (lower_offset == off) 
//                         {
// 	                    	return current_path;
//                         }
//                         // *** test ends here
//                         current = current->get_first_child();
//                     }
//                     catch (lib::No_entry)
//                     {
//                 	    /* If that doesn't work, try moving to the next sibling 
//                          * If *that* doesn't work, retreat up a level and try again.
//                          * We terminate if we've retreated up to the top.
//                          * Retreating up to the top
//                          */
//                         do
//                         {
//                             try
//                             {
// 	                            //boost::make_shared<die>(*p_f, *current);
//                                 //next = current->get_next_sibling();
//                                 Dwarf_Off next_offset = current->get_next_sibling_offset();
// 			                    abstract_dieset::position tmp = {this, next_offset};
//                                 current_path.push_back(tmp);
//                                 if (next_offset == off)
//                                 {
//                                     return current_path;
//                                 }
//                                 break;
//                             }
//                             catch (lib::No_entry) 
//                             {
//                             	if (current_path.size() == 0)
//                                 {
//                                 	assert(false); // didn't find the parent!
//                                 }
//                             	//current = current_path.at(current_path.size() - 1);
//                                 //current = boost::make_shared<die>(*p_f, 
//                                 //	current_path.at(current_path.size() - 1).off);
//                                 current = current->get_next_sibling();
//                                 current_path.pop_back();
//                             } // continue
// 	                    } while (true);
//                     }
//                 }
//             }
//         }
        abstract_dieset::path_type dieset::path_from_root(Dwarf_Off off)
        {
//             std::deque< spec::abstract_dieset::position > /*reverse_*/path;
//             auto cur = (*this)[off];
//             spec::abstract_dieset::position tmp = {this, cur->get_offset()};
// 			/*reverse_*/path.push_back(tmp);
//             while (cur->get_offset() != 0UL)
//             {
//                 cur = cur->get_parent();
//    		        spec::abstract_dieset::position tmp = {this, cur->get_offset()};
//                 /*reverse_*/path./*push_back*/push_front(tmp);
//             }
//             //std::reverse(reverse_path.begin(), reverse_path.end());
//             return /*reverse_*/path;
			return this->find(off).base().path_from_root;
        }

	}
    namespace spec
    {
        abstract_dieset::iterator abstract_dieset::begin(policy& pol)
		{ auto i_begin = begin(); return iterator(i_begin, pol); }
        abstract_dieset::iterator abstract_dieset::end(policy& pol)
		{ auto i_end = end(); return iterator(i_end, pol); }
	
        abstract_dieset::basic_iterator_base::basic_iterator_base(
        	abstract_dieset& ds, Dwarf_Off off,
            const path_type& path_from_root,
            policy& pol)
        : position_and_path((position){&ds, off}, path_from_root), 
          //path_from_root(path_from_root),
          p_policy(&pol) 
        { 
        	/* We've been given an offset but no path. So search for the 
             * offset, which will give us the path. */
        	if (off != 0UL && 
            	off != std::numeric_limits<Dwarf_Off>::max() &&
                path_from_root.size() == 0) this->path_from_root = ds.find(off).path();
            canonicalize_position(); 
        }
		
		/* Partial order on iterators -- these are comparable when they share a parent. */
		bool
		abstract_dieset::iterator::shares_parent_pos(const abstract_dieset::iterator& i) const
		{ return this->base().p_ds == i.base().p_ds
			&& 
			this->dereference()->get_parent()->get_offset() 
			== i.dereference()->get_parent()->get_offset();
		}
		bool
		abstract_dieset::iterator::operator<(const iterator& i) const
		{ return this->shares_parent_pos(i) && 
		  this->dereference()->get_offset() < i.dereference()->get_offset(); 
		}

        bool 
		file_toplevel_die::is_visible::operator()(
			boost::shared_ptr<spec::basic_die> p) const
        {
            auto p_el = boost::dynamic_pointer_cast<program_element_die>(p);
            if (!p_el) return true;
            return !p_el->get_visibility() 
                || *p_el->get_visibility() != DW_VIS_local;
        }
//                 bool operator()(Die_encap_base& d) const
//                 {
//                     try
//                     {
//                         Die_encap_is_program_element& el = 
//                             dynamic_cast<Die_encap_is_program_element&>(d);
//                         return !el.get_visibility() 
//                             || *el.get_visibility() != DW_VIS_local;
//                     } catch (std::bad_cast e) { return true; }
//                 }

		shared_ptr<basic_die>
		file_toplevel_die::visible_named_grandchild(
			const std::string& name
		)
		{
			auto returned = visible_named_grandchild_pos(name);
			if (returned)
			{
				auto iter = abstract_dieset::iterator(returned->first);
				if (iter != get_ds().end()) return *iter;
			}
			return shared_ptr<basic_die>();
		}
		
		optional<file_toplevel_die::cache_rec_t>
		file_toplevel_die::visible_named_grandchild_pos(
			const std::string& name,
			optional<cache_rec_t> opt_start_here, /* = no value */
			shared_ptr<visible_grandchildren_sequence_t> opt_seq
		)
		{
			/* NOTE: 
			 * There are some tricky semantic requirements here.
			 * 1. the vectors in the cache are kept in grandchild order (not offset order!);
			 * 2. opt_start_here, if it is set, must point to an existing match.
			      This is so that we can ensure we return the next match in grandchild
			      order, which is not the same as offset order.
			 */
			
			/* Cache invalidation: 
			 * to avoid the fragile requirement that any modifications
			 * invalidate the relevant portion of the cache directly, 
			 * we simply keep a stamp of "max offset when last exhaustively searched cache"
			 * and if the max offset has changed in the meantime, we invalidate.
			 * BUT this doesn't handle the case where a DIE's name has changed!
			 * AND we can't get a decent highest-DIE number for lib::dieset.
			 * For now, assume that we don't delete DIE names. So, a positive result
			 * is never wrong. BUT what if it misses an intervening entry recently added?
			 * FIXME: whenever we set the name on a CU-toplevel DIE,
			 * invalidate the cache for its name.
			 * */
			shared_ptr<visible_grandchildren_sequence_t> vg_seq
			 = opt_seq ? opt_seq : visible_grandchildren_sequence();

			// first cheque the cash
			auto found_in_cache = visible_grandchildren_cache.find(name);
			if (found_in_cache != visible_grandchildren_cache.end())
			{
				clog << "Hit cache..." << endl;
				// two cases: we find a positive result in the cache...
				if (found_in_cache->second)
				{
					clog << "Cache hit is positive" << endl;
					auto& vec = *found_in_cache->second;
					assert(vec.size() != 0);
					if (opt_start_here)
					{
						// locate previous match, and return the next one (if any, else keep searching)
						cache_rec_t previous_match = //(**opt_start_here)->get_offset();
							//make_pair(
							//	opt_start_here->base().base(), 
							//	opt_start_here->base().m_currently_in
							//);
							*opt_start_here;
						abstract_dieset::iterator startpos(previous_match.first);
						clog << "Looking in cache, starting from " << (*startpos)->summary() << endl;
						auto found_previous = std::find(vec.begin(), vec.end(), previous_match);
						if (found_previous == vec.end())
						{
							// this means we didn't find the previous match in the cache
							// -- our caller passed us a bogus opt_start_here
							assert(false);
						}
						else if (found_previous + 1 != vec.end())
						{
							auto found_rec = *(found_previous + 1);
							clog << "Returning cached match " 
								<< (*abstract_dieset::iterator(found_rec.first))->summary() << endl;
							return found_rec; //*abstract_dieset::iterator(found_rec.first); //(this->get_ds())[found_off];
						}
						else // found_previous + 1 == vec.end()
						{
							// this means that we haven't cached a later result
							// there may or may not be one
							// so we continue searching
							clog << "Cache has nothing after startpos; searching onward" << endl;
							goto search_onward;
						}

					}
					else
					{
						// we are free just to return the first match
						clog << "No starting pos, so returning first cached match" << endl;
						auto found_rec = *vec.begin();
						//return (this->get_ds())[found_off];
						return found_rec; // *abstract_dieset::iterator(found_rec.first);
					}
				}
				else /* negative result in cache */
				{
					clog << "Cache hit is negative" << endl;
					// do we trust the negative result?
					if (max_offset_on_last_complete_search 
						== this->get_ds().highest_offset_upper_bound())
					{
						cerr << "Hit cached negative result for " << name << endl;
						//return optional<cache_rec_t>(); // shared_ptr<basic_die>();
						goto return_no_entry;
					}
					// else we will do the lookup afresh
					else 
					{
						clog << "Disregarding stale negative cache hit" << endl;
						goto search_onward;
					}
				}
			}
			/* We could short-circuit the search for nonexistent DIEs here, 
			 * by using cache_is_exhaustive_before_offset. BUT 
			 * if we're a lib::file_toplevel_die, we don't know
			 * our own end offset! encap::file_toplevel_die can do better,
			 * so we call through a virtual method to do the check. */
// 			if (this->get_ds().highest_offset_upper_bound()
// 				<= this->get_ds().get_last_monotonic_offset()
// 				&&
// 				cache_is_exhaustive_up_to_offset 
// 				>= this->get_ds().highest_offset_upper_bound())


// 			if (!opt_start_here
// 				&& max_offset_on_last_complete_search 
// 					>= this->get_ds().highest_offset_upper_bound())
// 			{
// 				cerr << "Don't bother searching for " << name << " because cache is exhaustive "
// 					"up to offset 0x" << std::hex << cache_is_exhaustive_up_to_offset << std::dec
// 					<< " and there are no nonmonotonic DIEs."
// 					<< endl;
// 					
// 				return shared_ptr<basic_die>();
// 			}

			{
			search_onward:
				Dwarf_Off last_seen_offset = 0UL;
				//if (opt_start_here) vg_seq = dynamic_pointer_cast<visible_grandchildren_sequence_t>(
				//	opt_start_here->base().get_sequence());

				/* NOTE: vg_seq does not necessarily go in
				 * strictly ascending order by offset. */
				visible_grandchildren_iterator start_iter
				 = opt_start_here ? ++(vg_seq->at(opt_start_here->first, opt_start_here->second))
			                	  : vg_seq->begin();
				
				clog << "Linear search startpos is "
					<< ((start_iter == vg_seq->end()) ? "(end of dieset)" : (*start_iter)->summary())
					<< endl;

				for (auto i_vg = start_iter;
					i_vg != vg_seq->end(); 
					last_seen_offset = (*i_vg)->get_offset(), ++i_vg)
				{
					Dwarf_Off cur_off = (*i_vg)->get_offset();
					assert(cur_off > last_seen_offset
						|| cur_off > this->get_ds().get_last_monotonic_offset());
					if ((*i_vg)->get_name())
					{
						string cur_name = *(*i_vg)->get_name();
						if (cur_name == name)
						{
							// ensure we have a vector in the cache to write to
							if (!visible_grandchildren_cache[cur_name]) 
							{
								visible_grandchildren_cache[cur_name] = vector<cache_rec_t>();
							}
							cache_rec_t cache_ent_added
							 = make_pair(i_vg.base().base().base(), i_vg.base().get_currently_in());
							//clog << "Traversing cacheable entry " << (*i_vg)->summary() << endl;

							// we should not be adding something we've added already
							/*if*/ assert( (std::find(visible_grandchildren_cache[cur_name]->begin(),
									visible_grandchildren_cache[cur_name]->end(), 
									cache_ent_added) == visible_grandchildren_cache[cur_name]->end()));
							//{
							//	clog << "Cacheable entry is not already cached, so adding it." << endl;
								visible_grandchildren_cache[cur_name]->push_back(
									cache_ent_added
								);
							//}
						//if (cur_name == name)
						//{
						
							assert(cache_is_exhaustive_up_to_offset < cur_off
							||     cache_is_exhaustive_up_to_offset > this->get_ds().get_last_monotonic_offset());
							if (!opt_start_here && cur_off > this->get_ds().get_last_monotonic_offset())
							{ cache_is_exhaustive_up_to_offset = cur_off; }
							clog << "Search succeeded at " << (*i_vg)->summary() << endl;
							return optional<cache_rec_t>(cache_ent_added);
						}
					}
				}
				if (!opt_start_here) 
				{
					// we use last_seen_offset and not the end() offset (max()) 
					// because DIEs might get added later, at higher offsets
					//  than the current max, but less than the sentinel.
					cache_is_exhaustive_up_to_offset = last_seen_offset;

					// we can also store a negative result if we searched all the way
					cerr << "Installing negative cache result for " << name << endl;
					visible_grandchildren_cache[name] = optional< vector<cache_rec_t> >();

					// timestamp this search
					// HACK: "upper bound" is not appropriate here, but it'll do for now
					max_offset_on_last_complete_search = this->get_ds().highest_offset_upper_bound();
				}
				cerr << "Linear search for " << name << " went all the way to the end." << endl;
			} // end convenience lexical block
		return_no_entry:
			return optional<cache_rec_t>(
				make_pair(get_ds().end().base(), vg_seq->subsequences_count() - 1)
			);
		}
		
		boost::shared_ptr<file_toplevel_die::grandchildren_sequence_t>
		file_toplevel_die::grandchildren_sequence()
		{
			auto seq = boost::make_shared<grandchildren_sequence_t>();
			for (auto i_cu = this->compile_unit_children_begin();	
				i_cu != this->compile_unit_children_end();
				++i_cu)
			{
				seq->append((*i_cu)->children_begin(), (*i_cu)->children_end());
				
				//auto appended_size = srk31::count((*i_cu)->children_begin(), (*i_cu)->children_end());
				//auto new_size = srk31::count(seq->begin(), seq->end());
				//cerr << "Appended " << appended_size << " elements." << endl;
				//cerr << "New size: " << new_size << " elements." << endl;
			}
			return seq;
		}
		
		shared_ptr<file_toplevel_die::visible_grandchildren_sequence_t> 
		file_toplevel_die::visible_grandchildren_sequence()
		{
			return boost::shared_ptr<visible_grandchildren_sequence_t>(
				new visible_grandchildren_sequence_t(
					*grandchildren_sequence()
				)
			);
		}

    } // end namespace spec
    namespace lib
    {
		/* WARNING: only use find() when you're not sure that a DIE exists
		 * at offset off. Otherwise use operator[]. */
		abstract_dieset::iterator 
		dieset::find(Dwarf_Off off)  
		{
			//abstract_dieset::iterator i = this->begin();
			//while (i != this->end() && i.pos().off != off) i++;
			//return i;
			
			/* We can do better than linear search, using the properties of
			 * DWARF offsets.  */
			boost::shared_ptr<spec::basic_die> current = this->toplevel();
			spec::abstract_dieset::path_type path_from_root(1, (position){this, 0UL}); // start with root node only
			/* We do this because iterators can point at the root, and all iterators should 
			 * have the property that the last element in the path is their current node. */
			 
			while (current->get_offset() != off)
			{
				// we haven't reached our target yet, so walk through siblings...
				auto child = current->get_first_child(); // index for loop below
				/* Note that we may throw No_entry here ^^^ -- this happens
				 * if and only if our search has failed, which is what we want. */
				boost::shared_ptr<spec::basic_die> prev_child; // starts at 0
				
				// ...linear search for the child we should accept or descend to
				// (NOTE: if they first child is the one we want, we'll go round once,
				// until that is now prev_child and child is the next (if any)
				while (child && !(child->get_offset() > off))
				{
					// keep going
					prev_child = child;
					try
					{
						child = child->get_next_sibling();
					} 
					catch (No_entry)
					{
						// reached the last sibling
						child = boost::shared_ptr<spec::basic_die>();
					}
				}
				// on terminating this loop: child is *after* the one we want, or null
				// prev_child is either equal to it, or is an ancestor of it
				assert(prev_child && (!child || child->get_offset() > off)
					&& prev_child->get_offset() <= off);
					
				current = prev_child; // we either terminate on this child, or descend under it
				// ... either way, remember its position
				// (sanity check: make sure we're not doubling up a path entry)
				assert(path_from_root.size() != 0 &&
					(path_from_root.back() != (position){this, prev_child->get_offset()}));
				path_from_root.push_back((position){this, prev_child->get_offset()});
			}
			
			return abstract_dieset::iterator((position){this, off}, path_from_root);
		}
        
        abstract_dieset::iterator 
        dieset::begin() 
        {
    		return abstract_dieset::iterator(*this, 0UL, 
            	path_type(1, (position){this, 0UL}));
        }
        
        abstract_dieset::iterator 
        dieset::end() 
        {
	        return abstract_dieset::iterator(*this, 
            	std::numeric_limits<Dwarf_Off>::max(),
                path_type());
		} 
        
		Dwarf_Off 
		dieset::find_parent_offset_of(Dwarf_Off off)
		{
			if (off == 0UL) throw No_entry();
			//else if (
			if (this->parent_cache.find(off) != this->parent_cache.end())
			{
				return this->parent_cache[off];
			}
			
			// NOTE: we use find() so that we get the path not just die ptr
			auto path = this->find(off).path();
			assert(path.back().off == off);
			if (path.size() > 1) 
			{ 
				position parent_pos = *(path.end() - 2);
				this->parent_cache[off] = parent_pos.off; // our answer is the *new* last element
				return parent_pos.off;
			}
			else throw lib::No_entry();
		} 

        boost::shared_ptr<basic_die> 
        dieset::find_parent_of(Dwarf_Off off)
        {
        	return get(find_parent_offset_of(off));
        }

        boost::shared_ptr<lib::basic_die> 
        dieset::get(Dwarf_Off off)
        {
        	if (off == 0UL) return boost::dynamic_pointer_cast<lib::basic_die>(toplevel());
			return get(lib::die(*p_f, off)); // "offdie" constructor
		}
			
// //             else
// //             {
// //             	boost::shared_ptr<die> p_tmp = boost::make_shared<die>(*p_f, off);
// // 	            return get(p_tmp);
// //             }
// 			/* This code originally made an ADT-style die out of an lib::die. 
// 			 * We just want to use inheritance for this. 
// 			 * FIXME: why did I take this make-shared-make-shared approach? Any good reason?
// 			 * HACK: for now, just do the old thing! */
//             Dwarf_Half tag;
//             //Dwarf_Off off;
// 			boost::shared_ptr<die> p_d = boost::make_shared<die>(*p_f, off);
//             //int ret = p_d->offset(&off); assert(ret == DW_DLV_OK);
//             switch(p_d->tag(&tag), tag)
//             {
// #define factory_case(name, ...) \
// case DW_TAG_ ## name: return boost::make_shared<lib:: name ## _die >(*this, p_d);
// #include "dwarf3-factory.h"
// #undef factory_case
//                 default: return boost::make_shared<lib::basic_die>(*this, p_d);
//             }
// 
//         }
		
		/* factory method -- this is private */
		boost::shared_ptr<lib::basic_die>
		dieset::get(const lib::die& d)
		{
            Dwarf_Half tag;
			Dwarf_Off offset;
			int ret;
			ret = d.offset(&offset); assert(ret == DW_DLV_OK);

			// to catch weird bug where d.f was null
			assert(&d.f);
			
            switch(ret = d.tag(&tag), assert(ret == DW_DLV_OK), tag)
            {
#define factory_case(name, ...) \
case DW_TAG_ ## name: return dynamic_pointer_cast<basic_die>(my_make_shared<lib:: name ## _die >(*this, offset));
#include "dwarf3-factory.h"
#undef factory_case
                default: return my_make_shared<lib::basic_die>(*this, offset);
            }
		 
		}
        
//         boost::shared_ptr<lib::basic_die>
//         dieset::get(boost::shared_ptr<die> p_d)
//         {
//         	/* Given a raw die, we make, and return, a basic_die
//              * (offering our ADT interface)
//              * or one of its children. */
//             Dwarf_Half tag;
//             Dwarf_Off off;
//             int ret = p_d->offset(&off); assert(ret == DW_DLV_OK);
//             switch(p_d->tag(&tag), tag)
//             {
// #define factory_case(name, ...) \
// case DW_TAG_ ## name: return boost::make_shared<lib:: name ## _die >(*this, p_d);
// #include "dwarf3-factory.h"
// #undef factory_case
//                 default: return boost::make_shared<lib::basic_die>(*this, p_d);
//             }
//         }
        
        boost::shared_ptr<spec::basic_die> dieset::operator[](Dwarf_Off off) const
        {
        	if (off == 0UL) return boost::shared_ptr<spec::basic_die>(m_toplevel);
            return const_cast<dieset *>(this)->get(off);
        }
		
        boost::shared_ptr<spec::file_toplevel_die> dieset::toplevel()
        {
        	return boost::dynamic_pointer_cast<spec::file_toplevel_die>(m_toplevel);
        }
        
//         encap::rangelist dieset::rangelist_at(Dwarf_Unsigned i) const
//         {
//         	return encap::rangelist(this->p_f->get_ranges(), i);
//         }
    }
    namespace spec
    {
        //abstract_dieset::children_policy::children_policy(
        
        abstract_dieset::default_policy abstract_dieset::default_policy_sg;
		abstract_dieset::dummy_policy abstract_dieset::dummy_policy_sg;
        // depth-first traversal
        int abstract_dieset::default_policy::increment(position& pos,
        	path_type& path)
        {
#define print_path std::cerr << "Path is now: "; for (unsigned i = 0; i < path.size(); i++) std::cerr << ((i > 0) ? ", " : "") << path.at(i).off; std::cerr << std::endl
            assert(path.size() == 0 || path.back().off == pos.off);
            // if we have children, descend there...
            try
            {
            	pos.off = (*pos.p_ds)[pos.off]->get_first_child_offset();//->get_offset();
                	//boost::make_shared<lib::die>(*(*pos.p_ds)[pos.off]->p_d)->offset(&pos.off);
                //std::cerr << "Descending from offset " << ((path.size() > 0) ? path.back().off : 0UL) << " to child " << pos.off << std::endl;
                path.push_back((position){pos.p_ds, pos.off});
                //print_path;
               return 0;
            }
            catch (No_entry) {}

            //print_path; std::cerr << "Offset " << ((path.size() > 0) ? path.back().off : 0UL) << " has no children, trying siblings." << std::endl;
            
            // else look for later siblings right here
            int number_of_pops = 1;
            //if (path.size() == 0) mutable_path = pos.p_ds->path_from_root(pos.off);
            while (path.size() > 0)
            {
            	assert(path.size() == 0 || path.back().off == pos.off);
                try
                {
                	//Dwarf_Off tmp = (*pos.p_ds)[pos.off]->get_next_sibling_offset();
                    //print_path; std::cerr << "Found next sibling of " << pos.off << " is " << tmp << ", following" << std::endl;
            	    pos.off = (*pos.p_ds)[pos.off]->get_next_sibling_offset();//->get_offset();
                    	//boost::make_shared<lib::die>(pos.p_ds->p_f, *(*pos.p_ds)[pos.off]->p_d)->offset(&pos.off);
            		path.pop_back(); // our siblings have a different last hop
                    path.push_back(pos);
                    //print_path;
                    return number_of_pops;
                }
                catch (No_entry) 
                {
                    // else retreat higher up
                    //std::cerr << "Offset " << pos.off << " has no subsequent siblings, retreating." << std::endl;;
                    path.pop_back();
                    //print_path;
                    if (path.size() > 0)
                    {
	                    //std::cerr << " Will next try siblings of " << path.back().off << std::endl;
                    	pos.off = path.back().off;
                    }
                    //else  std::cerr << " Will terminate." << std::endl;
			    }
            }
            // if we got here, there is nothing left in the tree...
            assert(path.size() == 0);
            // ... so set us to the end sentinel
            pos.off = std::numeric_limits<Dwarf_Off>::max();
            path = path_type();
            return path.size();
        }
        int abstract_dieset::default_policy::decrement(position& pos,
        	path_type& path)
        {
        	assert(false);
        }

		// breadth-first traversal
		int abstract_dieset::bfs_policy::increment(position& pos,
			path_type& path)
		{
			/* Try to explore siblings, remembering children. */
			enqueue_children(pos, path);
			try
			{
				advance_to_next_sibling(pos, path);
				return 0;
			}
			catch (No_entry)
			{
				return take_from_queue_or_terminate(pos, path);
			}
		}
		int abstract_dieset::bfs_policy::increment_skipping_subtree(position& pos,
			path_type& path)
		{
			/* This is like normal traversal, but don't enqueue children */
			try
			{
				advance_to_next_sibling(pos, path);
				return 0;
			}
			catch (No_entry)
			{
				return take_from_queue_or_terminate(pos, path);
			}
		}
		
		void abstract_dieset::bfs_policy::enqueue_children(position& pos,
			path_type& path)
		{
			try
			{
				Dwarf_Off first_child_off = (*pos.p_ds)[pos.off]->get_first_child_offset();
				auto queued_path = path; 
				queued_path.push_back((position){pos.p_ds, first_child_off});
				this->m_queue.push_back(queued_path);
			} 
			catch (No_entry)
			{
				/* No children -- that's okay, just carry on and don't enqueue anything. */
			}
		}
		
		void abstract_dieset::bfs_policy::advance_to_next_sibling(position& pos,
			path_type& path)
		{
			/* Now find that sibling. */
			pos.off = (*pos.p_ds)[pos.off]->get_next_sibling_offset();
			// if that succeeded, we have a next node -- calculate our new path
			path.pop_back();
			path.push_back((position){pos.p_ds, pos.off});
		}
		
		int abstract_dieset::bfs_policy::take_from_queue_or_terminate(position& pos,
			path_type& path)
		{
			int retval;
			/* No more siblings, so take from the queue. */
			if (this->m_queue.size() > 0)
			{
				pos = this->m_queue.front().back(); 
				// retval is the number of levels up the tree we're moving
				retval = path.size() - this->m_queue.front().size();
				// finished processing head queue element
				this->m_queue.pop_front();
			}
			else
			{
				/* That's it! Use end sentinel. */
				pos.off = std::numeric_limits<Dwarf_Off>::max();
				path = path_type();
				retval = 0;
			}
			return retval;
		}
		
        int abstract_dieset::bfs_policy::decrement(position& pos,
        	path_type& path) 
        {
        	assert(false);
        }
        int abstract_dieset::bfs_policy::decrement_skipping_subtree(position& pos,
        	path_type& path) 
        {
        	assert(false);
        }
        
        // siblings-only traversal
        abstract_dieset::siblings_policy abstract_dieset::siblings_policy_sg;
        int abstract_dieset::siblings_policy::increment(position& pos,
        	path_type& path)
        {
        	auto maybe_next_off = (*pos.p_ds)[pos.off]->next_sibling_offset();
        	if (maybe_next_off)
            {
                pos.off = *maybe_next_off;
                path.pop_back(); path.push_back((position){pos.p_ds, *maybe_next_off});
            }
            else 
            {
                /* That's it! Use end sentinel. */
                pos.off = std::numeric_limits<Dwarf_Off>::max();
                path = path_type();
            }
        	return 0;
        }
        int abstract_dieset::siblings_policy::decrement(position& pos,
        	std::deque<abstract_dieset::position>& path) 
        {
        	assert(false);
        }
    }
    namespace lib
    {
		Dwarf_Half file_toplevel_die::get_address_size_for_cu(shared_ptr<compile_unit_die> cu) const
		{ 
			auto found = cu_info.find(cu->get_offset());
			assert(found != cu_info.end());
			return found->second.address_size; 
		}
		
		// helper function
		void add_cu_info(void *arg, 
				Dwarf_Off off,
				Dwarf_Unsigned cu_header_length,
				Dwarf_Half version_stamp,
				Dwarf_Unsigned abbrev_offset,
				Dwarf_Half address_size,
				Dwarf_Unsigned next_cu_header)
		{
			reinterpret_cast<file_toplevel_die*>(arg)
				->add_cu_info(off, cu_header_length, version_stamp, abbrev_offset,
					address_size, next_cu_header);
		}
		
		// the actual member function
		void file_toplevel_die::add_cu_info(Dwarf_Off off,
			Dwarf_Unsigned cu_header_length,
			Dwarf_Half version_stamp,
			Dwarf_Unsigned abbrev_offset,
			Dwarf_Half address_size,
			Dwarf_Unsigned next_cu_header)
		{
			/* This function is supposed to be idempotent, so don't clobber 
			 * any existing state. */
			
			cu_info[off].version_stamp = version_stamp;
			cu_info[off].address_size = address_size;
			// leave srcfiles as-is!
		}
			
		std::string 
		file_toplevel_die::source_file_name_for_cu(
			shared_ptr<compile_unit_die> cu,
			unsigned o
		)
		{
			Dwarf_Off off = cu->get_offset();
			// don't create cu_info if it's not there
			assert(cu_info.find(off) != cu_info.end());
			if (!cu_info[off].source_files) 
			{
				cu_info[off].source_files = boost::make_shared<lib::srcfiles>(
					*cu);
			}
			assert(cu_info[off].source_files);
			return cu_info[off].source_files->get(o);
		}
		
		unsigned 
		file_toplevel_die::source_file_count_for_cu(
			shared_ptr<compile_unit_die> cu)
		{
			Dwarf_Off off = cu->get_offset();
			// don't create cu_info if it's not there
			assert(cu_info.find(off) != cu_info.end());
			if (!cu_info[off].source_files) 
			{
				cu_info[off].source_files = boost::make_shared<lib::srcfiles>(
					*cu);
			}
			assert(cu_info[off].source_files);
			return cu_info[off].source_files->count();
		
		}

		boost::shared_ptr<spec::basic_die> file_toplevel_die::get_first_child()
		{
			// We have to explicitly loop through CU headers, 
			// to set the CU context when getting dies.
			Dwarf_Unsigned cu_header_length;
			Dwarf_Half version_stamp;
			Dwarf_Unsigned abbrev_offset;
			Dwarf_Half address_size;
			Dwarf_Unsigned next_cu_header;

			int retval;
			p_ds->p_f->clear_cu_context(&dwarf::lib::add_cu_info, (void*)this);

			retval = p_ds->p_f->next_cu_header(&cu_header_length, &version_stamp,
				&abbrev_offset, &address_size, &next_cu_header);
			if (retval != DW_DLV_OK)
			{
				std::cerr << "Warning: couldn't get first CU header! no debug information imported" << std::endl;
				throw No_entry();
			}

			switch (version_stamp)
			{
				case 2: p_spec = &dwarf::spec::dwarf3; break;
				default: throw std::string("Unsupported DWARF version stamp!");
			}
			prev_version_stamp = version_stamp;

			/* This lib::die constructor means "first CU of current CU context" 
			 * -- and we've just set the CU context to be the one we want. */
			return p_ds->get(lib::die(*p_ds->p_f));
				
			//	, 
			//	boost::make_shared<dwarf::lib::die>(*p_ds->p_f) /*,
			//	version_stamp, address_size*/);
		}

		Dwarf_Off file_toplevel_die::get_first_child_offset() const
		{
			return const_cast<file_toplevel_die*>(this)->get_first_child()->get_offset();
		}
		Dwarf_Off file_toplevel_die::get_next_sibling_offset() const
		{
			throw No_entry();
		}
		
		Dwarf_Off compile_unit_die::get_next_sibling_offset() const
		{
			auto nonconst_this = const_cast<compile_unit_die *>(this); // HACK
			auto parent = dynamic_pointer_cast<file_toplevel_die>(nonconst_this->get_parent());
			assert(parent);
			
			Dwarf_Off off = get_offset();
			auto found = parent->cu_info.find(off);
			// We can rely on already having cu_info populated, since 
			// clear_cu_context runs through all the CU headers once.
			if (found == parent->cu_info.end())
			{
				// print the CU info, for debugging
				cerr << "Did not found CU at offset 0x" << std::hex << off << std::dec << endl;
				cerr << "CU info contains only CUs at offsets: " << endl;
				std::for_each(parent->cu_info.begin(), parent->cu_info.end(),
					[](const pair<Dwarf_Off, file_toplevel_die::cu_info_t>& p) 
					{ cerr << std::hex << p.first << std::dec << endl; });
				assert(false);
			}
			
			++found;
			if (found == parent->cu_info.end()) throw No_entry();
			else return found->first;
		}
		boost::shared_ptr<spec::basic_die> compile_unit_die::get_next_sibling()
		{
			int retval;
			Dwarf_Half prev_version_stamp 
				= boost::dynamic_pointer_cast<file_toplevel_die>(p_ds->toplevel())
					->prev_version_stamp;
			Dwarf_Half version_stamp = prev_version_stamp;
			Dwarf_Off next_cu_offset;

			using boost::dynamic_pointer_cast;

			// first reset the CU context (pesky stateful API)
			retval = p_ds->p_f->clear_cu_context();
			if (retval != DW_DLV_OK) // then it must be DW_DLV_ERROR
			{
				assert(retval == DW_DLV_ERROR);
				std::cerr << "Warning: couldn't get first CU header! no debug information imported" << std::endl;
				throw No_entry();
			}

			// now find us
			Dwarf_Half address_size;
			for (retval = p_ds->p_f->next_cu_header(/*&cu_header_length*/0, &version_stamp,
					/*&abbrev_offset*/0, /*&address_size*/0, &next_cu_offset);
					retval != DW_DLV_NO_ENTRY; // termination condition (negated)
					retval = p_ds->p_f->next_cu_header(/*&cu_header_length*/0, &version_stamp,
									/*&abbrev_offset*/0, &address_size, &next_cu_offset))
			{
				// only support like-versioned CUs for now
				assert(prev_version_stamp == version_stamp);
				// now we can access the CU by constructing a lib::die under the current CU state
				auto p_cu = boost::make_shared<die>(*p_ds->p_f);
				Dwarf_Off off;
				p_cu->offset(&off); // retrieve the offset
				// store the data for later
				dynamic_pointer_cast<file_toplevel_die>(get_parent())
				->cu_info[off].version_stamp = version_stamp;
				dynamic_pointer_cast<file_toplevel_die>(get_parent())
				->cu_info[off].address_size = address_size;
				// don't touch srcfiles
				if (off == this->get_offset()) // found us
				{
					break;
				}
			}

			if (retval == DW_DLV_NO_ENTRY) // failed to find ourselves!
			{
				assert(false);
			}

			// now go one further
			retval = p_ds->p_f->next_cu_header(/*&cu_header_length*/0, &version_stamp,
					/*&abbrev_offset*/0, /*&address_size*/0, &next_cu_offset);
			if (retval == DW_DLV_NO_ENTRY) throw No_entry();
			else
			{
				auto p_cu = //dieset::my_make_shared<compile_unit_die>(
				 p_ds->get(dwarf::lib::die(*p_ds->p_f));//, // this is the CU DIE (really! "first" is relative to CU context)lib::die(), 
				//*p_ds);
				Dwarf_Off off;
				p_cu->offset(&off); // retrieve the offset
				// store the data for later
				// -- no need to do this now, as we did it during reset_context
				
				//dynamic_pointer_cast<file_toplevel_die>(get_parent())
				//->cu_info[off].version_stamp = version_stamp;
				//dynamic_pointer_cast<file_toplevel_die>(get_parent())
				//->cu_info[off].address_size = address_size;
				auto parent = dynamic_pointer_cast<file_toplevel_die>(get_parent());
				assert(parent->cu_info.find(off) != parent->cu_info.end());
				
				return p_cu;
			}
			
			//	boost::make_shared<compile_unit_die>(*p_ds, 
			//boost::make_shared<dwarf::lib::die>(*p_ds->p_f));
		} // end get_next_sibling()
	} // end namespace lib
} // end namespace dwarf
