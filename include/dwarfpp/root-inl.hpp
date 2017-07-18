#ifndef DWARFPP_ROOT_INL_HPP_
#define DWARFPP_ROOT_INL_HPP_

#include <iostream>
#include <utility>
#include <set>

#include "root.hpp"
#include "iter.hpp"
#include "dies.hpp"

namespace dwarf
{
	using std::string;
	using std::set;
	using dwarf::core::with_named_children_die;
	
	namespace core
	{
		/* root_die's name resolution functions */
		template <typename Iter>
		inline void 
		root_die::resolve_all(const iterator_base& start, Iter path_pos, Iter path_end,
			std::vector<iterator_base >& results, unsigned max /*= 0*/)
		{
			if (path_pos == path_end) 
			{ results.push_back(start); /* out of names, so unconditional */ return; }

			Iter cur_plus_one = path_pos; cur_plus_one++;
			if (cur_plus_one == path_end)
			{
				auto c = start.named_child(*path_pos);
				if (c) { results.push_back(c); if (max != 0 && results.size() >= max) return; }
			}
			else
			{
				auto found = start.named_child(*path_pos);
				if (found == iterator_base::END) return;
				resolve_all(found, ++path_pos, path_end, results, max);
			}			
		}
		
		template <typename Iter>
		inline iterator_base 
		root_die::resolve(const iterator_base& start, Iter path_pos, Iter path_end)
		{
			std::vector<iterator_base > results;
			resolve_all(start, path_pos, path_end, results, 1);
			if (results.size() > 0) return *results.begin();
			else return iterator_base::END;
		}

		template <typename Iter>
		inline void 
		root_die::resolve_all_visible_from_root(Iter path_pos, Iter path_end, 
			std::vector<iterator_base >& results, unsigned max /*= 0*/)
		{
			if (path_pos == path_end) return;
			
			/* We want to be able to iterate over grandchildren s.t. 
			 * 
			 * - we hit the cached-visible ones first
			 * - we hit them all eventually
			 * - we only hit each one once.
			 */
			set<Dwarf_Off> hit_in_cache;
			Iter cur_plus_one = path_pos; cur_plus_one++;
			
			auto recurse = [this, &results, path_end, max, cur_plus_one](const iterator_base& i) {
				/* It's visible; use resolve_all from hereon. */
				resolve_all(i, cur_plus_one, path_end, results, max);
			};
			
			auto matching_cached = visible_named_grandchildren_cache.equal_range(*path_pos);
			for (auto i_cached = matching_cached.first;
				i_cached != matching_cached.second; 
				++i_cached)
			{
				recurse(pos(i_cached->second, 2));
				if (max != 0 && results.size() >= max) return;
			}

			/* Now we have to be exhaustive. But don't bother if we know that 
			 * our cache is exhaustive. */
			if (!visible_named_grandchildren_is_complete)
			{
				auto vg_seq = visible_named_grandchildren();
				for (auto i_g = std::move(vg_seq.first); i_g != vg_seq.second; ++i_g)
				{
					/* skip any we saw before. 
					 * FIXME: something's wrong; we never add to this set */
					if (hit_in_cache.find(i_g.offset_here()) != hit_in_cache.end()) continue;

					/* It's visible; use resolve_all from hereon. */
					recurse(i_g);
					if (max != 0 && results.size() >= max) return;
				}
			}
		}

		inline iterator_base 
		root_die::resolve(const iterator_base& start, const std::string& name)
		{
			std::vector<string> path; path.push_back(name);
			return resolve(start, path.begin(), path.end());
		}

		template <typename Iter>
		inline iterator_base 
		root_die::scoped_resolve(const iterator_base& start, Iter path_pos, Iter path_end)
		{
			std::vector<iterator_base > results = {};
			scoped_resolve_all(start, path_pos, path_end, results, 1);
			if (results.size() > 0) return *results.begin();
			else return iterator_base::END;
		}

		template <typename Iter>
		inline void
		root_die::scoped_resolve_all(const iterator_base& start, Iter path_pos, Iter path_end, 
			std::vector<iterator_base >& results, unsigned max /*= 0*/) 
		{
			if (max != 0 && results.size() >= max) return;
			auto found_from_here = resolve(start, path_pos, path_end);
			if (found_from_here) 
			{ 
				results.push_back(found_from_here); 
				if (max != 0 && results.size() >= max) return;
			}
			
			// find our nearest encloser that has named children, and tail-recurse
			auto p_encl = start;
			do
			{
				this->move_to_parent(p_encl);
				if (p_encl.tag_here() == 0) 
				{ 
					// we ran out of parents; try visible things in other CUs, then give up
					resolve_all_visible_from_root(path_pos, path_end, results, max);
					return; 
				}
			} while (!p_encl.is_a<with_named_children_die>());

			// successfully moved to an encloser; tail-call to continue resolving
			scoped_resolve_all(p_encl, path_pos, path_end, results, max);
			// by definition, we're finished
		}
		template <typename Iter/* = iterator_df<compile_unit_die>*/ >
		inline Iter root_die::enclosing_cu(const iterator_base& it)
		{ return cu_pos<Iter>(it.get_enclosing_cu_offset()); }

		//// FIXME: do I need this function?
		//inline iterator_base root_die::scoped_resolve(const std::string& name)
		//{ return scoped_resolve(this->begin(), name); }

		inline
		dwarf::core::sequence<
				typename subseq_t<iterator_sibs<>, is_a_t<compile_unit_die> >::transformed_iterator
			>
		root_die::children() const
		{
			return begin().children().subseq_of<compile_unit_die>();
		}
		
		inline
		dwarf::core::sequence<root_die::grandchildren_iterator>
		root_die::grandchildren() const
		{
			auto p_seq = std::make_shared<srk31::concatenating_sequence< iterator_sibs<> > >();
			auto cu_seq = children();
			for (auto i_cu = std::move(cu_seq.first); i_cu != cu_seq.second; ++i_cu)
			{
				pair<iterator_sibs<>, iterator_sibs<> > children_seq = i_cu.base().children_here();
				p_seq->append(std::move(children_seq.first), std::move(children_seq.second));
			}
			return make_pair(p_seq->begin(), p_seq->end());
		}
		
		inline 
		dwarf::core::sequence<root_die::visible_named_grandchildren_iterator>
		root_die::visible_named_grandchildren() const
		{
			auto g_seq = this->grandchildren();
			return make_pair(
				root_die::visible_named_grandchildren_iterator(
					g_seq.first, g_seq.second
				),
				root_die::visible_named_grandchildren_iterator(
					g_seq.second, g_seq.second
				)
			);
		}
		
// 		inline
// 		pair<
// 				root_die::visible_grandchildren_with_name_iterator,
// 				root_die::visible_grandchildren_with_name_iterator
// 			>
// 		root_die::visible_grandchildren_with_name(const string& name) const
// 		{
// 			/* ensure the cache is full */
// 			if (!visible_named_grandchildren_is_complete)
// 			{
// 				auto vg_seq = this->visible_named_grandchildren();
// 				for (auto i = vg_seq.first; i != vg_seq.second; ++i);
// 			}
// 			assert(visible_named_grandchildren_is_complete);
// 			/* now just use the cache */
// 			auto vg_seq = this->visible_named_grandchildren();
// 			auto range = this->visible_named_grandchildren_cache.equal_range(name);
// 			return make_pair(
// 				root_die::visible_grandchildren_with_name_iterator(
// 					range.first, grandchild_die_at_offset(*this)
// 				),
// 				root_die::visible_grandchildren_with_name_iterator(
// 					range.second, grandchild_die_at_offset(&this)
// 				)
// 			);		
// 		}
		/* NOTE: pos() is incompatible with a strict parent cache.
		 * But it is necessary to support following references.
		 * We fill in the parent if depth <= 2, or if the user can tell us. */
		template <typename Iter /* = iterator_df<> */ >
		inline Iter root_die::pos(Dwarf_Off off, opt<unsigned short> opt_depth /* opt<unsigned short>() */,
			opt<Dwarf_Off> parent_off /* = opt<Dwarf_Off>() */,
			opt<pair<Dwarf_Off, Dwarf_Half> > referencer /* = opt<pair<Dwarf_Off, Dwarf_Half> >() */ )
		{
			if (opt_depth && *opt_depth == 0) { assert(off == 0UL); assert(!referencer); return Iter(begin()); }
			
			// always check the live set first
			auto found = live_dies.find(off);
			if (found != live_dies.end())
			{
				// it's there, so use find_upwards to get the iterator
				return iterator_base(*found->second);
			}
			
			Die h(*this, off);
			assert(h.handle.get());
			iterator_base base(std::move(h), opt_depth, *this);
			
			if (opt_depth && *opt_depth == 1) parent_of[off] = 0UL;
			else if (opt_depth && *opt_depth == 2) parent_of[off] = base.enclosing_cu_offset_here();
			else if (parent_off) parent_of[off] = *parent_off;
			
			// do we know anything about the first_child_of and next_sibling_of?
			// NO because we don't know where we are w.r.t. other siblings
			
			if (base && referencer) refers_to[*referencer] = base.offset_here();
			
			return Iter(std::move(base));
		}		
		
		template <typename Iter /* = iterator_df<> */ >
		inline Iter root_die::find_upwards(Dwarf_Off off, root_die::ptr_type maybe_ptr)
		{
			/* Use the parent cache to verify our existence and
			 * get our depth. */
			int height = 0; // we're at least at depth 0; will increment every time we go up successfully
			Dwarf_Off cur = off;
			//map<Dwarf_Off, Dwarf_Off>::iterator i_found_parent;
			//do
			//{
			//	i_found_parent = parent_of.find(cur);
			//	++height;
			//} while (i_found_parent != parent_of.end() && (cur = i_found_parent->second, true));

			/* 
			   What we want is 
			   - to search all the way to the top
			   - when we hit offset 0, `height' should be the depth of `off'
			 */
			for (auto i_found_parent = parent_of.find(cur);
				cur != 0 && i_found_parent != parent_of.end();
				i_found_parent = parent_of.find(cur))
			{
				cur = i_found_parent->second;
				++height;
			}
			// if we got all the way to the root, cur will be 0
			if (cur == 0)
			{
				// CARE: this recursion is safe because pos never calls back to us
				// with a non-null maybe_ptr
				if (!maybe_ptr) return pos(off, height, parent_of[off]);
				else return iterator_base(*maybe_ptr, opt<unsigned short>(height));
			}
			else
			{
				debug() << "Did not find DIE at offset 0x" << std::hex << off << std::dec << std::endl;
				return iterator_base::END;
			}
		}
		
		template <typename Iter /* = iterator_df<> */ >
		inline Iter root_die::find(Dwarf_Off off, 
			opt<pair<Dwarf_Off, Dwarf_Half> > referencer /* = opt<pair<Dwarf_Off, Dwarf_Half> >() */,
			root_die::ptr_type maybe_ptr /* = root_die::ptr_type(nullptr) */)
		{
			Iter found_up = find_upwards(off, maybe_ptr);
			if (found_up != iterator_base::END)
			{
				if (referencer) refers_to[*referencer] = found_up.offset_here();
				return found_up;
			} 
			else
			{
				auto found = find_downwards(off);
				if (found && referencer) refers_to[*referencer] = found.offset_here();
				return found;
			}
		}
		
		/* We use the properties of DIE trees to avoid a naive depth-first search. 
		 * FIXME: make it work with encap::-style less strict ordering. 
		 * NOTE: a possible idea here is to support a kind of "fractional offsets"
		 * where we borrow *high-order* bits from the offset space in a dynamic
		 * fashion. We need some per-root bookkeeping about what offsets have
		 * been issued, and a way to get a numerical comparison (for search
		 * functions, like this one). 
		 * Probably the best way to accommodate this is as a new class
		 * used in place of Dwarf_Off. */
		template <typename Iter /* = iterator_df<> */ >
		inline Iter root_die::find_downwards(Dwarf_Off off)
		{
			/* Interesting problem: our iterators don't make searching a subtree 
			 * easy. I think there is a neat way of expressing this by combining
			 * dfs and bfs traversal. FIXME: work out the recipe. */
			
			/* I think we want bf traversal with a smart subtree-skipping test. */
			iterator_bf<typename Iter::DerefType> pos = begin();
			// debug(2) << "Searching for offset " << std::hex << off << std::dec << endl;
			// debug(2) << "Beginning search at 0x" << std::hex << pos.offset_here() << std::dec << endl;
			while (pos != iterator_base::END && pos.offset_here() != off)
			{
				/* What's next in the breadth-first order? */
				assert(((void)pos.offset_here(), true));
				// debug(2) << "Began loop body; pos is 0x" 
				// 	<< std::hex << pos.offset_here() << std::dec;
				iterator_bf<typename Iter::DerefType> next_pos = pos; 
				assert(((void)pos.offset_here(), true));
				next_pos.increment();
				assert(((void)pos.offset_here(), true));
				// debug(2) << ", next_pos is ";
				// if (next_pos != iterator_base::END) {
				// 	debug(2) << std::hex << next_pos.offset_here() << std::dec;
				//} else debug(2) << "(END)";
				// debug(2) << endl;
				 
				/* Does the move pos->next_pos skip over (enqueue) a subtree? 
				 * If so, the depth will stay the same. 
				 * If no, it's because we took a previously enqueued (deeper, but earlier) 
				 * node out of the queue (i.e. descended instead of skipped-over). */
				if (next_pos != iterator_base::END && next_pos.depth() == pos.depth())
				{
					// debug(2) << "next_pos is at same depth..." << endl;
					// if I understand correctly....
					assert(next_pos.offset_here() > pos.offset_here());
					
					/* Might that subtree contain off? */
					if (off < next_pos.offset_here() && off > pos.offset_here())
					{
						// debug(2) << "We think that target is in subtree ..." << endl;
						/* Yes. We want that subtree. 
						 * We don't want to move_to_first_child, 
						 * because that will put the bfs traversal in a weird state
						 * (s.t. next_pos might take us *upwards* not just across/down). 
						 * But we don't want to increment through everything, 
						 * because that will be slow. 
						 * Instead, 
						 * - create a new bf iterator at pos (with empty queue); 
						 * - increment it once normally, so that the subtree is enqueued; 
						 * - continue the loop. */
						iterator_bf<typename Iter::DerefType> new_pos 
						 = static_cast<iterator_base>(pos); 
						new_pos.increment();
						if (new_pos != iterator_base::END) {
							//  previously I had the following slow code: 
							// //do { pos.increment(); } while (pos.offset_here() > off); 
							pos = new_pos;
							// debug(2) << "Fast-forwarded pos to " 
							// 	<< std::hex << pos.offset_here() << std::dec << std::endl;
							continue;
						} 
						else 
						{
							// subtree is empty -- we have failed
							pos = iterator_base::END;
							continue;
						}
						
					}
					else // off >= next_pos.offset_here() || off <= pos.offset_here()
					{
						// debug(2) << "Subtree between pos and next_pos cannot possibly contain target..." << endl;
						/* We can't possibly want that subtree. */
						pos.increment_skipping_subtree();
						continue;
					}
				}
				else 
				{ 
					// next is END, or is at a different (lower) depth than pos
					pos.increment(); 
					continue; 
				}
				assert(false); // i.e. the above cases must cover everything
			}
			// debug(2) << "Search returning "; 
			// if (pos == iterator_base::END) debug(2) << "(END)";
			// else debug(2) << std::hex << pos.offset_here() << std::dec; 
			// debug(2) << endl;
			return pos;
		}
		
		// FIXME: what does this constructor do? Can we get rid of it?
		// It seems to be used only for the compile_unit_die constructor.
		//inline basic_die::basic_die(root_die& r/*, const Iter& i*/)
		//: d(Die::handle_type(nullptr)), p_root(&r) {}

		template <typename Iter /* = iterator_df<> */ >
		inline Iter root_die::begin()
		{
			/* The first DIE is always the root.
			 * We denote an iterator pointing at the root by
			 * a Debug but no Die. */
			iterator_base base(*this);
			assert(base.is_root_position());
			Iter it(base);
			assert(it.is_root_position());
			return it;
		}

		template <typename Iter /* = iterator_df<> */ >
		inline Iter root_die::end()
		{
			return Iter(iterator_base::END);
		}

		template <typename Iter /* = iterator_df<> */ >
		inline pair<Iter, Iter> root_die::sequence()
		{
			return std::make_pair(begin(), end());
		}
	}
}

#endif
