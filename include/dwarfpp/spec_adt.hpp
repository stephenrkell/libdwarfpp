#ifndef __DWARFPP_SPEC_ADT_HPP
#define __DWARFPP_SPEC_ADT_HPP

#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/iterator_adaptors.hpp>
#include <boost/iterator/filter_iterator.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/make_shared.hpp>
#include <boost/enable_shared_from_this.hpp>
#include "lib.hpp"
#include "expr.hpp"
#include "attr.hpp"
#include "opt.hpp"

#define NULL_SHARED_PTR(type) boost::shared_ptr<type>()

namespace dwarf
{
	// -- FIXME bring abstract_dieset into spec?
	namespace spec 
	{
		using namespace lib;
		using std::pair;
		using std::make_pair;
		
		class compile_unit_die;
		class program_element_die;
		struct basic_die;
		struct file_toplevel_die;
		struct member_die;
		std::ostream& operator<<(std::ostream& s, const basic_die& d);
		std::ostream& operator<<(std::ostream& s, const abstract_dieset& ds);

		class abstract_dieset : public boost::enable_shared_from_this<abstract_dieset>
        {
        public:
            /* This is all you need to denote a member of a dieset. */
            struct position
            {
            	abstract_dieset *p_ds;
                Dwarf_Off off;
/*				bool operator==(const position& arg) const 
				{ return this->p_ds == arg.p_ds && this->off == arg.off; }
				bool operator!=(const position& arg) const
				{ return !(*this == arg); }
				bool operator<=(const position& arg) const
				{ return this->p_ds < arg.p_ds
				|| (this->p_ds == arg.p_ds
				    && this->off <= arg.off); }
				bool operator<(const position& arg) const 
				{ return *this < arg && *this != arg; }
				bool operator>(const position& arg) const
				{ return !(*this <= arg); }
				bool operator>=(const position& arg) const
				{ return *this == arg || *this > arg; }*/
				
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
            }; // end class position
			typedef std::deque<position> path_type; 
            
            struct order_policy
            {
            	virtual int increment(position& pos, 
                	path_type& path) = 0;
                virtual int decrement(position& pos,
                	path_type& path) = 0;
			protected:
				const int behaviour; // used for equality comparison
				
				enum
				{
					 NO_BEHAVIOUR,
					 PREDICATED_BEHAVIOUR,
					 DEPTHFIRST_BEHAVIOUR, // FIXME: do I actually use the predicates in these?
					 BREADTHFIRST_BEHAVIOUR,
					 SIBLINGS_BEHAVIOUR
				};
				
				order_policy(int behaviour) : behaviour(behaviour) {}
			public:
				bool is_undefined() const { return behaviour == NO_BEHAVIOUR; }
				
				/* Equality of policies: 
				 * By default, policies are tested only for behavioural equality.
				 * Subtypes might want to narrow that to
				 * - all policies of the same class are equal
				 * or
				 * - policies must exhibit value-equality.
				 *
				 * Virtual dispatch asymmetry here:
				 * in the worst case this will get dispatched
				 * to a base class. But that only happens if
				 * the two policies are not of the same class.
				 * We don't want such to be equal anyway, so
				 * this is okay. 
				 */ 
				order_policy() : behaviour(0) {}
				
				virtual bool half_equal(const order_policy& arg) const
				{
					// this is the "most generous" equality relation
					return this->behaviour == arg.behaviour;
				}
				bool operator==(const order_policy& arg) const
				{
					return this->half_equal(arg) && arg.half_equal(*this);
				}
			}; // end class order_policy
            
            // FIXME: remember what this is for....
            struct die_pred : public std::unary_function<spec::basic_die, bool>
            {
				virtual bool operator()(const spec::basic_die& d) const = 0;
			};
            
            struct policy : order_policy, die_pred 
			{
			protected:
				policy(int behaviour) : order_policy(behaviour) {}
			public:
				policy() : order_policy(PREDICATED_BEHAVIOUR) {}
			};
			/* Dummy policy: okay to assign over! */
			struct dummy_policy : policy
			{
				// depth-first iteration
				int increment(position& pos,
					path_type& path) { assert(false); }
				int decrement(position& pos,
					path_type& path) { assert(false); }
			public:
				dummy_policy() : policy(NO_BEHAVIOUR) {}

				// always true
				bool operator()(const spec::basic_die& d) const { return true; }
			};
			static dummy_policy dummy_policy_sg;
			
            /* Default policy: depth-first order, all match. */
            struct default_policy : policy
            {
            	// depth-first iteration
            	int increment(position& pos,
                	path_type& path);
                int decrement(position& pos,
                	path_type& path);
			protected:
				default_policy(int behaviour) : policy(behaviour) {}
			public:
				default_policy() : policy(DEPTHFIRST_BEHAVIOUR) {}
				
                // always true
                bool operator()(const spec::basic_die& d) const { return true; }
			};
			static default_policy default_policy_sg;
			/* Breadth-first policy: breadth-first order, all match. */
			struct bfs_policy : policy
			{
				// helper functions
			protected:
				void enqueue_children(position& pos,
					path_type& path);
				void advance_to_next_sibling(position& pos,
					path_type& path);
				int take_from_queue_or_terminate(position& pos,
					path_type& path);
			
			public:
				// a queue of paths
				std::deque<path_type> m_queue;

				// breadth-first iteration
				int increment(position& pos,
					path_type& path);
				int decrement(position& pos,
					path_type& path);

				// special functions for tweaking the breadth-first exploration
				int increment_skipping_subtree(position& pos,
					path_type& path);
				int decrement_skipping_subtree(position& pos,
					path_type& path);
					
			protected:
				bfs_policy(int behaviour) : policy(behaviour), m_queue() {}
			public:
				bfs_policy() : policy(BREADTHFIRST_BEHAVIOUR), m_queue() {}
				
				bool half_equal(const order_policy& arg) const
				{
					return dynamic_cast<const bfs_policy*>(&arg)
						&& dynamic_cast<const bfs_policy*>(&arg)->m_queue == this->m_queue;
				}
				
				// always true
				bool operator()(const spec::basic_die& d) const { return true; }
			};
            //static bfs_policy bfs_policy_sg;
            // we *don't* create a bfs policy singleton because each BFS traversal
            // has to keep its own state (queue of nodes)
            /* Sibling policy: for children iterators */
            struct siblings_policy : policy 
            {
            	int increment(position& pos,
                	path_type& path);
                int decrement(position& pos,
                	path_type& path);
			protected:
				siblings_policy(int behaviour) : policy(behaviour) {}
			public:
				siblings_policy() : policy(SIBLINGS_BEHAVIOUR) {}
                
                // always true
                bool operator()(const spec::basic_die& d) const { return true; }
            };
            static siblings_policy siblings_policy_sg;
            
			struct position_and_path : position
			{
				path_type path_from_root;
				position_and_path(const position& pos): position(pos) {}
				position_and_path(const position& pos, const path_type& path)
				: position(pos), path_from_root(path) {}
			};
			
            struct basic_iterator_base 
            : public position_and_path
            {
				/* IDEA: if we extend this to include a shared_ptr to a DIE,
				 * with the invariant that this DIE is always at "position",
				 * or null if we're at the end,
				 * then we can perhaps save a lot of inefficient creation of DIEs.
				 * The traversal policies can update the pointer at the same time.
				 * This allows them to use the next-sibling, next-child and offset-
				 * based constructors, which are efficient on libdwarf .
				 * Currently, we use get_next_child, get_next_sibling, get_parent etc.,
				 * which are slow, and sometimes resort to dieset::find(), which is very bad.
				 * 
				 * Q. How does this interact with the factory?
				 * A. We shouldn't use the next-sibling, next-child and offset-based
				 *    constructors directly to make_shared a basic_die, because
				 *    we generally want to instantiate the class appropriate to the DIE's tag.
				 *    At the moment, the dieset invokes the factory, within
				 *    find() or operator[],
				 *    to encapsulate in a lib::die or encap::die in its ADT class.
				 *    There is no getting around some indirection here:
				 *    to identify the appropriate ADT class, we need a lib::die,
				 *    so we have to either allocate a second lib::die within the ADT class
				 *    or use a pointer. We go with the first, because on-stack lib::die creation
				 *    should be fast.
				 *    Now we need the iterator policy methods
				 *    (and the cursor-style navigation methods)
				 *    to be able to invoke the factory directly, asking for
				 *    next-child, next-sibling, offset, etc.. This means an API change,
				 *    or excessive friending. Certainly, the iterator implementation now takes
				 *    on activities that were previously localised in the dieset.
				 *    Maybe ask the dieset? 
				 *    "Please update me to the next child, next sibling, DIE at this offset..."?
				 *    Actually it's just the first two that are new; it already does offdie.
				 *    AND we want to AVOID offdie, because it doesn't preserve path information!
				 *    YES. This is key. Although offdie is efficient, it breaks our ability
				 *    to navigate. This is another reason to remove navigation from basic_die,
				 *    because providing it is inherently expensive.
				 * Q. Does this mean we want accessors (of children, reference attributes, ...)
				 *    to return iterators where they currently return pointers?
				 * A. This depends on the use-case. In general, children are okay because
				 *    we already do those through iterators. Reference attributes are a problem
				 *    because they inherently lose context. So maybe iterator_here is sadly
				 *    necessary, and parent_cache is still a good idea!
				 */
				
				typedef std::pair<Dwarf_Off, boost::shared_ptr<spec::basic_die> > pair_type;
				//policy& m_policy;
				policy *p_policy;
				/* We have to use a pointer because some clients 
				 * (like boost graph algorithms, where we are playing the edge_iterator)
				 * want to default-construct an iterator, then assign to it. Although
				 * we don't want to assign different policies, we do want to be able
				 * to construct with a dummy policy, then overwrite with a real policy. */

				bool operator==(const basic_iterator_base& arg) const
				{ return this->off == arg.off && this->p_ds == arg.p_ds
					&& (off == std::numeric_limits<Dwarf_Off>::max () || // HACK: == end() works
						*this->p_policy == *arg.p_policy);			   // for any policy
				}
				bool operator!=(const basic_iterator_base& arg) const { return !(*this == arg); }

				basic_iterator_base(abstract_dieset& ds, Dwarf_Off off,
					 const path_type& path_from_root,
					 policy& pol = default_policy_sg);
				basic_iterator_base() // path_from_root is empty
				: position_and_path((position){0, 0UL}), 
				  p_policy(&dummy_policy_sg) { canonicalize_position(); } 
				basic_iterator_base(const position_and_path& arg)
				: position_and_path(arg), p_policy(&dummy_policy_sg) { canonicalize_position(); } 

				typedef std::bidirectional_iterator_tag iterator_category;
				typedef spec::basic_die value_type;
				typedef Dwarf_Off difference_type;
				typedef spec::basic_die *pointer;
				typedef spec::basic_die& reference;
				
				basic_iterator_base& operator=(const basic_iterator_base& arg)
				{
					assert(p_policy->is_undefined() || *p_policy == *arg.p_policy);
					// HMM: do we want to deep-copy the policy? Not for now
					if (p_policy->is_undefined()) p_policy = arg.p_policy;
					this->path_from_root = arg.path_from_root;
					*static_cast<position*>(this) = arg;
					return *this;
				}
			}; // end basic_iterator_base

            struct iterator;
        	virtual iterator find(Dwarf_Off off) = 0;
            virtual iterator begin() = 0;
            virtual iterator end() = 0;
            virtual iterator begin(policy& pol);
            virtual iterator end(policy& pol) ;
            
            virtual path_type
            path_from_root(Dwarf_Off off) = 0;
            
            virtual boost::shared_ptr<spec::basic_die> operator[](Dwarf_Off off) const = 0;
            boost::shared_ptr<spec::basic_die> operator[](Dwarf_Off off)
            { return const_cast<const abstract_dieset *>(this)->operator[](off); }
           
            struct iterator
            : public boost::iterator_adaptor<iterator, // Derived
                    basic_iterator_base,        // Base
                    boost::shared_ptr<spec::basic_die>, // Value
                    boost::bidirectional_traversal_tag, // Traversal
                    boost::shared_ptr<spec::basic_die> // Reference
                > 
            {
                typedef boost::shared_ptr<spec::basic_die> Value;
            	typedef basic_iterator_base Base;
            	
                iterator() : iterator::iterator_adaptor_() {}
            	
                iterator(Base p) : iterator::iterator_adaptor_(p) {}
            	
                iterator(abstract_dieset& ds, Dwarf_Off off, 
                	const path_type& path_from_root, 
                    policy& pol = default_policy_sg)  
                : iterator::iterator_adaptor_(
                	basic_iterator_base(ds, off, path_from_root, pol)) {}

                iterator(const position& pos, 
                	const path_type& path_from_root,
                    policy& pol = default_policy_sg)  
                : iterator::iterator_adaptor_(basic_iterator_base(
                	*pos.p_ds, pos.off, path_from_root, pol)) {}

                 iterator(const position_and_path& pos, 
                    policy& pol = default_policy_sg)  
                : iterator::iterator_adaptor_(basic_iterator_base(
                	*pos.p_ds, pos.off, pos.path_from_root, pol)) {}
               
                // copy-like constructor that changes policy
                iterator(const iterator& arg, policy& pol) : 
                	iterator::iterator_adaptor_(basic_iterator_base(
                    	*arg.base().p_ds, arg.base().off, arg.base().path_from_root, pol)) {}
                            	
                void increment()        
                { this->base().p_policy->increment(this->base_reference(), this->base_reference().path_from_root); }
            	void decrement()        
                { this->base().p_policy->decrement(this->base_reference(), this->base_reference().path_from_root); }
                Value dereference() { return this->base().p_ds->operator[](this->base().off); }
                Value dereference() const { return this->base().p_ds->operator[](this->base().off); }
                position& pos() { return this->base_reference(); }
                const path_type& path() { return this->base_reference().path_from_root; }
				
				/* iterator_adaptor implements <, <=, >, >= using distance_to.
				 * However, that's more precise than we need: we can't cheaply
				 * compute distance_to, but we can cheaply compute a partial
				 * order among nodes sharing a parent. This is useful for our
				 * various children_iterator, in whose context it becomes a total
				 * order. We implement this partial order here. */
				bool shares_parent_pos(const iterator& i) const;
				bool operator<(const iterator& i) const;
				bool operator<=(const iterator& i) const
				{
					return this->shares_parent_pos(i)
					&&
					(i.base() == this->base()
						|| *this < i);
				}
				bool operator>(const iterator& i) const
				{ return this->shares_parent_pos(i) && !(*this <= i); }
				
				bool operator>=(const iterator& i) const
				{ return this->shares_parent_pos(i) && !(*this < i); }
				
					
            }; // end iterator
            virtual boost::shared_ptr<spec::file_toplevel_die> toplevel() = 0; /* NOT const */
            virtual const spec::abstract_def& get_spec() const = 0;
			
			// we return the host address size by default
			virtual Dwarf_Half get_address_size() const { return sizeof (void*); }
        };        
		// overloads moved outside struct definition above....
		inline bool operator==(const abstract_dieset::position& arg1, const abstract_dieset::position& arg2) 
		{ return arg1.p_ds == arg2.p_ds && arg1.off == arg2.off; }
		inline bool operator!=(const abstract_dieset::position& arg1, const abstract_dieset::position& arg2)
		{ return !(arg1 == arg2); }
		inline bool operator<=(const abstract_dieset::position& arg1, const abstract_dieset::position& arg2)
		{ return arg1.p_ds < arg2.p_ds
		|| (arg1.p_ds == arg2.p_ds
			&& arg1.off <= arg2.off); }
		inline bool operator<(const abstract_dieset::position& arg1, const abstract_dieset::position& arg2)
		{ return arg1 <= arg2 && arg1 != arg2; }
		inline bool operator>(const abstract_dieset::position& arg1, const abstract_dieset::position& arg2)
		{ return !(arg1 <= arg2); }
		inline bool operator>=(const abstract_dieset::position& arg1, const abstract_dieset::position& arg2)
		{ return arg1 == arg2 || arg1 > arg2; }
		
		struct basic_die : public boost::enable_shared_from_this<basic_die>
		{
			friend std::ostream& operator<<(std::ostream& s, const basic_die& d);
		
			virtual Dwarf_Off get_offset() const = 0;
			virtual Dwarf_Half get_tag() const = 0;
			
			virtual opt<std::string> get_name() const = 0;
			virtual const spec::abstract_def& get_spec() const = 0;

			// recover a shared_ptr to this DIE, from a plain this ptr
			// -- this is okay, because basic_dies are always refcounted
			boost::shared_ptr<basic_die> get_this();
			boost::shared_ptr<basic_die> get_this() const;

			// children
			// FIXME: iterator pair //virtual std::pair< > get_children() = 0;
			abstract_dieset::iterator children_begin() 
			{ 
				if (first_child_offset()) 
				{
					return this->get_first_child()->iterator_here(
						abstract_dieset::siblings_policy_sg);
				}
				else
				{
					return this->get_ds().end(abstract_dieset::siblings_policy_sg);
				}
			}
			abstract_dieset::iterator children_end() 
			{ return this->get_ds().end(abstract_dieset::siblings_policy_sg); }
			
			pair<abstract_dieset::iterator, abstract_dieset::iterator>
			children()
			{ return make_pair(children_begin(), children_end()); }

		protected: // public interface is to downcast
			virtual std::map<Dwarf_Half, encap::attribute_value> get_attrs() = 0;
			
		public:
			/* Navigation API. This is SLOW and therefore deprecated.
			 * The right place to do navigation is in iterators. */
			
			/* get_parent, normal and const */
			virtual boost::shared_ptr<basic_die> get_parent() __attribute__((deprecated)) = 0;
			boost::shared_ptr<basic_die> get_parent() const __attribute__((deprecated))
			{ return const_cast<basic_die *>(this)->get_parent(); }		   
			
			/* get_first_child, normal and const */
			virtual boost::shared_ptr<basic_die> get_first_child() __attribute__((deprecated)) = 0;
			boost::shared_ptr<basic_die> get_first_child() const __attribute__((deprecated))
			{ return const_cast<basic_die *>(this)->get_first_child(); }

			/* Functions prefixed "get_" and returning offsets throw No_entry exceptions.
			 * Those without the prefix return an optional.
			 * Those returning pointers return a null pointer. */
			 
			/* get_first_child_offset, normal and nothrow.
			 * There is no need for a non-const version, because we return by value. */
			virtual Dwarf_Off get_first_child_offset() const __attribute__((deprecated)) = 0;
			opt<Dwarf_Off> first_child_offset() const __attribute__((deprecated))
			{ 	try { return this->get_first_child_offset(); } 
				catch (No_entry) { return opt<Dwarf_Off>(); } }
			
			/* get_next_sibling, normal and onst */
			virtual boost::shared_ptr<basic_die> get_next_sibling() __attribute__((deprecated)) = 0;
			boost::shared_ptr<basic_die> get_next_sibling() const __attribute__((deprecated))
			{ return const_cast<basic_die *>(this)->get_next_sibling(); }

			/* get_next_sibling_offset, normal and nothrow */
			virtual Dwarf_Off get_next_sibling_offset() const __attribute__((deprecated)) = 0;
			opt<Dwarf_Off> next_sibling_offset() const __attribute__((deprecated))
			{ 	try { return this->get_next_sibling_offset(); } 
				catch (No_entry) { return opt<Dwarf_Off>(); } }
			
			/* arguably this also should be deprecated */
			virtual const abstract_dieset& get_ds() const __attribute__((deprecated))
			{ return const_cast<basic_die *>(this)->get_ds(); }
			virtual abstract_dieset& get_ds()  __attribute__((deprecated))= 0;

			/* this is deprecated too! it's inherently slow. */
			abstract_dieset::iterator 
			iterator_here(abstract_dieset::policy& pol = abstract_dieset::default_policy_sg)
			 __attribute__((deprecated)){ return abstract_dieset::iterator(
				this->get_ds(),
				this->get_offset(),
				this->get_ds().find(this->get_offset()).base().path_from_root, 
				pol); }
			// not a const function because may create backrefs -----------^^^

			// these also belong in navigation
			opt<std::vector<std::string> >
			ident_path_from_root() const __attribute__((deprecated));

			opt<std::vector<std::string> >
			ident_path_from_cu() const __attribute__((deprecated));

			// These two are similar, but still return a path even when
			// some nameless elements are missing. These therefore can't be
			// resolved, and need not be unique within a dieset, 
			// but can be useful for identifying corresponding
			// elements (e.g. shared type DIEs) across multiple diesets.
			std::vector< opt<std::string> >
			opt_ident_path_from_root() const __attribute__((deprecated));

			std::vector < opt<std::string> >
			opt_ident_path_from_cu() const __attribute__((deprecated));

			/* These, and other resolve()-style functions, are not const 
			 * because they need to be able to return references to mutable
			 * found DIEs. FIXME: provide const overloads. */
			boost::shared_ptr<basic_die> 
			nearest_enclosing(Dwarf_Half tag) const __attribute__((deprecated));
			boost::shared_ptr<spec::basic_die> 
			nearest_enclosing(Dwarf_Half tag) __attribute__((deprecated)); /* non-const version */
			boost::shared_ptr<compile_unit_die> 
			enclosing_compile_unit() __attribute__((deprecated));
			boost::shared_ptr<const compile_unit_die> 
			enclosing_compile_unit() const  __attribute__((deprecated))
			{ return boost::dynamic_pointer_cast<const compile_unit_die>(
					const_cast<basic_die *>(this)->enclosing_compile_unit()); 
			}

			boost::shared_ptr<basic_die>
			find_sibling_ancestor_of(boost::shared_ptr<basic_die> d) __attribute__((deprecated));
		};
		
		// FIXME: this class is work in progress, and maybe a bad idea!
		// HMM: maybe the least invasive way is just to always keep a pointer 
		// within the iterator. Since iterators need to create DIEs to get around,
		// this makes sense anyway.
		// BUT then we have to make iterator a template class!
		template <typename Die>
		struct cursor
		: shared_ptr<Die>,
		  abstract_dieset::iterator
		{
			typedef shared_ptr<Die> ptr_super;
			typedef abstract_dieset::iterator iterator_super;
			
			/* This class aims to provide "navigation done right"! */ 
			
			// constructors
			cursor(shared_ptr<Die> p_d, abstract_dieset::iterator i)
			: ptr_super(p_d), iterator_super(i) {}
			
			// accessors
			
			operator bool() 
			{ return *static_cast<iterator_super*>(*this) != get_ds().end(); }
			
			shared_ptr<Die>& operator*()
			{ return this->ptr_super; }
			
			template <typename OtherDie>
			cursor<OtherDie> as()
			{
				auto newp = dynamic_pointer_cast<OtherDie>(*static_cast<ptr_super*>(this));
				assert(newp);
				return cursor<OtherDie>(newp, *static_cast<iterator_super*>(this));
			}
			
			
			/* There is no need for const versions -- we don't use const die ptrs.
			 *
			 * There is the possibility of "move to" versions. These are 
			 * independent of policy, unlike the iterator's ++ and -- methods.
			 * However, there's not much value in supporting "move to"
			 *    e.g. my_cursor.move_to_parent();
			 * as opposed to just 
			 *         my_cursor = my_cursor.get_parent();
			 *     or  my_cursor = my_cursor.parent();
			 *           where "parent()" is the nothrow version, and will
			 *           move the cursor to the end sentinel if there is no parent.
			 * Note that assigning might require type conversion. We provide the "as"
			 * template method for this.
			 */
			
			// The "throw" versions are the most primitive, because the underlying
			// library will throw an exception which we just pass through.
			
			virtual cursor<basic_die> get_parent() = 0;
			virtual cursor<basic_die> get_first_child() = 0;
			virtual cursor<basic_die> get_next_sibling() = 0;
			
			// "offset"
			virtual Dwarf_Off get_parent_offset() = 0;
			virtual Dwarf_Off get_first_child_offset() = 0;
			virtual Dwarf_Off get_next_sibling_offset() = 0;
			
			// nothrow of the above
			// FIXME
			// -- offsets only for now
			opt<Dwarf_Off> parent_offset() 
			{ 	try { return this->get_parent_offset(); } 
				catch (No_entry) { return opt<Dwarf_Off>(); } }
			opt<Dwarf_Off> first_child_offset() 
			{ 	try { return this->get_first_child_offset(); } 
				catch (No_entry) { return opt<Dwarf_Off>(); } }
			opt<Dwarf_Off> next_sibling_offset() 
			{ 	try { return this->get_next_sibling_offset(); } 
				catch (No_entry) { return opt<Dwarf_Off>(); } }
			
			// get the dieset
			virtual abstract_dieset& get_ds() { assert(this->p_ds); return *this->p_ds; }

			// "ident path" methods assuming there should be an unbroken path;
			opt<std::vector<std::string> > ident_path_from_root();
			opt<std::vector<std::string> > ident_path_from_cu();
			
			// "ident path" methods returning a vector that may have blanks.
			std::vector< opt<std::string> > opt_ident_path_from_root();
			std::vector < opt<std::string> > opt_ident_path_from_cu();

			// "enclosing" methods
			cursor<basic_die> nearest_enclosing(Dwarf_Half tag);
			cursor<compile_unit_die> enclosing_compile_unit();

			// identify this DIE or a sibling of it, which is an ancestor of d
			cursor<basic_die> find_sibling_ancestor_of(boost::shared_ptr<basic_die> d);
		};

		struct with_static_location_die : public virtual basic_die
		{
			struct sym_binding_t 
			{ 
				Dwarf_Off file_relative_start_addr; 
				Dwarf_Unsigned size;
			};
			virtual encap::loclist get_static_location() const;
			virtual opt<Dwarf_Off> contains_addr(
				Dwarf_Addr file_relative_addr,
				sym_binding_t (*sym_resolve)(const std::string& sym, void *arg) = 0, 
				void *arg = 0) const;
			/*virtual std::vector<std::pair<Dwarf_Addr, Dwarf_Addr> >
			file_relative_extents(
				sym_binding_t (*sym_resolve)(const std::string& sym, void *arg) = 0, 
				void *arg = 0) const;*/
		};
		
		struct with_type_describing_layout_die : public virtual basic_die
		{
			virtual opt<boost::shared_ptr<spec::type_die> > get_type() const = 0;
		};

		struct with_dynamic_location_die : public virtual with_type_describing_layout_die
		{
			virtual opt<Dwarf_Off> contains_addr(
					Dwarf_Addr absolute_addr,
					Dwarf_Signed instantiating_instance_addr,
					Dwarf_Off dieset_relative_ip,
					dwarf::lib::regs *p_regs = 0) const = 0;
			/* We define two variants of the contains_addr logic: 
			 * one suitable for stack-based locations (fp/variable)
			 * and another for object-based locations (member/inheritance)
			 * and each derived class should pick one! */
		protected:
			opt<Dwarf_Off> contains_addr_on_stack(
					Dwarf_Addr absolute_addr,
					Dwarf_Signed instantiating_instance_addr,
					Dwarf_Off dieset_relative_ip,
					dwarf::lib::regs *p_regs = 0) const;
			opt<Dwarf_Off> contains_addr_in_object(
					Dwarf_Addr absolute_addr,
					Dwarf_Signed instantiating_instance_addr,
					Dwarf_Off dieset_relative_ip,
					dwarf::lib::regs *p_regs = 0) const;
		public:
			virtual boost::shared_ptr<spec::program_element_die> 
			get_instantiating_definition() const;
			
			virtual Dwarf_Addr calculate_addr(
				Dwarf_Addr instantiating_instance_location,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs = 0) const = 0;
			
			/** This gets a location list describing the location of the thing, 
			    assuming that the instantiating_instance_location has been pushed
			    onto the operand stack. */
			virtual encap::loclist get_dynamic_location() const = 0;
		protected:
			/* ditto */
			virtual Dwarf_Addr calculate_addr_on_stack(
				Dwarf_Addr instantiating_instance_location,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs = 0) const;
			virtual Dwarf_Addr calculate_addr_in_object(
				Dwarf_Addr instantiating_instance_location,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs = 0) const;
		public:
			
			/* virtual Dwarf_Addr calculate_addr(
				Dwarf_Signed frame_base_addr,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs = 0) const;*/
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
#define forward_decl(t) class t ## _die;
#define declare_base(base) virtual base ## _die
#define base_fragment(base) base ## _die(ds, p_d) {} /* unused */
#define initialize_base(fragment) fragment ## _die(ds, p_d)
#define constructor(fragment) 
        
#define begin_class(fragment, base_inits, ...) \
	struct fragment ## _die : __VA_ARGS__ { \
    	constructor(fragment)
/* #define base_initializations(...) __VA_ARGS__ */
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
#define stored_type_address dwarf::encap::attribute_value::address
#define stored_type_refdie boost::shared_ptr<spec::basic_die> 
#define stored_type_refdie_is_type boost::shared_ptr<spec::type_die> 
#define stored_type_rangelist dwarf::encap::rangelist

#define attr_optional(name, stored_t) \
	virtual opt<stored_type_ ## stored_t> get_ ## name() const = 0; \
  	opt<stored_type_ ## stored_t> name() const { return get_ ## name(); }

#define super_attr_optional(name, stored_t)

#define attr_mandatory(name, stored_t) \
	virtual stored_type_ ## stored_t get_ ## name() const = 0; \
  	stored_type_ ## stored_t name() const { return get_ ## name(); } 

#define super_attr_mandatory(name, stored_t)

template<Dwarf_Half Tag>
struct has_tag : public std::unary_function<boost::shared_ptr<spec::basic_die>, bool>
{
	bool operator()(const boost::shared_ptr<spec::basic_die> arg) const
    { 
    	//std::cerr << "testing whether die at " << std::hex << arg->get_offset() << std::dec 
        //	<< " has tag " << Tag << std::endl;
    	return arg->get_tag() == Tag; 
    }
};

template <class Iter>
struct with_iterator_partial_order : public Iter
{
	typedef with_iterator_partial_order self;
	typedef signed long difference_type;
	
	/* HACK: add back in the comparison operators from abstract_dieset::iterator. 
	 * We need this because transform_iterator and
	 * filter_iterator and iterator_adaptor all hide our operator< (et al)
	 * with one based on distance_to(), which we can't define efficiently 
	 * (noting that we have only a partial order anyway, so it'd be a partial
	 * function at best). */
	bool operator<(const self& i) const { return this->base().base() < i.base().base(); }
	bool operator<=(const self& i) const { return this->base().base() <= i.base().base(); }
	bool operator>(const self& i) const { return this->base().base() > i.base().base(); }
	bool operator>=(const self& i) const { return this->base().base() >= i.base().base(); }
	
	// let's try this fancy C++0x constructor forwarding then
	// using Iter::Iter;
	// HACK: the above doesn't yet work in g++, so do "perfect forwarding" instead...
	template <typename... Args> 
	with_iterator_partial_order(Args&&... args)
	// : Iter(args...) {}
	: Iter(std::forward<Args>(args)...) {}
};

//typedef std::unary_function<boost::shared_ptr<spec::basic_die>, boost::shared_ptr<spec:: arg ## _die > > type_of_dynamic_pointer_cast;
#define child_tag(arg) \
	typedef boost::shared_ptr<spec:: arg ## _die >(*type_of_dynamic_pointer_cast_ ## arg)(boost::shared_ptr<spec::basic_die> const&); \
	typedef boost::filter_iterator<has_tag< DW_TAG_ ## arg >, spec::abstract_dieset::iterator> arg ## _filter_iterator; \
	typedef boost::transform_iterator<type_of_dynamic_pointer_cast_ ## arg, arg ## _filter_iterator> arg ## _transform_iterator; \
	typedef with_iterator_partial_order<arg ## _transform_iterator> arg ## _iterator; \
    arg ## _iterator arg ## _children_begin() \
	{ return \
		arg ## _iterator( \
		arg ## _transform_iterator(\
		arg ## _filter_iterator(children_begin(), children_end()), \
		 boost::dynamic_pointer_cast<spec:: arg ## _die> \
		)); } \
    arg ## _iterator arg ## _children_end() \
	{ return \
		arg ## _iterator(\
		arg ## _transform_iterator(\
		arg ## _filter_iterator(children_end(), children_end()), \
		boost::dynamic_pointer_cast<spec:: arg ## _die> \
		)); }

        struct file_toplevel_die : public virtual with_named_children_die
        {
            struct is_visible
            {
                bool operator()(boost::shared_ptr<spec::basic_die> p) const;
            };

            template <typename Iter>
            boost::shared_ptr<basic_die>
            visible_resolve(Iter path_pos, Iter path_end);
            
            virtual boost::shared_ptr<basic_die>
            visible_named_child(const std::string& name);
            
            child_tag(compile_unit)
        };
                
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
        virtual opt<Dwarf_Unsigned> calculate_byte_size() const;
        virtual bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
		virtual boost::shared_ptr<type_die> get_concrete_type() const;
		virtual boost::shared_ptr<type_die> get_unqualified_type() const;
        boost::shared_ptr<type_die> get_concrete_type();
end_class(type)
begin_class(type_chain, base_initializations(initialize_base(type)), declare_base(type))
        attr_optional(type, refdie_is_type)
        opt<Dwarf_Unsigned> calculate_byte_size() const;
        boost::shared_ptr<type_die> get_concrete_type() const;
end_class(type_chain)
begin_class(qualified_type, base_initializations(initialize_base(type_chain)), declare_base(type_chain))
        virtual boost::shared_ptr<type_die> get_unqualified_type() const;
        boost::shared_ptr<type_die> get_unqualified_type();
end_class(qualified_type)
begin_class(with_data_members, base_initializations(initialize_base(type)), declare_base(type))
        child_tag(member)
end_class(with_data_members)

#define has_stack_based_location \
	opt<Dwarf_Off> contains_addr( \
                    Dwarf_Addr aa, \
                    Dwarf_Signed fb, \
                    Dwarf_Off dr_ip, \
                    dwarf::lib::regs *p_regs) const \
					{ return contains_addr_on_stack(aa, fb, dr_ip, p_regs); } \
	Dwarf_Addr calculate_addr( \
				Dwarf_Addr fb, \
				Dwarf_Off dr_ip, \
				dwarf::lib::regs *p_regs = 0) const \
				{ return calculate_addr_on_stack(fb, dr_ip, p_regs); } \
	encap::loclist get_dynamic_location() const;
#define has_object_based_location \
	opt<Dwarf_Off> contains_addr( \
                    Dwarf_Addr aa, \
                    Dwarf_Signed io, \
                    Dwarf_Off dr_ip, \
                    dwarf::lib::regs *p_regs) const \
					{ return contains_addr_in_object(aa, io, dr_ip, p_regs); } \
	Dwarf_Addr calculate_addr( \
				Dwarf_Addr io, \
				Dwarf_Off dr_ip, \
				dwarf::lib::regs *p_regs = 0) const \
				{ return calculate_addr_in_object(io, dr_ip, p_regs); } \
	encap::loclist get_dynamic_location() const;
#define extra_decls_compile_unit \
		opt<Dwarf_Unsigned> implicit_array_base() const; \
		virtual Dwarf_Half get_address_size() const { return this->get_ds().get_address_size(); } \
		virtual std::string source_file_name(unsigned o) const = 0; \
		virtual unsigned source_file_count() const = 0;
#define extra_decls_subprogram \
        opt< std::pair<Dwarf_Off, boost::shared_ptr<spec::with_dynamic_location_die> > > \
        contains_addr_as_frame_local_or_argument( \
                    Dwarf_Addr absolute_addr, \
                    Dwarf_Off dieset_relative_ip, \
                    Dwarf_Signed *out_frame_base, \
                    dwarf::lib::regs *p_regs = 0) const; \
        bool is_variadic() const;
#define extra_decls_variable \
        bool has_static_storage() const; \
		has_stack_based_location
#define extra_decls_formal_parameter \
		has_stack_based_location
#define extra_decls_array_type \
		opt<Dwarf_Unsigned> element_count() const; \
        opt<Dwarf_Unsigned> calculate_byte_size() const; \
        bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_pointer_type \
		boost::shared_ptr<type_die> get_concrete_type() const; \
        opt<Dwarf_Unsigned> calculate_byte_size() const; \
        bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_reference_type \
		boost::shared_ptr<type_die> get_concrete_type() const; \
        opt<Dwarf_Unsigned> calculate_byte_size() const; \
        bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_base_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_structure_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_union_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_class_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_enumeration_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_subroutine_type \
		bool is_rep_compatible(boost::shared_ptr<type_die> arg) const;
#define extra_decls_member \
		opt<Dwarf_Unsigned> byte_offset_in_enclosing_type() const; \
		has_object_based_location
#define extra_decls_inheritance \
		opt<Dwarf_Unsigned> byte_offset_in_enclosing_type() const; \
		has_object_based_location

#include "dwarf3-adt.h"

#undef extra_decls_inheritance
#undef extra_decls_member
#undef extra_decls_subroutine_type
#undef extra_decls_enumeration_type
#undef extra_decls_class_type
#undef extra_decls_union_type
#undef extra_decls_structure_type
#undef extra_decls_base_type
#undef extra_decls_reference_type
#undef extra_decls_pointer_type
#undef extra_decls_array_type
#undef extra_decls_variable
#undef extra_decls_formal_parameter
#undef extra_decls_compile_unit

#undef forward_decl
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
#undef child_tag

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
		
        template <typename Iter>
        boost::shared_ptr<basic_die>
        file_toplevel_die::visible_resolve(Iter path_pos, Iter path_end)
        {
            is_visible visible;
            boost::shared_ptr<basic_die> found;
            for (auto i_cu = this->compile_unit_children_begin();
                    i_cu != this->compile_unit_children_end(); i_cu++)
            {
                if (path_pos == path_end) { found = this->get_this(); break; }
                auto found_under_cu = (*i_cu)->named_child(*path_pos);

                Iter cur_plus_one = path_pos; cur_plus_one++;
                if (cur_plus_one == path_end && found_under_cu
                        && visible(found_under_cu))
                { found = found_under_cu; break; }
                else
                {
                    if (!found_under_cu || 
                            !visible(found_under_cu)) continue;
                    auto p_next_hop =
                        boost::dynamic_pointer_cast<with_named_children_die>(found_under_cu);
                    if (!p_next_hop) continue; // try next compile unit
                    else 
                    { 
                        auto found_recursive = p_next_hop->resolve(++path_pos, path_end);
                        if (found_recursive) { found = found_recursive; break; }
                        // else continue
                    }
                }
            }
            if (found) return found; else return boost::shared_ptr<basic_die>();
        }
		
// 		class factory
// 		{
// 		public:
// 			template<class Rep>  // specialise this on a per-rep basis
// 			static typename Rep::factory_type& 
// 				get_factory(const dwarf::spec::abstract_def& spec) //__attribute__((no_return))
// 			{ throw Bad_spec(); }
// 		};
	}
}

#endif
