#ifndef __DWARFPP_SPEC_ADT_HPP
#define __DWARFPP_SPEC_ADT_HPP

#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/iterator_adaptors.hpp>
#include <boost/make_shared.hpp>
#include "lib.hpp"
#include "expr.hpp"
#include "attr.hpp"

#define NULL_SHARED_PTR(type) boost::shared_ptr<type>()

namespace dwarf
{
    // -- FIXME bring abstract_dieset into spec?
	namespace spec 
    {
	    using namespace lib;
        //class abstract_dieset;
        //class abstract_dieset::iterator;
        class compile_unit_die;
        struct basic_die;
        struct file_toplevel_die;
        std::ostream& operator<<(std::ostream& s, const basic_die& d);
        
		class abstract_dieset
        {
        public:
            /* This is all you need to denote a member of a dieset. */
            struct position
            {
            	abstract_dieset *p_ds;
                Dwarf_Off off;
                void canonicalize_position()
                { 
/*                	try // test whether we're pointing at a real DIE
                    {
                    	if (!p_ds || off == std::numeric_limits<Dwarf_Off>::max())
                        {
                        	throw No_entry();
                        }
                        // FIXME: this will NOT throw No_entry! UNdefined behaviour! 
                    	//p_ds->operator[](off);
                        assert(p_ds->find(off) != p_ds->end());
                    } 
                    catch (No_entry) // if not, set us to be the end sentinel
                    {
                    	this->off = std::numeric_limits<Dwarf_Off>::max();
                    }*/ // FIXME: why is this function necessary?
                }
            };
            
            struct order_policy
            {
            	virtual int increment(position& pos, 
                	std::deque<position>& path) = 0;
                virtual int decrement(position& pos,
                	std::deque<position>& path) = 0;
			};
            
            struct die_pred : public std::unary_function<spec::basic_die, bool>
            {
				virtual bool operator()(const spec::basic_die& d) const = 0;
			};
            
            struct policy : order_policy, die_pred {};
            /* Default policy: depth-first order, all match. */
            struct default_policy : policy
            {
            	// depth-first iteration
            	int increment(position& pos,
                	std::deque<position>& path);
                int decrement(position& pos,
                	std::deque<position>& path);
                
                // always true
                bool operator()(const spec::basic_die& d) const { return true; }
			};
            static default_policy default_policy_sg;
            /* Breadth-first policy: breadth-first order, all match. */
            struct bfs_policy : policy
            {
            	// a queue of paths
            	std::deque<std::deque<position> > m_queue;
            	// breadth-first iteration
            	int increment(position& pos,
                	std::deque<position>& path);
                int decrement(position& pos,
                	std::deque<position>& path);

            	bfs_policy() : m_queue() {}
                
                // always true
                bool operator()(const spec::basic_die& d) const { return true; }
			};
            //static bfs_policy bfs_policy_sg;
            // we *don't* create a bfs policy singleton because each BFS traversal
            // has to keep its own state (queue of nodes)
            
            struct basic_iterator_base 
            : public position
            {
            	typedef std::pair<Dwarf_Off, boost::shared_ptr<spec::basic_die> > pair_type;
                std::deque<position> path_from_root;
            	policy& m_policy;
                bool operator==(const basic_iterator_base& arg) const
                { return this->off == arg.off && this->p_ds == arg.p_ds
                	&& (off == std::numeric_limits<Dwarf_Off>::max () || // HACK: == end() works
                    	&this->m_policy == &arg.m_policy);               // for any policy
                }
                bool operator!=(const basic_iterator_base& arg) const { return !(*this == arg); }
                basic_iterator_base(abstract_dieset& ds, Dwarf_Off off,
                	 const std::deque<position>& path_from_root,
                	 policy& pol = default_policy_sg);
                basic_iterator_base() // path_from_root is empty
                : position({0, 0UL}), m_policy(default_policy_sg) { canonicalize_position(); } 
                typedef std::bidirectional_iterator_tag iterator_category;
                typedef spec::basic_die value_type;
                typedef Dwarf_Off difference_type;
                typedef spec::basic_die *pointer;
                typedef spec::basic_die& reference;
            };

        	//typedef std::map<Dwarf_Off, boost::shared_ptr<spec::basic_die> >::iterator iterator;
            struct iterator;
        	virtual iterator find(Dwarf_Off off) = 0;
            virtual iterator begin() = 0;
            virtual iterator end() = 0;
            
            virtual std::deque< position >
            path_from_root(Dwarf_Off off) = 0;
            
            virtual boost::shared_ptr<spec::basic_die> operator[](Dwarf_Off off) const = 0;
            boost::shared_ptr<spec::basic_die> operator[](Dwarf_Off off)
            { return const_cast<const abstract_dieset *>(this)->operator[](off); }
            
            //virtual encap::rangelist rangelist_at(Dwarf_Unsigned i) const = 0;
           
            struct iterator
            : public boost::iterator_adaptor<iterator, // Derived
                    basic_iterator_base,        // Base
                    boost::shared_ptr<spec::basic_die>, // Value
                    boost::use_default, // Traversal
                    boost::shared_ptr<spec::basic_die> // Reference
                > 
            {
                //typedef std::pair<const Dwarf_Off, boost::shared_ptr<spec::basic_die> > Value;
                typedef boost::shared_ptr<spec::basic_die> Value;
            	typedef basic_iterator_base Base;
            	
                iterator() : iterator::iterator_adaptor_() {}
            	
                iterator(Base p) : iterator::iterator_adaptor_(p) {}
            	
                iterator(abstract_dieset& ds, Dwarf_Off off, 
                	const std::deque<position>& path_from_root, 
                    policy& pol = default_policy_sg)  
                : iterator::iterator_adaptor_(
                	basic_iterator_base(ds, off, path_from_root, pol)) {}

                iterator(const position& pos, 
                	const std::deque<position>& path_from_root,
                    policy& pol = default_policy_sg)  
                : iterator::iterator_adaptor_(basic_iterator_base(
                	*pos.p_ds, pos.off, path_from_root, pol)) {}
                
                // copy-like constructor that changes policy
                iterator(const iterator& arg, policy& pol) : 
                	iterator::iterator_adaptor_(basic_iterator_base(
                    	*arg.base().p_ds, arg.base().off, arg.base().path_from_root, pol)) {}
                            	
                void increment()        
                { this->base().m_policy.increment(this->base_reference(), this->base_reference().path_from_root); }
            	void decrement()        
                { this->base().m_policy.decrement(this->base_reference(), this->base_reference().path_from_root); }
                Value dereference() { return /*std::make_pair(
                	this->base().off, */this->base().p_ds->operator[](this->base().off)/*)*/; }
                Value dereference() const { return this->base().p_ds->operator[](this->base().off); }
                position& pos() { return this->base_reference(); }
                const std::deque<position>& path() { return this->base_reference().path_from_root; }
                //bool equal(const iterator& arg) { return this->base_reference() == arg.base_reference(); }
            };
            virtual boost::shared_ptr<spec::file_toplevel_die> toplevel() = 0; /* NOT const */
            virtual const spec::abstract_def& get_spec() const = 0;
        };        
        
	    struct basic_die
        {
        	friend std::ostream& operator<<(std::ostream& s, const basic_die& d);
        
		    virtual Dwarf_Off get_offset() const = 0;
            virtual Dwarf_Half get_tag() const = 0;
            
            virtual boost::shared_ptr<basic_die> get_parent() = 0;
			boost::shared_ptr<basic_die> get_parent() const
            { return const_cast<basic_die *>(this)->get_parent(); }           
            
            virtual boost::shared_ptr<basic_die> get_first_child() = 0;
            boost::shared_ptr<basic_die> get_first_child() const
            { return const_cast<basic_die *>(this)->get_first_child(); }
            virtual Dwarf_Off get_first_child_offset() const = 0;
            boost::optional<Dwarf_Off> first_child_offset() const
            { 	try { return this->get_first_child_offset(); } 
            	catch (No_entry) { return boost::optional<Dwarf_Off>(); } }
            
            virtual boost::shared_ptr<basic_die> get_next_sibling() = 0;
            boost::shared_ptr<basic_die> get_next_sibling() const
            { return const_cast<basic_die *>(this)->get_next_sibling(); }
            virtual Dwarf_Off get_next_sibling_offset() const = 0;            
            boost::optional<Dwarf_Off> next_sibling_offset() const
            { 	try { return this->get_next_sibling_offset(); } 
            	catch (No_entry) { return boost::optional<Dwarf_Off>(); } }
            
            // FIXME: iterator pair //virtual std::pair< > get_children() = 0;
            
            virtual boost::optional<std::string> get_name() const = 0;
            virtual const spec::abstract_def& get_spec() const = 0;
            
            virtual const abstract_dieset& get_ds() const
            { return const_cast<basic_die *>(this)->get_ds(); }
            virtual abstract_dieset& get_ds() = 0;

            boost::shared_ptr<basic_die> get_this();
            boost::shared_ptr<basic_die> get_this() const;

			abstract_dieset::iterator 
            iterator_here(abstract_dieset::policy& pol = abstract_dieset::default_policy_sg)
            { return abstract_dieset::iterator(
            	this->get_ds(),
                this->get_offset(),
                this->get_ds().find(this->get_offset()).base().path_from_root, 
                pol); }

        protected: // public interface is to downcast
            virtual std::map<Dwarf_Half, encap::attribute_value> get_attrs() = 0;
            // not a const function because may create backrefs -----------^^^
		public:
            boost::optional<std::vector<std::string> >
            ident_path_from_root() const;

            boost::optional<std::vector<std::string> >
            ident_path_from_cu() const;

			/* These, and other resolve()-style functions, are not const 
             * because they need to be able to return references to mutable
             * found DIEs. FIXME: provide const overloads. */
            boost::shared_ptr<basic_die> 
            nearest_enclosing(Dwarf_Half tag);

            boost::shared_ptr<compile_unit_die> 
            enclosing_compile_unit();

            boost::shared_ptr<basic_die>
            find_sibling_ancestor_of(boost::shared_ptr<basic_die> d);
        };

        struct with_runtime_location_die : public virtual basic_die
        {
        	struct sym_binding_t 
            { 
            	Dwarf_Off file_relative_start_addr; 
                Dwarf_Unsigned size;
            };
        	virtual encap::loclist get_runtime_location() const;
            virtual boost::optional<Dwarf_Off> contains_addr(
            	Dwarf_Addr file_relative_addr,
                sym_binding_t (*sym_resolve)(const std::string& sym, void *arg) = 0, 
                void *arg = 0) const;
		};
        
        struct with_stack_location_die : public virtual basic_die
        {
            virtual boost::optional<Dwarf_Off> contains_addr(
            	    Dwarf_Addr absolute_addr,
                    Dwarf_Signed frame_base_addr,
                    Dwarf_Off dieset_relative_ip,
                    dwarf::lib::regs *p_regs = 0) const;
    	};
                
	    struct with_named_children_die : public virtual basic_die
        {
            virtual 
            boost::shared_ptr<basic_die>
            named_child(const std::string& name);

            template <typename Iter>
            boost::shared_ptr<basic_die> 
            resolve(Iter path_pos, Iter path_end);

            boost::shared_ptr<basic_die> 
            resolve(const std::string& name);

            template <typename Iter>
            boost::shared_ptr<basic_die> 
            scoped_resolve(Iter path_pos, Iter path_end);

            template <typename Iter>
            void
            scoped_resolve_all(Iter path_pos, Iter path_end, 
            	std::vector<boost::shared_ptr<basic_die> >& results, int max = 0) 
            {
            	boost::shared_ptr<basic_die> found_from_here = resolve(path_pos, path_end);
            	if (found_from_here) 
                { 
                	results.push_back(found_from_here); 
                    if (max != 0 && results.size() == max) return;
                }
                if (this->get_tag() == 0) return;
                else // find our nearest encloser that has named children
                {
                	boost::shared_ptr<spec::basic_die> p_encl = this->get_parent();
                    while (boost::dynamic_pointer_cast<with_named_children_die>(p_encl) == 0)
                    {
                    	if (p_encl->get_tag() == 0) return;
                    	p_encl = p_encl->get_parent();
                    }
                    // we've found an encl that has named children
                    dynamic_cast<with_named_children_die&>(*p_encl)
                    	.scoped_resolve_all(path_pos, path_end, results);
                    return;
                }
			}            

            boost::shared_ptr<basic_die> 
            scoped_resolve(const std::string& name);
        };
        
        struct file_toplevel_die : public virtual with_named_children_die
        {
        	// FIXME: visible_resolve stuff goes here
        };
                
        class abstract_mutable_dieset : public abstract_dieset
        {
        public:
        	virtual 
            boost::shared_ptr<spec::basic_die> 
            insert(Dwarf_Off key, boost::shared_ptr<spec::basic_die> val) = 0;
        };

        
/****************************************************************/
/* begin generated ADT includes                                 */
/****************************************************************/
#define declare_base(base) virtual base ## _die
#define base_fragment(base) base ## _die(ds, p_d) {}
#define initialize_base(fragment) fragment ## _die(ds, p_d)
#define constructor(fragment, ...) 
        
#define begin_class(fragment, base_inits, ...) \
	struct fragment ## _die : __VA_ARGS__ { \
    	constructor(fragment, base_inits)
#define base_initializations(...) __VA_ARGS__
#define end_class(fragment) \
	};

#define stored_type_string std::string
#define stored_type_flag bool
#define stored_type_unsigned Dwarf_Unsigned
#define stored_type_signed Dwarf_Signed
#define stored_type_offset Dwarf_Off
#define stored_type_half Dwarf_Half
#define stored_type_ref Dwarf_Off
#define stored_type_tag Dwarf_Half
#define stored_type_loclist dwarf::encap::loclist
#define stored_type_address Dwarf_Addr
#define stored_type_refdie boost::shared_ptr<spec::basic_die> 
#define stored_type_refdie_is_type boost::shared_ptr<spec::type_die> 
#define stored_type_rangelist dwarf::encap::rangelist

#define attr_optional(name, stored_t) \
	virtual boost::optional<stored_type_ ## stored_t> get_ ## name() const = 0; \
  	boost::optional<stored_type_ ## stored_t> name() const { return get_ ## name(); }

#define super_attr_optional(name, stored_t)

#define attr_mandatory(name, stored_t) \
	virtual stored_type_ ## stored_t get_ ## name() const = 0; \
  	stored_type_ ## stored_t name() const { return get_ ## name(); } 

#define super_attr_mandatory(name, stored_t)

// define additional virtual dies first -- note that
// some virtual DIEs are defined manually (above)
begin_class(program_element, base_initializations(initialize_base(basic)), declare_base(basic))
        attr_optional(decl_file, unsigned)
        attr_optional(decl_line, unsigned)
        attr_optional(decl_column, unsigned)
        attr_optional(prototyped, flag)
        attr_optional(declaration, flag)
        attr_optional(external, flag)
        attr_optional(visibility, unsigned)
end_class(program_element)
begin_class(type, base_initializations(initialize_base(program_element)), declare_base(program_element))
        attr_optional(byte_size, unsigned)
        virtual boost::optional<Dwarf_Unsigned> calculate_byte_size() const;
        virtual bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
		virtual boost::shared_ptr<type_die> get_concrete_type() const;
        boost::shared_ptr<type_die> get_concrete_type();
end_class(type)
begin_class(type_chain, base_initializations(initialize_base(type)), declare_base(type))
        attr_optional(type, refdie_is_type)
        boost::optional<Dwarf_Unsigned> calculate_byte_size() const;
        boost::shared_ptr<type_die> get_concrete_type() const;
end_class(type_chain)

#define extra_decls_compile_unit \
		boost::optional<Dwarf_Unsigned> implicit_array_base() const; 
#define extra_decls_subprogram \
        boost::optional< std::pair<Dwarf_Off, boost::shared_ptr<spec::with_stack_location_die> > > \
        contains_addr_as_frame_local_or_argument( \
            	    Dwarf_Addr absolute_addr, \
                    Dwarf_Off dieset_relative_ip, \
                    Dwarf_Signed *out_frame_base, \
                    dwarf::lib::regs *p_regs = 0) const; \
        bool is_variadic() const;
#define extra_decls_variable \
        bool has_static_storage() const;
#define extra_decls_array_type \
		boost::optional<Dwarf_Unsigned> element_count() const; \
        boost::optional<Dwarf_Unsigned> calculate_byte_size() const;
#define extra_decls_pointer_type \
		boost::shared_ptr<type_die> get_concrete_type() const; \
        boost::optional<Dwarf_Unsigned> calculate_byte_size() const;
#define extra_decls_reference_type \
		boost::shared_ptr<type_die> get_concrete_type() const; \
        boost::optional<Dwarf_Unsigned> calculate_byte_size() const;

#include "dwarf3-adt.h"

#undef extra_decls_reference_type
#undef extra_decls_pointer_type
#undef extra_decls_array_type
#undef extra_decls_variable
#undef extra_decls_compile_unit

#undef declare_base
#undef base_fragment
#undef initialize_base
#undef constructor
#undef begin_class
#undef base_initializations
#undef end_class
#undef stored_type_string
#undef stored_type_flag
#undef stored_type_unsigned
#undef stored_type_signed
#undef stored_type_offset
#undef stored_type_half
#undef stored_type_ref
#undef stored_type_tag
#undef stored_type_loclist
#undef stored_type_address
#undef stored_type_refdie
#undef stored_type_refdie_is_type
#undef stored_type_rangelist
#undef attr_optional
#undef attr_mandatory
#undef super_attr_optional
#undef super_attr_mandatory

/****************************************************************/
/* end generated ADT includes                                   */
/****************************************************************/

        template <typename Iter>
        boost::shared_ptr<basic_die> 
        with_named_children_die::resolve(Iter path_pos, Iter path_end)
        {
            /* We can't return "this" because it's not a shared_ptr, and we don't
             * want to create a duplicate count by creating a new shared ptr from it.
             * So we ask our containing dieset to make a new one for us. In the case
             * of lib::die, this will create an entirely new Die. In the case of
             * encap::die, it will just retrieve the stored shared_ptr and use that. */
            this->get_offset();
            this->get_ds();
            if (path_pos == path_end) return this->get_ds().operator[](this->get_offset());
            Iter cur_plus_one = path_pos; cur_plus_one++;
            if (cur_plus_one == path_end) return named_child(*path_pos);
            else
            {
                auto found = named_child(*path_pos);
                if (!found) return NULL_SHARED_PTR(basic_die);
                auto p_next_hop =
                    boost::dynamic_pointer_cast<with_named_children_die>(found);
                if (!p_next_hop) return NULL_SHARED_PTR(basic_die);
                else return p_next_hop->resolve(++path_pos, path_end);
            }
        }
        
        template <typename Iter>
        boost::shared_ptr<basic_die> 
        with_named_children_die::scoped_resolve(Iter path_pos, Iter path_end)
        {
            if (resolve(path_pos, path_end)) return this->get_ds().operator[](this->get_offset());
            if (this->get_tag() == 0) return NULL_SHARED_PTR(basic_die);
            else // find our nearest encloser that has named children
            {
                boost::shared_ptr<spec::basic_die> p_encl = this->get_parent();
                while (boost::dynamic_pointer_cast<with_named_children_die>(p_encl) == 0)
                {
                    if (p_encl->get_tag() == 0) return NULL_SHARED_PTR(basic_die);
                    p_encl = p_encl->get_parent();
                }
                // we've found an encl that has named children
                return boost::dynamic_pointer_cast<with_named_children_die>(p_encl)
                    ->scoped_resolve(path_pos, path_end);
            }
		}
    }
}

#endif
