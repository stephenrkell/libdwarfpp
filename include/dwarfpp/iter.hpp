/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * iter.hpp: DIE tree iterators
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_ITER_HPP_
#define DWARFPP_ITER_HPP_

#include <iostream>
#include <utility>
#include <functional>
#include <memory>
#include <cassert>
#include <srk31/selective_iterator.hpp>
#include <srk31/concatenating_iterator.hpp>
#include <srk31/transform_iterator.hpp>

#include "root.hpp"

namespace dwarf
{
	using std::string;
	
	namespace core
	{
		struct iterator_base : private virtual abstract_die
		{
			/* Everything that calls a libdwarf constructor-style function
			 * needs to be a friend, so that it can raw_handle() to supply
			 * the argument to libdwarf. Or does it? Just do get_handle()
			 * and downcast it to Die. */
			friend class root_die;
		private:
			/* This stuff is the Rep that can be abstracted out. */
			// union
			// {
				/* These guys are mutable because they need to be modifiable 
				 * even by, e.g., the copy constructor modifying its argument.
				 * The abstract value of the iterator isn't changed in such
				 * operations, but its representation must be. */
				mutable Die cur_handle; // to copy this, have to upgrade it
				mutable root_die::ptr_type cur_payload; // payload = handle + shared count + extra state
			// };
			mutable enum { HANDLE_ONLY, WITH_PAYLOAD } state;
			/* ^-- this is the absolutely key design point that makes this code fast. 
			 * An iterator can either be a libdwarf handle, or a pointer to some
			 * refcounted state (including such a handle, and maybe other cached stuff). */
		public:
			string summary() const { return this->abstract_die::summary(); }
			abstract_die& get_handle() const
			{
				if (!is_real_die_position()) 
				{ assert(!cur_handle.handle && !cur_payload); return cur_handle; }
				switch (state)
				{
					case HANDLE_ONLY: return cur_handle;
					case WITH_PAYLOAD: {
						if (cur_payload->d.handle) return cur_payload->d;
						else return dynamic_cast<in_memory_abstract_die&>(*cur_payload);
					}
					default: assert(false);
				}
			}
			root_die::ptr_type fast_deref() const
			{ if (state == WITH_PAYLOAD) return cur_payload; else return nullptr; }
		private:
			/* This is more general-purpose stuff. */
			mutable opt<unsigned short> m_opt_depth;
			root_die *p_root;
			
		public:
			// we like to be default-constructible, BUT 
			// we are in an unusable state after this constructor
			// -- the same state as end()!
			iterator_base()
			 : cur_handle(Die(nullptr, nullptr)), cur_payload(nullptr), state(HANDLE_ONLY), m_opt_depth(), p_root(nullptr) {}
			
			static const iterator_base END; // sentinel definition
			
			/* root position is encoded by null handle, null payload
			 * and non-null root pointer.
			 * cf. end position, which has null root pointer. */
			bool is_root_position() const 
			{ return p_root && !cur_handle.handle && !cur_payload; }
			bool is_end_position() const 
			{ return !p_root && !cur_handle.handle && !cur_payload; }
			bool is_real_die_position() const 
			{ return !is_root_position() && !is_end_position(); }
			bool is_under(const iterator_base& i) const
			{ return p_root->is_under(*this, i); }
			
			// this constructor sets us up at begin(), i.e. the root DIE position
			explicit iterator_base(root_die& r)
			 : cur_handle(nullptr, nullptr), cur_payload(nullptr), state(HANDLE_ONLY), m_opt_depth(0), p_root(&r) 
			{
				assert(this->is_root_position());
			}
			
			// this constructor sets us up using a handle -- 
			// this does the exploitation of the sticky set
			iterator_base(abstract_die&& d, opt<unsigned short> opt_depth, root_die& r)
			 : cur_handle(Die(nullptr, nullptr)), cur_payload(nullptr) // will be replaced in function body...
			{
				// get the offset of the handle we've been passed
				Dwarf_Off off = d.get_offset(); 
				// is it an existing live DIE?
				auto found = r.live_dies.find(off);
				if (found != r.live_dies.end())
				{
					// exists; may be sticky
					cur_handle = Die(nullptr, nullptr);
					state = WITH_PAYLOAD;
					cur_payload = found->second;
					assert(cur_payload);
					//m_opt_depth = found->second->get_depth(); assert(depth == m_opt_depth);
					//p_root = &found->second->get_root();
				}
				else if (r.is_sticky(d))
				{
					// should be sticky, so should exist, but does not exist yet -- use the factory
					cur_handle = Die(nullptr, nullptr);
					state = WITH_PAYLOAD;
					cur_payload = factory::for_spec(d.get_spec(r)).make_payload(std::move(d), r);
					assert(cur_payload);
					r.sticky_dies[off] = cur_payload;
				}
				else
				{
					// does not exist, and not sticky, so need not exist; stick with handle
					cur_handle = std::move(dynamic_cast<Die&&>(d).handle);
					state = HANDLE_ONLY;
				}
				m_opt_depth = opt_depth; // now shared by both cases
				p_root = &r;
			}
			
			/* Construct us from a basic_die? Why not.... */
			iterator_base(const basic_die& d, opt<unsigned short> opt_depth = opt<unsigned short>())
			 : cur_handle(Die(nullptr, nullptr)), cur_payload(const_cast<basic_die*>(&d))
			{
				state = WITH_PAYLOAD;
				m_opt_depth = opt_depth;
				p_root = &d.get_root();
			}
			
		public:
			// copy constructor
			iterator_base(const iterator_base& arg)
				/* We used to always make payload on copying.
				 * We no longer do that; instead, if we're a handle, just ask libdwarf
				 * for a fresh handle. This still does an allocation (in libdwarf, not
				 * in our code) and will cause our code to do *another* allocation if
				 * we dereference the iterator -- UNLESS the DIE at that offset has
				 * already been materialised via another iterator, in which case we'll
				 * find it via live_dies.
				 * 
				 * In particular, there is no way to prevent multiple handles
				 * pointing at the same DIE independently. When we upgrade one of
				 * them, we have no way of knowing to upgrade the others. We
				 * cannot rely on a payload's handle being the only live handle
				 * on that DIE (but we can rely on its being the only payload). */
			 : cur_handle(nullptr, nullptr),
			   m_opt_depth(arg.m_opt_depth), 
			   p_root(arg.is_end_position() ? nullptr : &arg.get_root())
			{
				if (arg.is_end_position())
				{
					// NOTE: must put us in the same state as the default constructor
					this->cur_payload = nullptr;
					this->state = HANDLE_ONLY;
					assert(this->is_end_position());
				}				
				else if (arg.is_root_position())
				{
					this->cur_payload = nullptr;
					this->state = HANDLE_ONLY; // the root DIE can get away with this (?)
					assert(this->is_root_position());
				}
				else switch (arg.state)
				{
					case WITH_PAYLOAD:
						/* Copy the payload pointer */
						this->state = WITH_PAYLOAD;
						this->cur_payload = arg.cur_payload;
						break;
					case HANDLE_ONLY: {
						Die tmp(*p_root, arg.offset_here());
						this->state = HANDLE_ONLY;
						this->cur_handle = std::move(tmp);
						this->cur_payload = nullptr;
					} break;
					default: assert(false);
				}
			}
			
			// ... but we prefer to move them
			iterator_base(iterator_base&& arg)
			 : cur_handle(std::move(arg.cur_handle)),
			   cur_payload(arg.cur_payload),
			   state(arg.state),
			   m_opt_depth(arg.m_opt_depth),
			   p_root(arg.is_end_position() ? nullptr : &arg.get_root())
			{}
			
			// copy assignment
			/* FIXME: instead of making payload in copy-construction and copy-assignment,
			 * we could delay it so that it only happens on dereference, and use libdwarf's
			 * offdie to get a fresh handle at the same offset (but CHECK that it really is fresh).
			 * HMM. That would be problematic because on deref, there might be other handles
			 * around which won't get upgraded to with-payload. Hmm. That might be okay, though. */
			iterator_base& operator=(const iterator_base& arg) // does the upgrade...
			{
				// FIXME: do copy-and-swap here
				this->m_opt_depth = arg.m_opt_depth;
				this->p_root = arg.p_root;
				// as with the copy constructor, get the duplicate from libdwarf
				if (arg.is_end_position())
				{
					// NOTE: must put us in the same state as the default constructor
					this->cur_payload = nullptr;
					this->cur_handle = std::move(Die(nullptr, nullptr));
					this->state = HANDLE_ONLY;
					assert(this->is_end_position());
				}				
				else if (arg.is_root_position())
				{
					this->cur_payload = nullptr;
					this->cur_handle = std::move(Die(nullptr, nullptr));
					this->state = HANDLE_ONLY; // the root DIE can get away with this (?)
					assert(this->is_root_position());
				}
				else switch (arg.state)
				{
					case WITH_PAYLOAD:
						/* Copy the payload pointer */
						this->state = WITH_PAYLOAD;
						this->cur_payload = arg.cur_payload;
						break;
					case HANDLE_ONLY: {
						this->state = HANDLE_ONLY;
						Die tmp(*p_root, arg.offset_here());
						this->cur_handle = std::move(tmp);
						this->cur_payload = nullptr;
					} break;
					default: assert(false);
				}

				return *this;
			}
			
			// move assignment
			iterator_base& operator=(iterator_base&& arg)
			{
				this->cur_handle = std::move(arg.cur_handle);
				this->cur_payload = std::move(arg.cur_payload);
				this->state = std::move(arg.state);
				this->m_opt_depth = std::move(arg.m_opt_depth);
				this->p_root = std::move(arg.p_root);
				return *this;
			}
			
			/* BUT note: these constructors are not enough, because unless we want
			 * users to construct raw handles, the user has no nice way of constructing
			 * a new iterator that is a sibling/child of an existing one. Note that
			 * this doesn't imply copying... we might want to create a new handle. */
		
			// convenience
			root_die& root() { assert(p_root); return *p_root; }
			root_die& root() const { assert(p_root); return *p_root; }
			root_die& get_root() { assert(p_root); return *p_root; }
			root_die& get_root() const { assert(p_root); return *p_root; }
		
			Dwarf_Off offset_here() const; 
			Dwarf_Half tag_here() const;
			
			opt<string> 
			name_here() const;
			opt<string> 
			global_name_here() const;
			
			inline spec& spec_here() const;
			
		public:
			/* implement the abstract_die interface
			 *  -- NOTE: some methods are private because they 
				   only work in the Die handle case, 
				   not the "payload + in_memory" case (when get_handle() returns null). 
				FIXME: should we change this? */
			inline Dwarf_Off get_offset() const { return offset_here(); }
			inline Dwarf_Half get_tag() const { return tag_here(); }
			// helper for raw names -> std::string names
		private:
			inline unique_ptr<const char, string_deleter> get_raw_name() const 
			{ return dynamic_cast<Die&>(get_handle()).name_here(); } 
			inline opt<string> get_name() const 
			{ return /*opt<string>(string(get_raw_name().get())); */ get_handle().get_name(); }
		public:
			inline Dwarf_Off get_enclosing_cu_offset() const 
			{ return enclosing_cu_offset_here(); }
			inline bool has_attr(Dwarf_Half attr) const { return has_attr_here(attr); }
			inline encap::attribute_map copy_attrs() const
			{
				if (is_root_position()) return encap::attribute_map();
				if (state == HANDLE_ONLY)
				{
					return encap::attribute_map(
						AttributeList(dynamic_cast<Die&>(get_handle())),
						dynamic_cast<Die&>(get_handle()), 
						dynamic_cast<Die&>(get_handle()).get_constructing_root()
					);
				}
				else
				{
					assert(state == WITH_PAYLOAD);
					return cur_payload->all_attrs();
				}
			}
			inline encap::attribute_value attr(Dwarf_Half attr) const
			{
				if (is_root_position()) return encap::attribute_value();
				if (state == HANDLE_ONLY)
				{
					AttributeList l(dynamic_cast<Die&>(get_handle()));
					for (auto i = l.copied_list.begin(); i != l.copied_list.end(); ++i)
					{
						if (i->attr_here() == attr)
						{
							return encap::attribute_value(*i, dynamic_cast<Die&>(get_handle()), get_root());
						}
					}
					return encap::attribute_value();
				} 
				else 
				{
					assert(state == WITH_PAYLOAD);
					return cur_payload->attr(attr);
				}
			}
			inline spec& get_spec(root_die& r) const { return spec_here(); }
			
		public:
			bool has_attr_here(Dwarf_Half attr) const;
			bool has_attribute_here(Dwarf_Half attr) const { return has_attr_here(attr); }
			
			AttributeList::handle_type attributes_here()
			{ return AttributeList::try_construct(dynamic_cast<Die&>(get_handle())); }
			AttributeList::handle_type attrs_here() { return attributes_here(); }
			AttributeList::handle_type attributes_here() const 
			{ return AttributeList::try_construct(dynamic_cast<Die&>(get_handle())); }
			AttributeList::handle_type attrs_here() const { return attributes_here(); }
			
			// want an iterators-style interface?
			// or an associative-style operator[] interface?
			// or both?
			// make payload include a deep copy of the attrs state? 
			// HMM, yes, this feels correct. 
			// Clients that want to scan attributes in a lightweight libdwarf way
			// (i.e. not benefiting from caching/ custom representations / 
			// utility methods implemented using payload state) can use 
			// the AttributeList interface.
					
			// some fast topological queries
			Dwarf_Off enclosing_cu_offset_here() const
			{ return get_handle().get_enclosing_cu_offset(); }
			inline unsigned short depth() const;
			inline opt<unsigned short> maybe_depth() const { return m_opt_depth; }
			unsigned short get_depth() const { return depth(); }
			
			// access to children, siblings, parent, ancestors
			// -- these wrap the various Die constructors
			iterator_base nearest_enclosing(Dwarf_Half tag) const;
			iterator_base parent() const;
			iterator_base first_child() const;
			iterator_base next_sibling() const;			
			iterator_base named_child(const string& name) const;
			// + resolve? no, I have put resolve stuff on the root_die
			inline iterator_df<compile_unit_die> enclosing_cu() const;
			
			template <typename Payload>
			bool is_a() const { return is_a_t<Payload>()(*this); }
			
			// convenience for checked construction 
			// of typed iterators
			template <typename Payload, template <typename InnerPayload> class Iter = iterator_df >
			inline Iter<Payload> as_a() const
			{
				if (this->is_a<Payload>()) return Iter<Payload>(*this);
				else return END;
			}
			
			inline sequence<iterator_sibs<> >
			children_here();
			inline sequence<iterator_sibs<> >
			children_here() const;
			// synonyms
			inline sequence<iterator_sibs<> > children();
			inline sequence<iterator_sibs<> > children() const;
			
			// we're just the base, not the iterator proper, 
			// so we don't have increment(), decrement()
			
			bool operator==(const iterator_base& arg) const
			{
				if (!p_root && !arg.p_root) return true; // END
				// now we're either root or "real". Handle the case where we're root. 
				if (state == HANDLE_ONLY && !cur_handle.handle.get() 
					&& arg.state == HANDLE_ONLY && !arg.cur_handle.handle.get()) return p_root == arg.p_root;
				if (state == WITH_PAYLOAD && arg.state == WITH_PAYLOAD &&
					cur_payload == arg.cur_payload) return true;
				if (m_opt_depth && arg.m_opt_depth 
					&& m_opt_depth != arg.m_opt_depth) return false;
				// NOTE: we can't compare handles or payload addresses, because 
				// we can ask libdwarf for a fresh handle at the same offset, 
				// and it might be distinct.
				return p_root == arg.p_root
					&& offset_here() == arg.offset_here();
			}
			bool operator!=(const iterator_base& arg) const	
			{ return !(*this == arg); }
			
			operator bool() const
			{ return *this != END; }
			
			/* iterator_adaptor implements <, <=, >, >= using distance_to. 
			 * This is too expensive to compute in general, so we hide this 
			 * by defining our own comparison simply as offset comparison. */
			bool operator<(const iterator_base& arg) const
			{
				return this->offset_here() < arg.offset_here();
			}
			
			basic_die& dereference() const 
			{
				assert(this->operator bool());
				return *get_root().make_payload(*this);
			}

			// printing
			void print(std::ostream& s, unsigned indent_level = 0) const;
			void print_with_attrs(std::ostream& s, unsigned indent_level = 0) const;
			friend std::ostream& operator<<(std::ostream& s, const iterator_base& it);
		}; 
		/* END class iterator_base */
		
		/* Now we can define that pesky template operator function. 
		 * The factory exposes a dummy method (NOT type-level though! it's 
		 * polymorphic!) that returns us a fake singleton of any instantiable  
		 * DIE type. */
		template <typename Payload>
		inline bool is_a_t<Payload>::operator()(const iterator_base& it) const
		{
			return dynamic_cast<Payload *>(
				factory::for_spec(it.spec_here()).dummy_for_tag(it.tag_here())
			) ? true : false;
		}
		

		template <typename Iter, typename Pred>
		inline 
		pair<
			typename subseq_t<Iter, Pred>::filtered_iterator, 
			typename subseq_t<Iter, Pred>::filtered_iterator
		>
		subseq_t<Iter, Pred>::operator()(const pair<Iter, Iter>& in_seq)
		{ 
			filtered_iterator first(in_seq.first, in_seq.second, this->m_pred);
			filtered_iterator second(in_seq.second, in_seq.second, this->m_pred);
			return make_pair(
				std::move(first), std::move(second)
			);
		}

		template <typename Iter, typename Pred>
		inline 
		pair<
			typename subseq_t<Iter, Pred>::filtered_iterator, 
			typename subseq_t<Iter, Pred>::filtered_iterator
		>
		subseq_t<Iter, Pred>::operator()(pair<Iter, Iter>&& in_seq)
		{ 
			/* GAH. We can only move if .second == iterator_base::END, because 
			 * otherwise we have to duplicate the end iterator into both 
			 * filter iterators. */
			if (in_seq.second == iterator_base::END)
			{
				filtered_iterator first(std::move(in_seq.first), iterator_base::END, this->m_pred);
				filtered_iterator second(std::move(in_seq.second), iterator_base::END, this->m_pred);
				return make_pair(
					std::move(first), std::move(second)
				);
			}
			else
			{
				filtered_iterator first(in_seq.first, in_seq.second, this->m_pred);
				filtered_iterator second(in_seq.second, in_seq.second, this->m_pred);
				return make_pair(
					std::move(first), std::move(second)
				);
			}
		}

		template <typename Iter, typename Payload>
		inline 
		pair<
			typename subseq_t<Iter, is_a_t<Payload> >::transformed_iterator, 
			typename subseq_t<Iter, is_a_t<Payload> >::transformed_iterator
		> 
 		subseq_t<Iter, is_a_t<Payload> >::operator()(pair<Iter, Iter>&& in_seq)
		{
			/* GAH. We can only move if .second == iterator_base::END, because 
			 * otherwise we have to duplicate the end sentinel into both 
			 * filter iterators. */
			if (in_seq.second == iterator_base::END)
			{
				// NOTE: this std::move is all for nothing at the moment because 
				// transform_iterator doesn't implement move constuctor/assignment.

				auto filtered_first = filtered_iterator(std::move(in_seq.first), iterator_base::END);
				auto filtered_second = filtered_iterator(std::move(in_seq.second), iterator_base::END);

				return make_pair(
					std::move(transformed_iterator(
						std::move(filtered_first)
					)),
					std::move(transformed_iterator(
						std::move(filtered_second)
					))
				);
			} else { auto tmp = in_seq; return operator()(tmp); } // copying version
		}

		/* Make sure we can construct any iterator from an iterator_base. 
		 * In the case of BFS it may be expensive. */
		template <typename DerefAs /* = basic_die */>
		struct iterator_df : public iterator_base,
							 public boost::iterator_facade<
							   iterator_df<DerefAs>
							 , DerefAs
							 , boost::forward_traversal_tag
							 , DerefAs& //boost::use_default /* Reference */
							 , Dwarf_Signed /* difference */
							 >
		{
			typedef iterator_df<DerefAs> self;
			typedef DerefAs DerefType;
			friend class boost::iterator_core_access;
			
			iterator_base& base_reference()
			{ return static_cast<iterator_base&>(*this); }
			const iterator_base& base() const
			{ return static_cast<const iterator_base&>(*this); }
			
			iterator_df() : iterator_base() {}
			iterator_df(const iterator_base& arg)
			 : iterator_base(arg) {}// this COPIES so avoid
			iterator_df(iterator_base&& arg)
			 : iterator_base(arg) {}
			
			iterator_df& operator=(const iterator_base& arg) 
			{ this->base_reference() = arg; return *this; }
			iterator_df& operator=(iterator_base&& arg) 
			{ this->base_reference() = std::move(arg); return *this; }
			
			void increment()
			{
				Dwarf_Off start_offset = offset_here();
				if (get_root().move_to_first_child(base_reference()))
				{
					// our offsets should only go up
					assert(offset_here() > start_offset);
					return;
				}
				do
				{
					if (get_root().move_to_next_sibling(base_reference()))
					{
						assert(offset_here() > start_offset);
						return;
					}
				} while (get_root().move_to_parent(base_reference()));

				// if we got here, there is nothing left in the tree...
				// ... so set us to the end sentinel
				base_reference() = base_reference().get_root().end/*<self>*/();
				assert(*this == iterator_base::END);
			}
			void decrement()
			{
				assert(false); // FIXME
			}
			bool equal(const self& arg) const { return this->base() == arg.base(); }
			
			DerefAs& dereference() const
			{ return dynamic_cast<DerefAs&>(this->iterator_base::dereference()); }
		};
		/* assert that our opt<> specialization for subclasses of iterator_base 
		 * has had its effect. */
		static_assert(std::is_base_of<core::iterator_base, opt<iterator_base> >::value, "opt<iterator_base> specialization error");
		static_assert(std::is_base_of<core::iterator_base, opt<core::iterator_df<> > >::value, "opt<iterator_base> specialization error");
	
		template <typename DerefAs /* = basic_die */>
		struct iterator_bf : public iterator_base,
							 public boost::iterator_facade<
							   iterator_bf<DerefAs>
							 , DerefAs
							 , boost::forward_traversal_tag
							 , DerefAs& // boost::use_default /* Reference */
							 , Dwarf_Signed /* difference */
							 >
		{
			typedef iterator_bf<DerefAs> self;
			friend class boost::iterator_core_access;

			// extra state needed!
			deque< iterator_base > m_queue;
			
			iterator_base& base_reference()
			{ return static_cast<iterator_base&>(*this); }
			const iterator_base& base() const
			{ return static_cast<const iterator_base&>(*this); }
			
			iterator_bf() : iterator_base() {}
			iterator_bf(const iterator_base& arg)
			 : iterator_base(arg) {}// this COPIES so avoid
			iterator_bf(iterator_base&& arg)
			 : iterator_base(arg) {}
			iterator_bf(const iterator_bf<DerefAs>& arg)
			 : iterator_base(arg), m_queue(arg.m_queue) {}// this COPIES so avoid
			iterator_bf(iterator_bf<DerefAs>&& arg)
			 : iterator_base(arg), m_queue(std::move(arg.m_queue)) {}
			
			iterator_bf& operator=(const iterator_base& arg) 
			{ this->base_reference() = arg; this->m_queue.clear(); return *this; }
			iterator_bf& operator=(iterator_base&& arg) 
			{ this->base_reference() = std::move(arg); this->m_queue.clear(); return *this; }
			iterator_bf& operator=(const iterator_bf<DerefAs>& arg) 
			{ this->base_reference() = arg; this->m_queue = arg.m_queue; return *this; }
			iterator_bf& operator=(iterator_bf<DerefAs>&& arg) 
			{ this->base_reference() = std::move(arg); this->m_queue =std::move(arg.m_queue); return *this; }

			void increment()
			{
				/* Breadth-first traversal:
				 * - move to the next sibling if there is one, 
				 *   enqueueing first child (if there is one);
				 * - else take from the queue, if non empty
				 * - else fail (terminated)
				 */
				auto first_child = get_root().first_child(this->base_reference()); 
				//   ^-- might be END
				
				// we ALWAYS enqueue the first child
				if (first_child != iterator_base::END) m_queue.push_back(first_child);
				
				if (get_root().move_to_next_sibling(this->base_reference()))
				{
					// success
					return;
				}
				else
				{
					// no more siblings; use the queue
					if (m_queue.size() > 0)
					{
						this->base_reference() = m_queue.front(); m_queue.pop_front();
					}
					else
					{
						this->base_reference() = iterator_base::END;
					}
				}
			}
			
			void increment_skipping_subtree()
			{
				/* This is the same as increment, except we are not interested in children 
				 * of the current node. */
				if (get_root().move_to_next_sibling(this->base_reference()))
				{
					// TEMP debugging hack: make sure we have a valid DIE
					assert(!is_real_die_position() || offset_here() > 0);
					
					// success -- don't enqueue children
					return;
				}
				else if (m_queue.size() > 0)
				{
					this->base_reference() = m_queue.front(); m_queue.pop_front();
					assert(!is_real_die_position() || offset_here() > 0);
				}
				else
				{
					this->base_reference() = iterator_base::END;
					assert(!is_real_die_position() || offset_here() > 0);
				}
			}
			
			void decrement()
			{
				assert(false); // FIXME
			}
			DerefAs& dereference() const
			{ return dynamic_cast<DerefAs&>(this->iterator_base::dereference()); }
		};
		
		template <typename DerefAs /* = basic_die*/>
		struct iterator_sibs : public iterator_base,
							   public boost::iterator_facade<
							   iterator_sibs<DerefAs> /* I (CRTP) */
							 , DerefAs /* V */
							 , boost::forward_traversal_tag
							 , DerefAs& //boost::use_default /* Reference */
							 , Dwarf_Signed /* difference */
							 >
		{
			typedef iterator_sibs<DerefAs> self;
			
			// FIXME: delete these. Just experimenting to figure out why we don't have iterator_traits<>::value_type
			//typedef DerefAs value_type;
			//typedef Dwarf_Signed difference_type;
			//typedef DerefAs *pointer;
			//typedef DerefAs& reference;
			//typedef std::bidirectional_iterator_tag iterator_category;

			friend class boost::iterator_core_access;
			
			iterator_base& base_reference()
			{ return static_cast<iterator_base&>(*this); }
			const iterator_base& base() const
			{ return static_cast<const iterator_base&>(*this); }
			
			iterator_sibs() : iterator_base() {}

			iterator_sibs(const iterator_base& arg)
			 : iterator_base(arg) {}

			iterator_sibs(iterator_base&& arg)
			 : iterator_base(std::move(arg)) {}
			
			iterator_sibs& operator=(const iterator_base& arg) 
			{ this->base_reference() = arg; return *this; }
			iterator_sibs& operator=(iterator_base&& arg) 
			{ this->base_reference() = std::move(arg); return *this; }
			
			void increment()
			{
				if (base_reference().get_root().move_to_next_sibling(base_reference())) return;
				// else something went wrong, so set us to END
				base_reference() = base_reference().get_root().end();
			}
			
			void decrement()
			{
				assert(false); // FIXME
			}
			
			bool equal(const self& arg) const { return this->base() == arg.base(); }
			DerefAs& dereference() const
			{ return dynamic_cast<DerefAs&>(this->iterator_base::dereference()); }
		};
		
		inline unsigned short iterator_base::depth() const
		{
			if (m_opt_depth) return *m_opt_depth;
			/* find_upwards is not enough; 
			 * the parent cache (parent_of) might not be complete. */
			auto found_self = get_root().find(offset_here(),
				opt<pair<Dwarf_Off, Dwarf_Half> >(),
				state == WITH_PAYLOAD ? cur_payload : nullptr);
			assert(found_self);
			assert(found_self.m_opt_depth);
			this->m_opt_depth = found_self.m_opt_depth;
			assert(found_self == *this);
			return *m_opt_depth;
		}
		
		inline iterator_base 
		basic_die::find_self() const
		{
			return iterator_base(*this);
		}

		inline
		sequence<iterator_sibs<basic_die> >
		basic_die::children() const
		{
			/* We have to find ourselves. :-( */
			auto found_self = find_self();
			return found_self.children_here();
		};

		struct is_visible_and_named : public std::function<bool(root_die::grandchildren_iterator)>
		{
			typedef std::function<bool(root_die::grandchildren_iterator)> fun;
			is_visible_and_named() : fun([](root_die::grandchildren_iterator i_g) -> bool {
				bool ret = i_g.global_name_here();
				root_die& r = i_g.get_root();
				if (ret)
				{
					/* install in cache */
					string name = *i_g.name_here();
					r.visible_named_grandchildren_cache.insert(
						make_pair(name, i_g.offset_here())
					);
				}
				/* Have we now swept the entire sequence of grandchildren? 
				 * If so, we can mark the cache as exhaustive. */
				if ((++i_g).done_complete_pass())
				{
					r.visible_named_grandchildren_is_complete = true;
				}
				return ret;
			}) {}
				//std::function<bool(root_die::grandchildren_iterator)> tmp;
				//tmp = std::move(lambda);
				//*static_cast<fun*>(this) = std::move(tmp);
			//}
		};
		/* 
		How to make an iterator over particular-named visible grandchildren?

		Easiest way: when we get a query for a particular name,
		ensure the cache is complete,
		then return a transform_iterator over the particular equal_range pair in the cache.

		This doesn't perform as well as incrementalising the cache-filling loop
		in resolve_all_visible.
		But it's probably fine for now.
		*/

		struct grandchild_die_at_offset : public std::function<basic_die&(Dwarf_Off)>
		{
			typedef std::function<basic_die&(Dwarf_Off)> fun;
			grandchild_die_at_offset(root_die& r) : fun([&r](Dwarf_Off off) -> basic_die& {
				return *r.pos(off, 2);
			}) {}
		};
	}
}

#endif
