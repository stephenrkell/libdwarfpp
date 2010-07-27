/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * abstract.hpp: prescriptive statically-typed abstractions of DWARF data.
 *
 * Copyright (c) 2008--9, Stephen Kell.
 */

#ifndef __DWARFPP_ABSTRACT_HPP
#define __DWARFPP_ABSTRACT_HPP

/* DWARF is very weaselly about what children and attributes a given DIE
 * may have or must have. These classes are much more prescriptive. For
 * each TAG they define 
 * - a set of attributes it *must* have (functional properties)
 * - a set of attributes it *may*  have (functional properties using boost::optional)
 * - a set of tags for allowed children (exposed as iterators) 
 * ... and emit warnings when other attributes are found (should be errors?),
 * ... and emit warnings when other children are found.

 * Key abstractions:
 * - Offsets are hidden. The identity of a DIE is a Wrap_base lvalue.
 * - Attribute values are no longer behind a union. Each has a precise type.

 * Mutation is supported: 
 * - mandatory attributes may be modified
 * - optional attributes may be modified, added, removed
 * - children may be added or removed using an insertion iterator.

 * Each DIE still has an offset... artificial DIEs are given offsets
 * according to an incremented counter, as before.

 * In the future, these classes may be implemented as wrappers
 * around not only dwarf::encap:: objects but also regular dwarf::lib objects. 
 * Mutability will probably be restricted in the latter case. */

#include "dwarfpp/lib.hpp"
#include "dwarfpp/spec_adt.hpp"
#include <boost/optional.hpp>

// macros for defining attribute getters/setters
#define stored_type_string std::string
#define stored_type_flag bool
#define stored_type_unsigned Dwarf_Unsigned
#define stored_type_signed Dwarf_Signed
#define stored_type_base base&
#define stored_type_offset Dwarf_Off
#define stored_type_half Dwarf_Half
#define stored_type_ref Dwarf_Off
#define stored_type_tag Dwarf_Half
//#define stored_type_refdie dwarf::abstract::Die_abstract_base<Rep>&
#define stored_type_refdie boost::shared_ptr<dwarf::spec::basic_die> //abstract::Die_abstract_base<Rep>&

#define declare_getters_optional(stored_t, name) \
	virtual boost::optional<stored_type_ ## stored_t> get_ ## name() const = 0; \
  	boost::optional<stored_type_ ## stored_t> name() const { return get_ ## name(); }

#define declare_getset_optional(stored_t, name) \
	declare_getters_optional(stored_t, name) \
    virtual abstract_self& set_ ## name(boost::optional<stored_type_ ## stored_t> arg) = 0;
    
#define declare_getters_mandatory(stored_t, name) \
	virtual stored_type_ ## stored_t get_ ## name() const = 0; \
  	stored_type_ ## stored_t name() const { return get_ ## name(); } 

#define declare_getset_mandatory(stored_t, name) \
	declare_getters_mandatory(stored_t, name) \
  	virtual abstract_self& set_ ## name(stored_type_ ## stored_t arg) = 0;

#define declare_iters(fragment_sg, fragment_pl) \
	virtual typename iters<Rep, DW_TAG_ ## fragment_sg>::iterator fragment_pl ## _begin() = 0; \
    virtual typename iters<Rep, DW_TAG_ ## fragment_sg>::iterator fragment_pl ## _end() = 0;

namespace dwarf {
	namespace encap {
    	// FIXME: abstract this
        struct loclist;
    }
	namespace abstract {
		using namespace dwarf::lib;
        
        // forward declarations
        template <class Rep, Dwarf_Half Tag> struct tag;
		template <class Rep, Dwarf_Half Tag> class iters;
        template <class Rep> class Die_abstract_base;
        template <class Rep> class Die_abstract_subprogram;
        template <class Rep> class Die_abstract_unspecified_parameters;        
        template <class Rep> class Die_abstract_array_type;          
        template <class Rep> class Die_abstract_is_type_chain;
        template <class Rep> class Die_abstract_compile_unit;        

include(`abstract_preamble_gen.inc')
         
        // lookup for tag types -- we will specialise this
        template <class Rep, Dwarf_Half Tag> struct tag { typedef Die_abstract_base<Rep> type; };
        // lookup for iterator types -- likewise
        template <class Rep, Dwarf_Half Tag> class iters
        {
        public:
            typedef typename Die_abstract_base<Rep>::children_base_iterator base_iterator;
        	typedef typename Die_abstract_base<Rep>::children_iterator iterator;
            typedef typename Die_abstract_base<Rep>::named_children_base_iterator named_children_base_iterator;
        	typedef typename Die_abstract_base<Rep>::named_children_iterator named_children_iterator;
        };
        
    	template <class Rep>
		class Die_abstract_base : public virtual spec::basic_die
		{
        protected:
        	typedef Die_abstract_base<Rep> base;
        public:
        	typedef Die_abstract_base<Rep> abstract_self;
            declare_getters_mandatory(offset, offset)
            declare_getters_mandatory(tag, tag)
            declare_getters_mandatory(base, parent)
            declare_getters_optional(string, name)
            
	        virtual typename Rep::children_iterator children_begin() = 0; \
            virtual typename Rep::children_iterator children_end() = 0;
            
            virtual const dwarf::spec::abstract_def& get_spec() = 0;
            
//             boost::optional<std::vector<std::string> >
//             path_from_root()
//             {
//                 if (get_offset() == 0) return std::vector<std::string>(); // empty
//                 else if (get_name())
//                 {
// 	                boost::optional<std::vector<std::string> >
//                     	built = get_parent().path_from_root();
//                     if (!built) return 0;
//                     else
//                     {
// 	                    (*built).push_back(*get_name());
//     	                return built;
//                     }
//                 }
//                 else return 0;
//             }
//             
//             boost::optional<std::vector<std::string> >
//             path_from_cu()
//             {
//                 if (get_offset() == 0) return 0; // error
//                 if (get_tag() == DW_TAG_compile_unit) return std::vector<std::string>(); // empty
//                 else if (get_name()) // recursive case
//                 {
//                 	// try to build our parent's path
// 	                boost::optional<std::vector<std::string> >
//                     	built = get_parent().path_from_cu();
//                     if (!built) return 0;
//                     else // success, so just add our own name to the path
//                     {
// 	                    (*built).push_back(*get_name());
//     	                return built;
//                     }
//                 }
//                 else return 0; // error -- we have no name
//             }
//             
//             // FIXME: stupid g++ doesn't understand complex type expressions.
//             // ... when it does, remove the extra parameters here.
//             template <Dwarf_Half Tag, 
//             	typename ForTag = typename dwarf::abstract::tag< Rep, Tag >, 
//             	typename Returned = typename ForTag::type>
//             boost::optional<Returned&> nearest_enclosing()
//             {
//             	if (get_tag() == 0) return 0;
//                 else if (get_parent().get_tag() == Tag) 
//                 {
// 					return dynamic_cast<Returned&>(get_parent());
//                 }
//                 else return get_parent().nearest_enclosing<Tag, ForTag, Returned>();
//             }
//             
//             Die_abstract_compile_unit<Rep>& enclosing_compile_unit()
//             { return *(this->nearest_enclosing<DW_TAG_compile_unit>()); }
//             
/*            boost::optional<Die_abstract_base<Rep>&> 
            find_sibling_ancestor_of(Die_abstract_base<Rep>& d)
            {
            	// search upward from the argument die to find a sibling of us
                if (&d == this) return d;
                else if (d.get_offset() == 0UL) return 0; // reached the top without finding anything
                else if (this->get_offset() == 0UL) return 0; // we have no siblings
            	else if (&d.get_parent() == &this->get_parent()) // we are siblings
                {
                	return boost::optional<Die_abstract_base<Rep>&>(d);
                }
                else return find_sibling_ancestor_of(d.get_parent()); // recursive case
            }*/
            
//             Die_abstract_base<Rep>
//             lowest_common_ancestor(Die_abstract_base<Rep>& d)
//             {
//                 if (&d == this) return d;
//                 else if (d.get_offset() == 0UL) return d; // reached the top without finding anything
//                 else if (this->get_offset() == 0UL) return *this; // we have no siblings
//             	else if (&d.get_parent() == &this->get_parent()) // we are siblings
//                 {
//                 	return this->get_parent();
//                 }
//                 else return this->get_parent()->lowest_common_ancestor(d.get_parent()); // recursive case
//             }        
		};
        template <class Rep>
        class Die_abstract_has_named_children : public virtual Die_abstract_base<Rep>,
        	public virtual spec::with_named_children_die
        {
        public:
        	typedef Die_abstract_has_named_children<Rep> abstract_self;
        	
	        virtual typename Rep::named_children_iterator named_children_begin()
            { return typename Rep::named_children_base_iterator(this->children_begin(), this->children_end()); }
            virtual typename Rep::named_children_iterator named_children_end()
            { return typename Rep::named_children_base_iterator(this->children_end(), this->children_end());}
            
            
           /* virtual boost::optional< boost::shared_ptr<spec::basic_die> > named_child(const std::string& name)
            { 
				//std::cerr << "Looking for child named " << name << std::endl;
                for (typename Rep::named_children_iterator i 
                		= this->named_children_begin();
                	i != this->named_children_end();
                    i++)
                {
                	//std::cerr << "Testing candidate die at offset " << (*i)->get_offset() << std::endl;
                	if (dynamic_cast<Die_abstract_base<Rep> *>(*i)->get_name() 
                    	&& *(dynamic_cast<Die_abstract_base<Rep> *>(*i)->get_name()) == name)
                    { 
                        return *(dynamic_cast<Die_abstract_base<Rep> *>(*i));
                    }
                }
				return 0;
          	}
            
            template <typename Iter>
            boost::optional<Die_abstract_base<Rep>&> resolve(Iter path_pos, Iter path_end)
            {
            	if (path_pos == path_end) return *this;
            	Iter cur_plus_one = path_pos; cur_plus_one++;
            	if (cur_plus_one == path_end) return named_child(*path_pos);
                else
                {
                	boost::optional<Die_abstract_base<Rep>&> found = named_child(*path_pos);
                    if (!found) return 0;
                    Die_abstract_has_named_children *p_next_hop =
                    	dynamic_cast<Die_abstract_has_named_children *>(&(*found));
                	if (!p_next_hop) return 0;
                    else return p_next_hop->resolve(++path_pos, path_end);
                }
            }
            boost::optional<Die_abstract_base<Rep>&> resolve(std::string& name)
            {
            	std::vector<std::string> multipart_name;
                multipart_name.push_back(name);
                return resolve(multipart_name.begin(), multipart_name.end());
            }
            
            template <typename Iter>
            boost::optional<Die_abstract_base<Rep>&> scoped_resolve(Iter path_pos, Iter path_end)
            {
            	if (resolve(path_pos, path_end)) return *this;
                if (this->get_tag() == 0) return 0;
                else // find our nearest encloser that has named children
                {
                	Die_abstract_base<Rep>& encl = this->get_parent();
                    while (dynamic_cast<Die_abstract_has_named_children<Rep>*>(&encl) == 0)
                    {
                    	if (encl.get_tag() == 0) return 0;
                    	encl = encl.get_parent();
                    }
                    // we've found an encl that has named children
                    return dynamic_cast<Die_abstract_has_named_children<Rep>&>(encl)
                    	.scoped_resolve(path_pos, path_end);
                }
			}            
            boost::optional<Die_abstract_base<Rep>&> scoped_resolve(std::string& name)
            {
            	std::vector<std::string> multipart_name;
                multipart_name.push_back(name);
                return scoped_resolve(multipart_name.begin(), multipart_name.end());
            }*/
        };
        
        template <class Rep>        
        class Die_abstract_is_program_element : public virtual Die_abstract_base<Rep>,
        	public virtual spec::program_element_die
        {
        public:
        	typedef Die_abstract_is_program_element<Rep> abstract_self;
        	boost::optional<bool> external() const { return get_external(); }
        	virtual boost::optional<bool> get_external() const = 0;
           	virtual Die_abstract_is_program_element<Rep>& set_external(boost::optional<bool>) = 0;
        	
            boost::optional<bool> declaration() const { return get_declaration(); }
        	virtual boost::optional<bool> get_declaration() const = 0;
           	virtual Die_abstract_is_program_element<Rep>& set_declaration(boost::optional<bool>) = 0;

            boost::optional<Dwarf_Unsigned> visibility() const { return get_visibility(); }
        	virtual boost::optional<Dwarf_Unsigned> get_visibility() const = 0;
           	virtual Die_abstract_is_program_element<Rep>& set_visibility(boost::optional<Dwarf_Unsigned>) = 0;

		};

        template <class Rep>        
        class Die_abstract_is_type : public virtual Die_abstract_is_program_element<Rep>,
        	public virtual spec::type_die
        {
        public:
        	typedef Die_abstract_is_type abstract_self;
            boost::optional<Dwarf_Unsigned> byte_size() const { return get_byte_size(); }
        	virtual boost::optional<Dwarf_Unsigned> get_byte_size() const = 0;
           	virtual Die_abstract_is_type<Rep>& set_byte_size(boost::optional<Dwarf_Unsigned>) = 0;

//             virtual Die_abstract_is_type<Rep>& get_concrete_type()
//             {
//             	/* This will follow any typedefs or modifiers to yield the 
//                  * underlying type that determines byte size and 
//                  * encoding or substructure. We follow typedefs and modifiers
//                  * but not pointer or refs or arrays. */
//                 return *this;
//             }
//             
//             static Dwarf_Unsigned array_type_element_count(Die_abstract_array_type<Rep>& t)
//             {
//             	int array_size;
//                 if (t.subrange_types_begin() != t.subrange_types_end())
//                 {
//             	    Die_abstract_subrange_type<Rep>& subrange = **(t.subrange_types_begin());
//                     if (subrange.get_count()) array_size = *subrange.get_count();
//                     else
//                     {
//                 	    /* For a weird bug, comment out all the debugging lines in this block! 
//                          * reading *subrange.get_upper_bound() yields garbage. */
// 
//                 	    //std::cerr << subrange << std::endl;
//                 	    Dwarf_Unsigned lower_bound = subrange.get_lower_bound() ?
//                     	    *subrange.get_lower_bound() : 0; // ... FIXME: assuming C
//                         //assert(subrange.get_upper_bound());
//                         if (!subrange.get_upper_bound()) return 0;
//                         //assert(subrange.has_attr(DW_AT_upper_bound));
//                         boost::optional<Dwarf_Unsigned> upper_bound_optional = static_cast<boost::optional<Dwarf_Unsigned> >(subrange.get_upper_bound());
// 
//                         // try printing addresses of anything and everything
//                         //std::cerr << "subrange is at " << &subrange << std::endl;
//                         //std::cerr << "upper_bound_optional is at " << &upper_bound_optional << " and has size " << sizeof upper_bound_optional << std::endl;
//                         //std::cerr << "upper_bound_optional pointee is at " << &(*upper_bound_optional) << " and has size " << sizeof *upper_bound_optional << std::endl;
// 
//                         // try constructing the boost::optional here
//                         //boost::optional<Dwarf_Unsigned> here;
//                         //subrange[DW_AT_upper_bound].get_unsigned();
//                         //boost::optional<Dwarf_Unsigned> here = subrange[DW_AT_upper_bound].get_unsigned();
//                         //std::cerr << "boost::optional created here at " << &here << std::endl;
//                         //std::cerr << "boost::optional created here stores object at " << &(*here) << std::endl;
//                         //std::cerr << "boost::optional created here has value " << *here << std::endl;
// 
//                         //std::cerr << "attribute claims upper bound is " << subrange[DW_AT_upper_bound].get_unsigned() << std::endl;
//                         //std::cerr << "abstract claims upper bound is " << *subrange.get_upper_bound() << std::endl;
//                         //std::cerr << "local copy claims upper bound is " << *upper_bound_optional << std::endl;
//                         //assert(subrange[DW_AT_upper_bound].get_unsigned() == *subrange.get_upper_bound());
//                         assert(*subrange.get_upper_bound() < 10000000); // detects most garbage
//                         Dwarf_Unsigned upper_bound = *subrange.get_upper_bound();
//                         array_size = upper_bound - lower_bound + 1;
//                     }
// 			    }
//                 return array_size;
//             }
//             
//             static Dwarf_Unsigned calculate_byte_size(Die_abstract_is_type<Rep>& t)
//             {
//             	// FIXME: pull out non-repdep functionality like this
//                 // into simple abstract base classes with no Rep dependency
//                 
//             	if (t.get_byte_size()) return *t.get_byte_size();
//                 else if (t.get_concrete_type().get_tag() == DW_TAG_array_type)
//                 {
//                 	// HACK: we'd make this a member function and
//                     // use overriding to specialise for DW_TAG_array_type,
//                     // but Die_*_array_type are generated
//                     assert(*dynamic_cast<Die_abstract_array_type<Rep>&>(t).get_type());
//                     return array_type_element_count(
//                     	dynamic_cast<Die_abstract_array_type<Rep>&>(t))
//                         * calculate_byte_size(**dynamic_cast<Die_abstract_array_type<Rep>&>(t).get_type());
//                 }
//                 else return *(t.get_concrete_type().get_byte_size());
//             }
//             
//             virtual bool is_rep_compatible(Die_abstract_is_type<Rep>& t)
//             {
//             	/* Okay, this needs to be a member function. */
//             	return false;
//             }
		};

        template <class Rep>        
        class Die_abstract_is_type_chain : public virtual Die_abstract_is_type<Rep>,
        	public virtual spec::type_chain_die
        {
        public:
        	typedef Die_abstract_is_type_chain abstract_self;
            boost::optional<boost::shared_ptr< spec::type_die > > type() const { return get_type(); }
        	virtual boost::optional<boost::shared_ptr< spec::type_die > > get_type() const = 0;
           	virtual Die_abstract_is_type_chain<Rep>& set_type(boost::optional<boost::shared_ptr< spec::type_die > >) = 0;

//             virtual Die_abstract_is_type<Rep>& get_concrete_type()
//             {
//             	/* This will follow any typedefs or modifiers to yield the 
//                  * underlying type that determines byte size and 
//                  * encoding or substructure. We follow typedefs and modifiers
//                  * but not pointer or refs or arrays. */
//                 switch(this->get_tag())
//                 {
//                 	case DW_TAG_pointer_type:
//                     case DW_TAG_reference_type:
//                     	return *this;
//                     case DW_TAG_array_type:
//                     	assert(false); return *this; // need to handle this specially;
//                     default:
//                     	if (!this->get_type()) return *this; // broken chain
//                     	else return (*this->get_type())->get_concrete_type();
//                 }
//             }

		};

        template <class Rep>        
		class Die_abstract_all_compile_units : 
        	public virtual Die_abstract_has_named_children<Rep>,
            public virtual spec::file_toplevel_die
		{
		public:
        	typedef Die_abstract_all_compile_units abstract_self;
            declare_iters(compile_unit, compile_units)
			//declare_iters(subprogram, subprograms)
   			//declare_iters(base_type, base_types)
            //declare_iters(pointer_type, pointer_types)
		};
        template <class Rep> struct tag<Rep, 0>
        { typedef Die_abstract_all_compile_units<Rep> type; };

include(`abstract_hdr_gen.inc')

		struct Bad_spec { };

		// abstract factory
        class factory
        {
        public:
        	template<class Rep>  // specialise this on a per-rep basis
            static typename Rep::factory_type& 
            	get_factory(const dwarf::spec::abstract_def& spec) //__attribute__((no_return))
            { throw Bad_spec(); }
       };
	}
}
#undef stored_type_string
#undef stored_type_flag
#undef stored_type_unsigned
#undef stored_type_signed 
#undef stored_type_base 
#undef stored_type_offset
#undef stored_type_half 
#undef stored_type_ref 
#undef stored_type_tag 
#undef stored_type_refdie 

#undef declare_getters_optional
#undef declare_getset_optional    
#undef declare_getters_mandatory
#undef declare_getset_mandatory
#undef declare_iters

#endif
