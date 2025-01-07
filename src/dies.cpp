/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * dies.cpp: methods specific to each DIE tag
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#include <boost/optional/optional_io.hpp> // FIXME: remove when finished debugging
#include "dwarfpp/abstract.hpp"
#include "dwarfpp/abstract-inl.hpp"
#include "dwarfpp/root.hpp"
#include "dwarfpp/root-inl.hpp"
#include "dwarfpp/iter.hpp"
#include "dwarfpp/iter-inl.hpp"
#include "dwarfpp/dies.hpp"
#include "dwarfpp/dies-inl.hpp"

#include <memory>
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <srk31/algorithm.hpp>


// "return site marker" a.k.a. horrible HACK for debugging -- see below
//extern "C" {
//extern int __dwarfpp_assert_1;
//}

namespace dwarf
{
	namespace core
	{
		using std::make_unique;
		
		opt<std::string> compile_unit_die::source_file_fq_pathname(unsigned o) const
		{
			string filepath;
			try
			{
				filepath = source_file_name(o);
			} catch (dwarf::lib::Error e)
			{
				debug() << "Warning: source_file_name threw libdwarf error: "
					<< dwarf_errmsg(current_dwarf_error) << std::endl;
				return opt<string>();
			}
			opt<string> maybe_dir = this->get_comp_dir();
			if (filepath.length() > 0 && filepath.at(0) == '/') return opt<string>(filepath);
			else if (!maybe_dir) return opt<string>();
			else
			{
				// we want to do 
				// return dir + "/" + path;
				// BUT "path" can contain "../".
				string ourdir = *maybe_dir;
				string ourpath = filepath;
				while (boost::starts_with(ourpath, "../"))
				{
					char *buf = strdup(ourdir.c_str());
					ourdir = dirname(buf); /* modifies buf! */
					free(buf);
					ourpath = ourpath.substr(3);
				}

				return opt<string>(ourdir + "/" + ourpath);
			}
		}

		iterator_base
		with_named_children_die::named_child(const std::string& name) const
		{
			/* The default implementation just asks the root. Since we've somehow 
			 * been called via the payload, we have the added inefficiency of 
			 * searching for ourselves first. This shouldn't happen, though 
			 * I'm not sure if we explicitly avoid it. Warn. */
			debug(2) << "Warning: inefficient usage of with_named_children_die::named_child" << endl;

			/* NOTE: the idea about payloads knowing about their children is 
			 * already dodgy because it breaks our "no knowledge of structure" 
			 * property. We can likely work around this by requiring that named_child 
			 * implementations call back to the root if they fail, i.e. that the root
			 * is allowed to know about structure which the child doesn't. 
			 * Or we could call into the root to "validate" our view, somehow, 
			 * to support the case where a given root "hides" some DIEs.
			 * This might be a method
			 * 
			 *  iterator_base 
			 *  root_die::check_named_child(const iterator_base& found, const std::string& name)
			 * 
			 * which calls around into find_named_child if the check fails. 
			 * 
			 * i.e. the root_die has a final say, but the payload itself "hints"
			 * at the likely answer. So the payload can avoid find_named_child's
			 * linear search in the common case, but fall back to it in weird
			 * scenarios (deletions). 
			 */
			root_die& r = get_root();
			Dwarf_Off off = get_offset();
			auto start_iter = r.find(off);
			return r.find_named_child(start_iter, name); 
		}
		/* from program_element_die */
		opt<string> program_element_die::find_associated_name() const
		{
			root_die& r = get_root();
			r.ensure_refers_to_cache_is_complete();
			/* We have an associated name iff
			 * - we have no name, and
			 * - there is a unique thing that refers to us, and
			 * - it has a name (not an associated name).
			 * WAIT. There is something else that we require: it's that
			 * by referring to us by that name, the name is not ambiguous.
			 * i.e. it itself only refers to one anonymous thing. Otherwise
			 * if we had
			 *   d --> <anon>
			 *    \`-> <anon>
			 *     `-> <anon>
			 *                ... they would all get d's name.
			 * Also we need it to have a different tag from us.
			 * */
			Dwarf_Off our_off = this->get_offset();
			auto referring_range = r.referred_from.equal_range(our_off);
			auto count = srk31::count(referring_range.first, referring_range.second);
			if (1 == count)
			{
				Dwarf_Off referrer = referring_range.first->second.first;
				iterator_df<program_element_die> i_referrer = r.pos(referrer);
				// it's not a match if its tag is the same as ours
				if (i_referrer.tag_here() == this->get_tag()) goto out;
				auto maybe_name = i_referrer.name_here();
				if (maybe_name)
				{
					// would this name point only to us? means
					// - tag must differ (so tag can be a disambiguator between
					//       associator and associated)
					// - we must be the only anonymous program element it refers to
					auto referenced_range_begin = r.refers_to.lower_bound(make_pair(referrer, (Dwarf_Half) 0));
					auto referenced_range_end = r.refers_to.upper_bound(make_pair(referrer, (Dwarf_Half) -1));
					bool found_us = false;
					bool found_others = false;
					for (auto i_ref_tuple = referenced_range_begin; i_ref_tuple != referenced_range_end; ++i_ref_tuple)
					{
						iterator_base i_other_ref = r.pos(i_ref_tuple->second);
						// skip anything that's not a program element
						if (!i_other_ref.is_a<program_element_die>()) { continue; }
						// skip anything that has a name
						if (i_other_ref.name_here()) { continue; }
						// skip anything whose tag is the same as the referrer
						if (i_other_ref.tag_here() == i_referrer.tag_here()) { continue; }
						// we should find us
						if (i_ref_tuple->second == our_off) found_us = true;
						else found_others = true;
					}
					assert(found_us);
					if (!found_others) return *maybe_name;
					else
					{
						debug_expensive(6, << "Not associating " << *this
							<< " with name " << *maybe_name
							<< " because there were other referrers" << endl);
					}
				}
			}
			else
			{
				debug(3) << "No associated name for " << *this
					<< "; referrer count was " << count << " (list follows)" << endl;
				for (auto i_ref = referring_range.first; i_ref != referring_range.second; ++i_ref)
				{
					debug(3) << std::hex << i_ref->second.first << std::dec << endl;
				}
			}
		out:
			return opt<string>();
		}
		
		/* type abstract equality and abstract naming. */
		bool types_abstractly_equal(iterator_df<type_die> t1, iterator_df<type_die> t2)
		{
			// both immediately void?
			if (!t1 && !t2) return true;
			// no, so we can delegate to one or other
			if (t1) return t1->abstractly_equals(t2);
			else return t2->abstractly_equals(t1);
		}
		std::ostream& print_type_abstract_name(std::ostream& s, iterator_df<type_die> t)
		{
			if (!t) { s << "void"; return s; }
			/* We should never be called from type_die::print_abstract_name */
			//ptrdiff_t return_site_distance_from_bad_caller
			// = (char*) __builtin_return_address(0) - (char*) &__dwarfpp_assert_1;
			//assert(return_site_distance_from_bad_caller > 50
			//	|| return_site_distance_from_bad_caller < -50);
			return t->print_abstract_name(s);
		}
		string abstract_name_for_type(iterator_df<type_die> t)
		{
			std::ostringstream s;
			print_type_abstract_name(s, t);
			return s.str();
		}
		bool base_type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<base_type_die>()
				&& (t.as_a<base_type_die>()->get_encoding() == this->get_encoding())
				&& (t.as_a<base_type_die>()->get_byte_size() == this->get_byte_size())
				&& (t.as_a<base_type_die>()->get_bit_size() == this->get_bit_size())
				&& (t.as_a<base_type_die>()->get_bit_offset() == this->get_bit_offset());
		}
		std::ostream& unspecified_type_die::print_abstract_name(std::ostream& s) const
		{
			s << "void"; return s; // FIXME: hmm, distinguish?
		}
		std::ostream& base_type_die::print_abstract_name(std::ostream& s) const
		{
			s << get_canonical_name();
			return s;
		}
		bool with_data_members_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<with_data_members_die>()
				&& (t.as_a<with_data_members_die>()->arbitrary_name() == this->arbitrary_name());
					/* FIXME: handle namespace */
		}
		std::ostream& with_data_members_die::print_abstract_name(std::ostream& s) const
		{
			vector<string> name_path(1, arbitrary_name()); /* FIXME: handle namespaces */
			for (auto i = name_path.begin(); i != name_path.end(); ++i)
			{
				if (name_path.size() > 1) s << "__NL" << i->length() << "_";
				s << *i;
			}
			return s;
		}
		bool enumeration_type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<enumeration_type_die>()
				&& (t.as_a<enumeration_type_die>()->arbitrary_name() == this->arbitrary_name());
					/* FIXME: handle namespace */
		}
		std::ostream& enumeration_type_die::print_abstract_name(std::ostream& s) const
		{
			vector<string> name_path(1, arbitrary_name()); /* FIXME: handle namespaces */
			for (auto i = name_path.begin(); i != name_path.end(); ++i)
			{
				if (name_path.size() > 1) s << "__NL" << i->length() << "_";
				s << *i;
			}
			return s;
		}
		bool type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			// should never recurse
			return types_abstractly_equal(get_concrete_type(), t ? t->get_concrete_type() : t);
		}
		bool array_type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<array_type_die>() &&
				t.as_a<array_type_die>()->element_count()
					 == find_self().as_a<array_type_die>()->element_count();
		}
		bool subrange_type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<subrange_type_die>()
				&& t.as_a<subrange_type_die>()->get_lower_bound() == get_lower_bound()
				&& t.as_a<subrange_type_die>()->get_upper_bound() == get_upper_bound();
		}
		bool string_type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<string_type_die>()
				&& t.as_a<string_type_die>()->fixed_length_in_bytes() == fixed_length_in_bytes()
				/* FIXME: element size/count */;
		}
		bool set_type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<set_type_die>()
				&& types_abstractly_equal(t.as_a<set_type_die>()->get_type(), get_type());

		}
		bool file_type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<file_type_die>()
				&& types_abstractly_equal(t.as_a<file_type_die>()->get_type(), get_type());
		}
		bool address_holding_type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<address_holding_type_die>()
				&& t.get_tag() == get_tag()
				&& types_abstractly_equal(t.as_a<address_holding_type_die>()->get_type(), get_type());
		}
		bool ptr_to_member_type_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			return t.is_a<ptr_to_member_type_die>()
				&& types_abstractly_equal(t.as_a<ptr_to_member_type_die>()->get_containing_type(), get_containing_type())
				&& types_abstractly_equal(t.as_a<ptr_to_member_type_die>()->get_type(), get_type());
		}
		bool type_describing_subprogram_die::abstractly_equals(iterator_df<type_die> t) const
		{
			t = t ? t->get_concrete_type() : t;
			bool mostly_matches = t.is_a<type_describing_subprogram_die>()
				&& types_abstractly_equal(t.as_a<type_describing_subprogram_die>()->get_return_type(), get_return_type())
				&& t.as_a<type_describing_subprogram_die>()->is_variadic() == is_variadic();
			if (!mostly_matches) return false;
			auto my_fps = find_self().children().subseq_of<formal_parameter_die>();
			auto ts_fps = t.children_here().subseq_of<formal_parameter_die>();
			auto i_my_fp = my_fps.first;
			auto i_ts_fp = ts_fps.first;
			for (; i_my_fp != my_fps.second; ++i_my_fp, ++i_ts_fp)
			{
				if (!i_ts_fp) return false; // we have more argments
				if (!types_abstractly_equal(i_my_fp->get_type(), i_ts_fp->get_type())) return false;
			}
			if (i_ts_fp) return false; // t has more arguments
			return true;
		}
		/* This one does for typedef and qualified types. */
		std::ostream& type_die::print_abstract_name(std::ostream& s) const
		{
			// should never recurse to the same type...
			static __thread Dwarf_Off generic_print_abstract_name_processing;
			if (generic_print_abstract_name_processing) assert(
				generic_print_abstract_name_processing != get_offset());
			generic_print_abstract_name_processing = get_offset();
			std::ostream& ref = print_type_abstract_name(s, get_concrete_type());
			generic_print_abstract_name_processing = 0;
			// ... or we could maybe use this "return site marker" to assert that
			// __asm__ volatile ("__dwarfpp_assert_1:\n");
			return ref;
		}
		//__asm__ volatile (".globl __dwarfpp_assert_1");
		std::ostream& array_type_die::print_abstract_name(std::ostream& s) const
		{
			opt<Dwarf_Unsigned> element_count = find_self().as_a<array_type_die>()->element_count();
			s << "__ARR";
			if (element_count) s << *element_count;
			s << "_";
			return print_type_abstract_name(s, get_type());
		}
		std::ostream& subrange_type_die::print_abstract_name(std::ostream& s) const
		{
			s << "__SUBR" << (get_lower_bound() ? *get_lower_bound() : 0) << "_"
				<< ((get_upper_bound() && get_lower_bound()) ? 
					(*get_upper_bound() - *get_lower_bound()) : 0) << "_";
			return print_type_abstract_name(s, get_type());
		}
		std::ostream& string_type_die::print_abstract_name(std::ostream& s) const
		{
			s << "__STR" << (fixed_length_in_bytes() ? *fixed_length_in_bytes() : 0) << "_";
			/* Strings have two sizes: element size and element count. */
			// in liballocs we used to do this:
			// const Dwarf_Unsigned element_size = 1; /* FIXME: always 1? */
			// string_prefix << "__STR" << (element_count ? *element_count : 0) << "_"
			// << element_size;
			//if (opt_element_size && *opt_element_size != 1) s << *opt_element_size;
			s << "_";
			s << 1; // FIXME
			return s; //print_type_abstract_name(s, get_type());
			// FIXME: element type?
		}
		std::ostream& set_type_die::print_abstract_name(std::ostream& s) const
		{
			s << "__SET_"; return print_type_abstract_name(s, get_type());
		}
		std::ostream& file_type_die::print_abstract_name(std::ostream& s) const
		{
			s << "__FILE_"; return print_type_abstract_name(s, get_type());
		}
		std::ostream& address_holding_type_die::print_abstract_name(std::ostream& s) const
		{
			switch (get_tag())
			{
				case DW_TAG_pointer_type:
					s << "__PTR_"; break;
				case DW_TAG_reference_type:
					s << "__REF_"; break;
				case DW_TAG_rvalue_reference_type:
					s << "__RR_"; break;
				default: assert(false); abort();
			}
			return print_type_abstract_name(s, get_type());
		}
		std::ostream& ptr_to_member_type_die::print_abstract_name(std::ostream& s) const
		{
			s << "__MEMPTR_";
			return print_type_abstract_name(s, get_type());
		}
		std::ostream& type_describing_subprogram_die::print_abstract_name(std::ostream& s) const
		{
			s << "__FUN_FROM_";
			unsigned argnum = 0;
			auto fps = find_self().children().subseq_of<formal_parameter_die>();
			for (auto i_fp = fps.first; i_fp != fps.second; ++i_fp, ++argnum)
			{
				/* args should not be void */
				/* We're making a canonical typename, so use canonical argnames. */
				s << "__ARG" << argnum << "_";
				print_type_abstract_name(s, i_fp->find_type());
			}
			if (is_variadic()) s << "__VA_";
			s << "__FUN_TO_";
			return print_type_abstract_name(s, get_return_type());
		}

		pair<iterator_df<type_die>, iterator_df<program_element_die> >
		type_iterator_df_base::first_outgoing_edge_target() const
		{
			auto NO_DEEPER = make_pair(iterator_base::END, iterator_base::END);
			if (!base() && reason()) { /* void case */ return NO_DEEPER; }
			else if (base().is_a<type_chain_die>()) // unary case -- includes typedefs, arrays, pointer/reference, ...
			{
				// recursively walk the chain's target
				return make_pair(base().as_a<type_chain_die>()->find_type(), base());
			}
			else if (base().is_a<with_data_members_die>()) 
			{
				// descend to first member child or first inheritance child
				auto data_member_children = base().as_a<with_data_members_die>().children().subseq_of<data_member_die>();
				if (data_member_children.first != data_member_children.second)
				{
					return make_pair(
						data_member_children.first->find_or_create_type_handling_bitfields(),
						/* the reason is the member */
						data_member_children.first
					);
				} else return NO_DEEPER;
			}
			else if (base().is_a<subrange_type_die>())
			{
				// visit the base base()
				auto explicit_t = base().as_a<subrange_type_die>()->find_type();
				// HACK: assume this is the same as for enums
				return make_pair(
					explicit_t ? explicit_t : base().enclosing_cu()->implicit_subrange_base_type(),
					base()
				);
			}
			else if (base().is_a<enumeration_type_die>())
			{
				// visit the base type -- HACK: assume subrange base is same as enum's
				auto explicit_t = base().as_a<enumeration_type_die>()->find_type();
				return make_pair(
					explicit_t ? explicit_t : base().enclosing_cu()->implicit_enum_base_type(),
					base()
				);
			}
			else if (base().is_a<type_describing_subprogram_die>())
			{
				/* TRICKY: our "children" consist of firstly a return type,
				 * and later a bunch of formal parameter types. If we've
				 * just visited the return type and want to move to the next
				 * sibling, how do we do that? Neither the type itself nor
				 * the "reason" of the subprogram is sufficient, because
				 * the same type might appear in multiple places in the signature.
				 * AH, it's okay. We can give the return type's "reason" as
				 * the subprogram, and the argument types' "reason" as the FP
				 * DIEs. */
				auto sub_t = base().as_a<type_describing_subprogram_die>();
				return make_pair(sub_t->find_type() /* even if void */,
					sub_t);
			}
			else
			{
				// what are our nullary cases?
				assert(base().is_a<base_type_die>() || base().is_a<unspecified_type_die>());
				return NO_DEEPER;
			}
		}
		
		pair<iterator_df<type_die>, iterator_df<program_element_die> >
		type_iterator_df_base::predecessor_node_next_outgoing_edge_target() const
		{
			auto NO_MORE = make_pair(iterator_base::END, iterator_base::END);
			auto to_return = NO_MORE;
			// m_stack.back().first might be null
			/* NOTE: we are testing "reason", and it need not be a type.
			 * It just needs to be a thing with a type,
			 * so includes members, inheritances, FPs, ... */
			if (reason().is_a<data_member_die>())
			{
				/* Get the next data member -- just using the iterator_base should be enough. */
				basic_die::children_iterator<data_member_die> cur(
					reason(),
					reason().parent().children().second
				);
				assert(cur);
				assert(cur.is_a<data_member_die>());
				++cur;
				if (cur) to_return = make_pair(cur->get_type(), cur);
			}
			else if (reason().is_a<type_describing_subprogram_die>())
			{
				/* For a subprogram type, the return type's "reason" is
				 * the subprogram, and the argument types' "reason" is the FP
				 * DIE. */
				auto fp_seq = reason().as_a<type_describing_subprogram_die>()->children().subseq_of<formal_parameter_die>();
				auto next = fp_seq.first;
				if (next != fp_seq.second) to_return = make_pair(next->get_type(), next);
			}
			else if (reason().is_a<formal_parameter_die>())
			{
				basic_die::children_iterator<formal_parameter_die> cur(
					reason(),
					reason().parent().children().second
				);
				++cur;
				if (cur) to_return = make_pair(cur->get_type(), cur);
			}
			else if (reason().is_a<type_chain_die>())
			{} // no more
			else
			{
				// what are our nullary cases?
				assert(!reason()
					|| reason().is_a<base_type_die>() || reason().is_a<unspecified_type_die>()
					|| reason().is_a<subrange_type_die>() || reason().is_a<enumeration_type_die>()
					|| reason().is_a<string_type_die>());
				// no more
			}
			return to_return;
		}
		
		// FIXME: add an actually sensible basic increment operation that visits nodes
		// when they are white and continues otherwise. Could do pre- or post-order.
		void type_iterator_df_base::increment_not_back(bool skip_dependencies)
		{
			if (pos_colour() == WHITE) m_stack.push_back(make_pair(base(), reason()));
			
			// FIXME: this should be true for a type_iterator_df_walk -- not for other iterators (i.e. we assume they override us)
			assert(base() == m_stack.back().first);
			assert(m_reason == m_stack.back().second);
			
			/* We are doing the usual incremental depth-first traversal, which means
			 * 1. try to move deeper
			 * 2. else do
			 *        try to move to next sibling
			 *    while (move to parent);
			 *
			 * But we are doing it over type relations, not DIE parent/child relations.
			 * Note that types are not trees! They are graphs and may be cyclic.
			 * So when following some edge that appears to be a child or sibling,
			 * it may be a back edge (reaching a grey node) or a cross edge
			 * (reaching a black node).
			 */
			pair<iterator_base, iterator_base> sideways_target, deeper_target;
			debug(5) << "Trying to move deeper..." << std::endl;
			deeper_target = first_outgoing_edge_target();
			if (!skip_dependencies && (deeper_target.first || deeper_target.second))
			{
				/* "descend"... but with a catch, because we need to avoid cycles.
				 * Don't descend to something we're already visiting (grey; back edge).
				 * BUT because we're silly (emulating walk_type), we *do* re-push
				 * cross-edge targets that we have already finished visiting (black). */
				debug(5) << "Found deeper (" << deeper_target.first.summary() << "); are we walking it already?...";
				colour col = colour_of(deeper_target.first);
				if (col == WHITE || col == BLACK)
				{
					debug(5) << "no, so descending." << std::endl;
					/* continue and push it on the stack again
					 * -- NOTE that this will re-explore a blackened subtree! */
					this->m_stack.push_back(deeper_target);
					this->base_reference() = std::move(deeper_target.first);
					this->m_reason = std::move(deeper_target.second);
					return;
				}
				else // grey -- it's a back-edge
				{
					debug(5) << "yes, so pretending we can't move deeper..." << std::endl;
					// silently ignore it -- it's as if we can't go deeper
					deeper_target = make_pair(END, END);
				}
			}
			
			while (true)
			{
				debug(5) << "Trying to move sideways..." << std::endl;
				sideways_target = predecessor_node_next_outgoing_edge_target();
				if (sideways_target.first || sideways_target.second)
				{
					debug(5) << "Found sideways, so moving there..." << std::endl;
					// blacken the old top
					black_offsets.insert(this->m_stack.back().first);
					/* replace the top element */
					this->m_stack.pop_back();
					this->m_stack.push_back(sideways_target);
					this->base_reference() = std::move(sideways_target.first);
					this->m_reason = std::move(sideways_target.second);
					return;
				}
			//force_backtrack:
				debug(5) << "Nowhere sideways to move... backtracking" << std::endl;
				assert(!m_stack.empty());
				// else backtrack
				black_offsets.insert(this->m_stack.back().first);
				assert(!m_stack.empty());
				pair<iterator_base, iterator_base> pos_and_reason = m_stack.back();
				m_stack.pop_back();
				if (m_stack.empty()) break;
				this->base_reference() = std::move(m_stack.back().first);
				this->m_reason = std::move(m_stack.back().second);
			};
			
			/* We've run out. */
			assert(m_stack.empty());
			*this = iterator_base::END;
			assert(!reason());
		}
		void type_iterator_df_base::decrement()
		{
			assert(false); // FIXME
		}
		void type_iterator_df_edges::increment()
		{
			this->increment_to_unseen_edge();
		}
		void type_iterator_df_base::increment_to_unseen_edge()
		{
			auto print_stack = [=]() {
				debug_expensive(5, << "[");
				for (auto i = m_stack.begin(); i != m_stack.end(); ++i)
				{
					if (i != m_stack.begin()) debug_expensive(5, << ", ");
					debug_expensive(5, << std::hex << 
						(i->first.is_end_position() ? (Dwarf_Off)-1 : i->first.offset_here()) 
							<< std::dec);
				}
				debug_expensive(5, << "]" << std::endl);
			};
			
			/* To visit edges, basic idea is this.
			 * We test our own colour immediately,
			 * which tells us about the kind of edge we are on.
			 * If we're black, it's a cross-edge; don't go deeper, just
			 *    try proceeding to the predecessor's next outgoing edge, and backtrack --
			 *    meaning try that predecessor's predecessor, etc..
			 *    CARE: whenever we exhaust a predecessor's outgoing edges,
			 *    that predecessor (which is grey) must be marked black.
			 * If we're grey, it's a back-edge; ditto.
			 * If we're white, it's a tree edge. We push ourselves onto the stack,
			 *   (making ourselves grey) and try to follow an outgoing edge.
			 *   If we have no outgoing edge, we make ourselves black
			 *      and continue.
			 */
			
			switch (pos_colour())
			{
				case WHITE:
					// we're grey now
					m_stack.push_back(make_pair(base(), reason()));
					assert(pos_colour() == GREY);
					// but unlike the "already grey" case, we need to now try exploring deeper...
					{
						auto found_deeper = first_outgoing_edge_target();
						if (found_deeper.second)
						{
							print_stack();
							this->base_reference() = std::move(found_deeper.first);
							this->m_reason = std::move(found_deeper.second);
							return;
						}
						// else this means we have *no* outgoing edges. we're black already.
						m_stack.pop_back();
						black_offsets.insert(base());
						// fall through to sideways
					}
				sideways:
				case GREY:
				case BLACK:
				while (!m_stack.empty())
				{
					// can we move to the next outgoing edge of our predecessor?
					auto found_sideways = predecessor_node_next_outgoing_edge_target();
					if (found_sideways.second)
					{
						// our predecessor is necessarily grey
						// and is necessarily our top-of-stack
						assert(colour_of(source_vertex_for(base(), reason())) == GREY);
						assert(source_vertex_for(base(), reason()) == m_stack.back().first);
						// and we're keeping the same predecessor, so leave the stack untouched
						this->base_reference() = std::move(found_sideways.first);
						this->m_reason = std::move(found_sideways.second);
						return;
					}
					// else our predecessor has no outgoing edges; fall through to backtrack
				backtrack:
					assert(m_stack.size() > 0);
					// i.e. "we've exhausted our predecessor's outgoing edges"
					auto predecessor = source_vertex_for(base(), reason());
					debug_expensive(5, << base().summary() << ":" 
						<< ": exhausted outgoing edges of: " << predecessor.summary() << std::endl);
					black_offsets.insert(predecessor);
					this->base_reference() = std::move(predecessor);
					this->m_reason = m_stack.back().second;
					m_stack.pop_back();
					print_stack();
				}
			}
			
			this->base_reference() = END;
			this->m_reason = END;
		}

		/* protected helper */
		string
		type_die::arbitrary_name() const
		{
			string name_to_use;
			if (get_name()) name_to_use = *get_name();
			else
			{
				std::ostringstream s;
				s << "0x" << std::hex << get_offset() << std::dec;
				string offsetstr = s.str();
				/* We really want to allow deduplicating anonymous structure types
				 * that originate in the same header file but are included in multiple
				 * compilation units. Since each gets a different offset, using that
				 * for the fake name string is a bad idea. Instead, use the
				 * associated name if we have it, or else the defining source file path
				 * if we have it. FIXME: I think we shouldn't use the filename/line# because it
				 * can introduce false distinctions e.g. across minor header changes. */
				if (find_associated_name()) name_to_use = *find_associated_name();
#if 0
				else if (get_decl_file() && get_decl_line())
				{
					std::ostringstream s;
					opt<string> maybe_fqp = find_self().enclosing_cu()->source_file_fq_pathname(*get_decl_file());
					s << (maybe_fqp ?
							boost::filesystem::path(*maybe_fqp).filename().native() :
							boost::filesystem::path(find_self().enclosing_cu()->source_file_name(*get_decl_file())).filename().native())
						<< "_" << *get_decl_line();
					name_to_use = s.str();
				}
#endif
				else
				{
					debug(3) << "Warning: using offset str as arbitrary name of " << this->summary()
						<< " (could not find an associated name); this may cause surprising inequalities between types" << endl;
					name_to_use = offsetstr;
				}
			}
			return name_to_use;
		}
		
		size_t type_hash_fn(iterator_df<type_die> t) 
		{
			opt<uint32_t> summary = t ? t->summary_code() : opt<uint32_t>(0);
			return summary ? *summary : 0;
		}
		bool type_eq_fn(iterator_df<type_die> t1, iterator_df<type_die> t2)
		{
			return (!t1 && !t2) || (t1 && t2 && *t1 == *t2);
		}
		void walk_type(iterator_df<type_die> t, iterator_df<program_element_die> reason, 
			const std::function<bool(iterator_df<type_die>, iterator_df<program_element_die>)>& pre_f, 
			const std::function<void(iterator_df<type_die>, iterator_df<program_element_die>)>& post_f,
			const dieloc_set& currently_walking /* = empty */)
		{
			auto key = !t ? opt<Dwarf_Off>() : opt<Dwarf_Off>(t.offset_here());
			if (currently_walking.find(key) != currently_walking.end()) return; // "grey node"
			
			bool continue_recursing;
			if (pre_f) continue_recursing = pre_f(t, reason); // i.e. we do walk "void"
			else continue_recursing = true;
			
			dieloc_set next_currently_walking = currently_walking;
			next_currently_walking.insert(key);
			
			if (continue_recursing)
			{
				if (!t) { /* void case; just post-visit */ }
				else if (t.is_a<type_chain_die>()) // unary case -- includes typedefs, arrays, pointer/reference, ...
				{
					// recursively walk the chain's target
					walk_type(t.as_a<type_chain_die>()->find_type(), t, pre_f, post_f, next_currently_walking);
				}
				else if (t.is_a<with_data_members_die>()) 
				{
					// recursively walk all members and inheritances
					auto member_children = t.as_a<with_data_members_die>().children().subseq_of<data_member_die>();
					for (auto i_child = member_children.first;
						i_child != member_children.second; ++i_child)
					{
						walk_type(i_child->find_or_create_type_handling_bitfields(),
							i_child, pre_f, post_f, next_currently_walking);
					}
				}
				else if (t.is_a<subrange_type_die>())
				{
					// visit the base type
					auto explicit_t = t.as_a<subrange_type_die>()->find_type();
					// HACK: assume this is the same as for enums
					walk_type(explicit_t ? explicit_t : t.enclosing_cu()->implicit_subrange_base_type(), t, pre_f, post_f, next_currently_walking);
				}
				else if (t.is_a<enumeration_type_die>())
				{
					// visit the base type -- HACK: assume subrange base is same as enum's
					auto explicit_t = t.as_a<enumeration_type_die>()->find_type();
					walk_type(explicit_t ? explicit_t : t.enclosing_cu()->implicit_enum_base_type(), t, pre_f, post_f, next_currently_walking);
				}
				else if (t.is_a<type_describing_subprogram_die>())
				{
					auto sub_t = t.as_a<type_describing_subprogram_die>();
					walk_type(sub_t->find_type(), sub_t, pre_f, post_f, next_currently_walking);
					auto fps = sub_t.children().subseq_of<formal_parameter_die>();
					for (auto i_fp = fps.first; i_fp != fps.second; ++i_fp)
					{
						walk_type(i_fp->find_type(), i_fp, pre_f, post_f, next_currently_walking);
					}
				}
				else
				{
					// what are our nullary cases?
					assert(t.is_a<base_type_die>() || t.is_a<unspecified_type_die>());
				}
			} // end if continue_recursing

			if (post_f) post_f(t, reason);
		}
		opt<Dwarf_Unsigned> type_die::calculate_byte_size() const
		{
			return get_byte_size(); 
		}
		iterator_df<type_die> type_die::get_concrete_type() const
		{
			return find_self();
		}
		iterator_df<type_die> type_die::get_unqualified_type() const
		{
			return find_self();
		}
		template <typename BaseType>
		opt<BaseType> type_die::containment_summary_code(
			std::function<opt<BaseType>(iterator_df<type_die>)> recursive_call
		) const
		{
			/* Summary of what we documented in type_die::summary_code():
			 * Incompletes have no summary.
			 * Pointers to incompletes have no summary.
			 * Pointers to completes do have a summary.
			 * Inside a struct (or array?), the contribution of a pointer
			 * to the summary
			 * is in terms of the pointer's abstract name, not its summary.
			 * This prevents pointer-to-complete and pointer-to-incomplete
			 * having different effects.
			 * In dumptypes, we will have to take care to emit a reference
			 * to the codeless or codeful uniqtype as appropriate.
			 * If we use a codeless pointer type, we should emit a weak definition
			 * that is not in a section group.
			 *
			 * NOTE: we're not virtual, because we're a template,
			 * so we have to handle every case here.
			 *
			 * NOTE: this method knows nothing about caching.
			 * If we recurse, we have to worry about caching.
			 * Only type_die::summary code knows about that.
			 * So, we are parameterised by what actually is our recursive call.
			 */
			
			if (!recursive_call) recursive_call = [](iterator_df<type_die> arg) -> opt<BaseType> {
				return arg->containment_summary_code<BaseType>();
			};
			
			summary_code_word<BaseType> output_word;
			iterator_df<type_die> t = find_self();
			// don't let concrete_t binding last
			{
				auto concrete_t = t ? t->get_concrete_type() : t;
				if (!concrete_t) return opt<BaseType>(0); // FIXME: top-level function that can deal with void
				/* If we ourselves are not concrete, recurse. */
				if (&*concrete_t != this) return recursive_call(concrete_t);
			}

			/* For declarations, try to find their definition and recurse. Else return opt<>(). */
			if (t.is_a<with_data_members_die>() &&
			    t.as_a<with_data_members_die>()->get_declaration() &&
			   *t.as_a<with_data_members_die>()->get_declaration())
			{
				iterator_df<> found = t->find_definition();
				if (found) return recursive_call(found);
				else return opt<BaseType>();
			}
			if (t.is_a<base_type_die>())
			{
				auto base_t = t.as_a<core::base_type_die>();
				unsigned encoding = base_t->get_encoding();
				assert(base_t->get_byte_size());
				unsigned byte_size = *base_t->get_byte_size();
				unsigned bit_size = base_t->get_bit_size() ? *base_t->get_bit_size() : byte_size * 8;
				unsigned bit_offset = base_t->get_bit_offset() ? *base_t->get_bit_offset() : 0;
				output_word << DW_TAG_base_type << encoding << byte_size << bit_size << bit_offset;
				return output_word.val;
			}
			else if (t.is_a<enumeration_type_die>())
			{
				/* HACK: because CIL sometimes mangles names to keep them unique,
				 * e.g. "ADDRESS___0"
				 * FIXME: in uniqtype-defs.h, and places which use its contents,
				  * use "_"-prefix/suffixing to remove this problem. Or just don't
				  * include uniqtype.h in libcrunch_cil_inlines? Why do we need it? */
				auto HACK_NAME = [](const string& name) -> string {
					boost::smatch m;
					if (boost::regex_match(name, m, boost::regex("([a-zA-Z_][a-zA-Z0-9_]*)___[0-9]+")))
					{
						string to_return = m[1];
						std::cerr << "Hacked name " << name << " back to " << to_return << std::endl;
						return to_return;
					} else return name;
				};
				// shift in the enumeration name
				output_word << t->arbitrary_name();

				// shift in the names and values of each enumerator
				auto enum_t = t.as_a<enumeration_type_die>();
				auto enumerators = t.children().subseq_of<enumerator_die>();
				int last_enum_value = -1;
				for (auto i_enum = enumerators.first; i_enum != enumerators.second; ++i_enum)
				{
					output_word << HACK_NAME(*i_enum->get_name());
					if (i_enum->get_const_value())
					{
						last_enum_value = *i_enum->get_const_value();
						output_word << last_enum_value;
					} else output_word << last_enum_value++;
				}

				// then shift in the base type's summary code
				auto enum_base_t = enum_t->get_type();
				if (!enum_base_t)
				{
					// debug() << "Warning: saw enum with no type" << endl;
					enum_base_t = enum_t.enclosing_cu()->implicit_enum_base_type();
					if (!enum_base_t)
					{
						debug() << "Warning: saw enum with no type" << endl;
						return output_word.val;
					}
				}
				output_word << recursive_call(enum_base_t);
				return output_word.val;
			}
			else if (t.is_a<subrange_type_die>())
			{
				auto subrange_t = t.as_a<subrange_type_die>();

				// DON'T shift in the name; names on subrange types are irrelevant

				// shift in the base type's summary code
				if (!subrange_t->get_type())
				{
					debug() << "Warning: saw subrange with no type" << endl;
				}
				else output_word << recursive_call(subrange_t->get_type());

				/* Then shift in the upper bound and lower bound, if present
				 * NOTE: this means unnamed boundless subrange types have the 
				 * same code as their underlying type. This is probably what we want. */
				if (subrange_t->get_upper_bound()) output_word << *subrange_t->get_upper_bound();
				if (subrange_t->get_lower_bound()) output_word << *subrange_t->get_lower_bound();
				return output_word.val;
			}
			else if (t.is_a<type_describing_subprogram_die>())
			{
				auto subp_t = t.as_a<type_describing_subprogram_die>();

				/* This is the key trick for cutting off the effects of incompleteness,
				 * and also avoiding cycles. */
				auto incorporate_type = [&](iterator_df<type_die> t) {
					if (t && t->get_concrete_type().is_a<address_holding_type_die>())
					{
						output_word << abstract_name_for_type(t->get_concrete_type());
					} else output_word << recursive_call(t);
				};
				
				// shift in the argument and return types
				auto return_type = subp_t->get_return_type();
				incorporate_type(return_type);

				// shift in something to distinguish void(void) from void
				output_word << "()";

				auto fps = t.children().subseq_of<formal_parameter_die>();
				for (auto i_fp = fps.first; i_fp != fps.second; ++i_fp)
				{
					/* This is the key trick for cutting off the effects of incompleteness,
					 * and also avoiding cycles. */
					incorporate_type(i_fp->find_type());
				}

				if (subp_t->is_variadic()) output_word << "...";
				return output_word.val;
			}
			else if (t.is_a<address_holding_type_die>())
			{
				auto ptr_t = t.as_a<core::address_holding_type_die>();
				auto ultimate_pointee_pair = ptr_t->find_ultimate_reached_type();
				auto ultimate_pointee_t = ultimate_pointee_pair.second;
				if (ultimate_pointee_t.is_a<with_data_members_die>()
					&& ultimate_pointee_t.as_a<with_data_members_die>()->get_declaration()
					&& *ultimate_pointee_t.as_a<with_data_members_die>()->get_declaration())
				{
					// we're opaque. we have no summary
					return opt<BaseType>();
				}
				
				unsigned ptr_size = *ptr_t->calculate_byte_size();
				unsigned addr_class = ptr_t->get_address_class() ? *ptr_t->get_address_class() : 0;
				if (addr_class != 0)
				{
					switch(addr_class) 
					{
						default:
							assert(false); // nobody seems to use this feature so far
						/* NOTE: There is also something called DWARF Pointer-Encoding (PEs).
						   This is a DWARF representation issue, used in frame info, and is not 
						   something we care about. */
					}
				}
				output_word << t.tag_here() << ptr_size << addr_class;
				auto target_t = ptr_t->find_type();
				output_word << recursive_call(target_t);
				return output_word.val;
			}
			if (t.is_a<with_data_members_die>())
			{
				// add in the name
				output_word << t->arbitrary_name();

				// for each member 
				auto members = t.children().subseq_of<core::data_member_die>();
				for (auto i_member = members.first; i_member != members.second; ++i_member)
				{
					// skip members that are mere declarations 
					if (i_member->get_declaration() && *i_member->get_declaration()) continue;

					// calculate its offset
					opt<Dwarf_Unsigned> opt_offset = i_member->byte_offset_in_enclosing_type();
					if (!opt_offset)
					{
						debug() << "Warning: saw member " << *i_member << " with no apparent offset." << endl;
						continue;
					}

					output_word << (opt_offset ? *opt_offset : 0);
					// also its bit offset!
					
					auto member_type = i_member->get_type();
					assert(member_type);
					assert(member_type.is_a<type_die>());
					
					/* This is the key trick for cutting off the effects of incompleteness,
					 * and also avoiding cycles. */
					if (member_type && member_type->get_concrete_type()
						.is_a<address_holding_type_die>())
					{
						output_word << abstract_name_for_type(member_type->get_concrete_type());
					} else output_word << recursive_call(member_type);
				}
				return output_word.val;
			}
			if (t.is_a<array_type_die>())
			{
				// if we're a member of something, we should be bounded in all dimensions
				auto opt_el_type = t.as_a<array_type_die>()->ultimate_element_type();
				auto opt_el_count = t.as_a<array_type_die>()->ultimate_element_count();
				if (!opt_el_type) output_word << opt<BaseType>();
				else if (opt_el_type.is_a<address_holding_type_die>())
				{
					output_word << abstract_name_for_type(opt_el_type);
				}
				else output_word << recursive_call(opt_el_type);
				
				output_word << (opt_el_count ? *opt_el_count : 0);
				// FIXME: also the factoring into dimensions needs to be taken into account
				
				return output_word.val;
			}
			else if (t.is_a<string_type_die>())
			{
				// Fortran strings can be fixed-length or variable-length
				auto opt_dynamic_length = t.as_a<string_type_die>()->get_string_length();
				unsigned byte_len;
				if (opt_dynamic_length)
				{
					// treat it as length 0
					byte_len = 0;
				}
				else
				{
					auto opt_byte_size = t.as_a<string_type_die>()->fixed_length_in_bytes();
					assert(opt_byte_size);
					byte_len = *opt_byte_size;
				}
				output_word << DW_TAG_string_type << byte_len;
				return output_word.val;
			}
			else if (t.is_a<unspecified_type_die>())
			{
				debug() << "Warning: saw unspecified type " << t << std::endl;
				return opt<BaseType>();
			}

			abort(); // we should not reach here
		}
		opt<uint16_t> type_die::traversal_summary_code() const
		{
			return opt<uint16_t>(); // FIXME
		}
		opt<uint32_t> summary_code_for_type(iterator_df<type_die> t)
		{
			if (!t) return opt<uint32_t>(0);
			else return t->summary_code();
		}
		template <typename BaseType>
		opt<BaseType> containment_summary_code_for_type(iterator_df<type_die> t);
		template <typename BaseType>
		opt<BaseType> containment_summary_code_for_type(iterator_df<type_die> t)
		{
			if (!t) return opt<BaseType>(0);
			else return t->containment_summary_code<BaseType>();
		}
		opt<uint16_t> traversal_summary_code_for_type(iterator_df<type_die> t);
		opt<uint16_t> traversal_summary_code_for_type(iterator_df<type_die> t)
		{
			if (!t) return opt<uint16_t>(0);
			else return t->traversal_summary_code();
		}
		opt<uint32_t> type_die::summary_code_using_old_method() const
		{
			// what follows is the "old way"; preserved here for now
			using lib::Dwarf_Unsigned;
			using lib::Dwarf_Half;
			using namespace dwarf::core;

			opt<uint32_t> code_to_return;
			summary_code_word<uint32_t> output_word;
			/* if we have it cached, return that */
			//auto found_cached = get_root().type_summary_code_cache.find(get_offset());
			//if (found_cached != get_root().type_summary_code_cache.end())
			//{
			//	return found_cached->second;
			//}
		
			/* FIXME: factor this into the various subclass cases. */
			// we have to find ourselves. :-(
			auto t = get_root().find(get_offset()).as_a<type_die>();
			__typeof(t) concrete_t;
			
			auto name_for_type_die = [](core::iterator_df<core::type_die> t) -> opt<string> {
				if (t.is_a<dwarf::core::subprogram_die>())
				{
					/* When interpreted as types, subprograms don't have names. */
					return opt<string>();
				}
				else return *t.name_here();
			};
			
			auto type_summary_code = [](core::iterator_df<core::type_die> t) -> opt<uint32_t> {
				if (!t) return opt<uint32_t>(0);
				else return t->summary_code();
			};
			
			/* Here we compute a 4-byte hash-esque summary of a data type's 
			 * definition. The intentions here are that 
			 *
			 * binary-incompatible definitions of two types will always
			   compare different, even if the incompatibility occurs 

			   - in compiler decisions (e.g. bitfield positions, pointer
			     encoding, padding, etc..)

			   - in a child (nested) object.

			 * structurally distinct definitions will always compare different, 
			   even if at the leaf level, they are physically compatible.

			 * binary compatible, structurally compatible definitions will compare 
			   alike iff they are nominally identical at the top-level. It doesn't
			   matter if field names differ. HMM: so what about nested structures' 
			   type names? Answer: not sure yet, but easiest is to require that they
			   match, so our implementation can just use recursion.

			 * WHAT about prefixes? e.g. I define struct FILE with some padding, 
			   and you define it with some implementation-private fields? We handle
			   this at the libcrunch level; here we just want to record that there
			   are two different definitions out there.

			 *
			 * Consequences: 
			 * 
			 * - encode all base type properties
			 * - encode pointer encoding
			 * - encode byte- and bit-offsets of every field
			 */
			if (!t) { code_to_return = opt<uint32_t>(0); goto out; }

			concrete_t = t->get_concrete_type();
			if (!concrete_t)
			{
				// we got a typedef of void
				code_to_return = opt<uint32_t>(0); goto out;
			}

			/* For declarations, if we can't find their definition, we return opt<>(). */
			if (concrete_t.is_a<with_data_members_die>() &&
			    concrete_t.as_a<with_data_members_die>()->get_declaration() &&
			   *concrete_t.as_a<with_data_members_die>()->get_declaration())
			{
				iterator_df<> found = concrete_t->find_definition();
				concrete_t = found.as_a<type_die>();
				if (!concrete_t) 
				{
					code_to_return = opt<uint32_t>();
					goto out;
				}
			}

			assert(output_word.val);
			{
				Dwarf_Half tag = concrete_t.tag_here();
				
				opt<string> maybe_fq_str = concrete_t->get_decl_file() ? concrete_t.enclosing_cu()->source_file_fq_pathname(
						*concrete_t->get_decl_file()) : opt<string>();
				
				std::ostringstream tmp;
				
				string fq_pathname_str = maybe_fq_str 
					? *maybe_fq_str 
					: concrete_t->get_decl_file() ? 
						concrete_t.enclosing_cu()->source_file_name(*concrete_t->get_decl_file())
						: /* okay, give up and use the offset after all */
							(tmp << std::hex << concrete_t.offset_here(), tmp.str());
				
				if (concrete_t.is_a<base_type_die>())
				{
					auto base_t = concrete_t.as_a<core::base_type_die>();
					unsigned encoding = base_t->get_encoding();
					assert(base_t->get_byte_size());
					unsigned byte_size = *base_t->get_byte_size();
					unsigned bit_size = base_t->get_bit_size() ? *base_t->get_bit_size() : byte_size * 8;
					unsigned bit_offset = base_t->get_bit_offset() ? *base_t->get_bit_offset() : 0;
					output_word << DW_TAG_base_type << encoding << byte_size << bit_size << bit_offset;
				} 
				else if (concrete_t.is_a<enumeration_type_die>())
				{
					// shift in the enumeration name
					if (concrete_t.name_here())
					{
						output_word << *name_for_type_die(concrete_t);
					} else output_word << std::hash<string>()(fq_pathname_str);

					// shift in the names and values of each enumerator
					auto enum_t = concrete_t.as_a<enumeration_type_die>();
					auto enumerators = enum_t.children().subseq_of<enumerator_die>();
					int last_enum_value = -1;
					for (auto i_enum = enumerators.first; i_enum != enumerators.second; ++i_enum)
					{
						output_word << *i_enum->get_name();
						if (i_enum->get_const_value())
						{
							last_enum_value = *i_enum->get_const_value();
							output_word << last_enum_value;
						} else output_word << last_enum_value++;
					}

					// then shift in the base type's summary code
					if (!enum_t->get_type())
					{
						// debug() << "Warning: saw enum with no type" << endl;
						auto implicit_t = enum_t.enclosing_cu()->implicit_enum_base_type();
						if (!implicit_t)
						{
							debug() << "Warning: saw enum with no type" << endl;
						} else output_word << type_summary_code(implicit_t);
					}
					else
					{
						output_word << type_summary_code(enum_t->get_type());
					}
				} 
				else if (concrete_t.is_a<subrange_type_die>())
				{
					auto subrange_t = concrete_t.as_a<subrange_type_die>();

					// shift in the name, if any
					if (concrete_t.name_here())
					{
						output_word << *name_for_type_die(concrete_t);
					} else output_word << std::hash<string>()(fq_pathname_str);

					// then shift in the base type's summary code
					if (!subrange_t->get_type())
					{
						debug() << "Warning: saw subrange with no type" << endl;
					}
					else
					{
						output_word << type_summary_code(subrange_t->get_type());
					}

					/* Then shift in the upper bound and lower bound, if present
					 * NOTE: this means unnamed boundless subrange types have the 
					 * same code as their underlying type. This is probably what we want. */
					if (subrange_t->get_upper_bound())
					{
						output_word << *subrange_t->get_upper_bound();
					}
					if (subrange_t->get_lower_bound())
					{
						output_word << *subrange_t->get_lower_bound();
					}
				} 
				else if (concrete_t.is_a<type_describing_subprogram_die>())
				{
					auto subp_t = concrete_t.as_a<type_describing_subprogram_die>();

					// shift in the argument and return types
					auto return_type = subp_t->get_return_type();
					output_word << type_summary_code(return_type);

					// shift in something to distinguish void(void) from void
					output_word << "()";

					auto fps = concrete_t.children().subseq_of<formal_parameter_die>();
					for (auto i_fp = fps.first; i_fp != fps.second; ++i_fp)
					{
						output_word << type_summary_code(i_fp->find_type());
					}

					if (subp_t->is_variadic())
					{
						output_word << "...";
					}
				}
				else if (concrete_t.is_a<address_holding_type_die>())
				{
					/* NOTE: actually, we *do* want to pay attention to what the pointer points to, 
					 * i.e. its contract. BUT there's a problem: recursive data types! For now, we
					 * use a giant HACK: if we're a pointer to a with-data-members, use only 
					 * the name. FIXME: can we form cycles using only subprogram types?
					 * I don't think so but am not sure. */
					auto ptr_t = concrete_t.as_a<core::address_holding_type_die>();
					unsigned ptr_size = *ptr_t->calculate_byte_size();
					unsigned addr_class = ptr_t->get_address_class() ? *ptr_t->get_address_class() : 0;
					if (addr_class != 0)
					{
						switch(addr_class) 
						{
							default:
								assert(false); // nobody seems to use this feature so far
							/* NOTE: There is also something called DWARF Pointer-Encoding (PEs).
							   This is a DWARF representation issue, used in frame info, and is not 
							   something we care about. */
						}
					}
					auto target_t = ptr_t->get_type();
					if (target_t.is_real_die_position()) target_t = target_t->get_concrete_type();
					opt<uint32_t> target_code;
					if (target_t.is_real_die_position() && target_t.is_a<with_data_members_die>())
					{
						summary_code_word<uint32_t> tmp_output_word;
						// add in the name only
						if (target_t.name_here())
						{
							tmp_output_word << *name_for_type_die(target_t);
						} else tmp_output_word << std::hash<string>()(fq_pathname_str);

						target_code = *tmp_output_word.val;
					} else target_code = type_summary_code(target_t);
					output_word << tag << ptr_size << addr_class << target_code;
				}
				else if (concrete_t.is_a<with_data_members_die>())
				{
					// add in the name if we have it
					if (concrete_t.name_here())
					{
						output_word << *name_for_type_die(concrete_t);
					} else output_word << std::hash<string>()(fq_pathname_str);

					// for each member 
					auto members = concrete_t.children().subseq_of<core::data_member_die>();
					for (auto i_member = members.first; i_member != members.second; ++i_member)
					{
						// skip members that are mere declarations 
						if (i_member->get_declaration() && *i_member->get_declaration()) continue;

						// calculate its offset
						opt<Dwarf_Unsigned> opt_offset = i_member->byte_offset_in_enclosing_type();
						if (!opt_offset)
						{
							debug() << "Warning: saw member " << *i_member << " with no apparent offset." << endl;
							continue;
						}
						auto member_type = i_member->get_type();
						assert(member_type);
						assert(member_type.is_a<type_die>());

						output_word << (opt_offset ? *opt_offset : 0);
						// FIXME: also its bit offset!

						output_word << type_summary_code(member_type);
					}
				}
				else if (concrete_t.is_a<array_type_die>())
				{
					// if we're a member of something, we should be bounded in all dimensions
					auto opt_el_type = concrete_t.as_a<array_type_die>()->ultimate_element_type();
					auto opt_el_count = concrete_t.as_a<array_type_die>()->ultimate_element_count();
					output_word << (opt_el_type ? type_summary_code(opt_el_type) : opt<uint32_t>())
						<< (opt_el_count ? *opt_el_count : 0);
						// FIXME: also the factoring into dimensions needs to be taken into account
				}
				else if (concrete_t.is_a<string_type_die>())
				{
					// Fortran strings can be fixed-length or variable-length
					auto opt_dynamic_length = concrete_t.as_a<string_type_die>()->get_string_length();
					unsigned byte_len;
					if (opt_dynamic_length)
					{
						// treat it as length 0
						byte_len = 0;
					}
					else
					{
						auto opt_byte_size = concrete_t.as_a<string_type_die>()->fixed_length_in_bytes();
						assert(opt_byte_size);
						byte_len = *opt_byte_size;
					}
					output_word << DW_TAG_string_type << byte_len;
				}
				else if (concrete_t.is_a<unspecified_type_die>())
				{
					debug() << "Warning: saw unspecified type " << concrete_t;
					output_word.val = opt<uint32_t>();
				}
				else 
				{
					debug() << "Warning: didn't understand type " << concrete_t;
				}
			}

			// pointer-to-incomplete, etc., will still give us incomplete answer
			assert (!concrete_t || !(output_word.val) || *output_word.val != 0);

			code_to_return = output_word.val; 
		out:
			//get_root().type_summary_code_cache.insert(
			//	make_pair(get_offset(), code_to_return)
			//);
			return code_to_return;
		}
		
		template <typename BaseType>
		opt<BaseType> type_die::combined_summary_code_using_iterators() const
		{
			if (this->cached_summary_code) return this->cached_summary_code;
			//auto found_in_root_cache = get_root().type_summary_code_cache.find(get_offset());
			//if (found_in_root_cache != get_root().type_summary_code_cache.end())
			//{
			//	this->cached_summary_code = found_in_root_cache->second;
			//	return found_in_root_cache->second;
			//}
			
			/* The "old way" to compute summary codes was to walk the type
			 * and fold in the summary code of all the constituent types,
			 * plus a few words here and there to record how they relate
			 * to each other. Think about that again for a second. Because
			 * walk_type walks over the whole type structure, we're not
			 * compositional. If I'm a structure containing a structure containing
			 * some ints, we use the summaries of the ints *and* the contained
			 * structure. That's redundant.
			 *
			 * So, we want a compositional way. In this example the obvious
			 * thing is just to use the contained structure's summary, i.e.
			 * to skip over indirectly depended-on types. I started on a solution
			 * along these lines, until I noticed that....
			 * 
			 * ... in cyclic cases this doesn't work. The neat thing that walk_type
			 * did for us was cycle avoidance. It visited cycle members, only once,
			 * in an order that depends on where we start. If we have a struct that
			 * is part of the same cycle, doing walk_type on it would visit the same
			 * nodes but in a different order, hence giving a different output.
			 * That's good (we want different nodes to give different summary codes)
			 * but also non-compositional (we can't re-use the summary code of
			 * visited nodes).
			 * 
			 * The way out of this to define the summary code of cyclic types in
			 * terms of the strongly-connected component of the type graph.
			 * This we can cache the SCC, and indeed amortise costs by sharing it
			 * among all nodes participating in it. Then we just need a way
			 * of digesting the SCC. The summary code is the SCC digest `plus`
			 * something to denote the start node. For this we can use the type's
			 * abstract name. */
			
			auto maybe_scc = get_scc();
			auto self = find_self();
			auto t = self.as_a<type_die>();
			summary_code_word<BaseType> output_word;
			if (!maybe_scc)
			{
				/* We're acyclic. Just iterate over our *immediate* child types,
				 * shifting in their summary codes, then add something to
				 * record how we combine those, and we're done.
				 * 
				 * We can do this with walk_type, using its pre-walk return flag
				 * to skip traversal of others. We return "true" only if we are
				 * still at ourselves. FIXME: what about immediately recursive types?
				 * Are there any?
				 *
				 * FIXME: here we are ignoring field offsets and names, among other 
				 * things which ought to make a difference to the type.
				 */
				// just a decl, no def? try to replace ourselves with the definition, and if not, null out. */
				if (t->get_declaration() && *t->get_declaration())
				{
					iterator_df<> found = t->find_definition();
					t = found.as_a<type_die>();
					if (!t)
					{
						debug(2) << "Detected that we have a declaration with no definition; returning no code" << std::endl;
						// NOTE that we will still get a post-visit, just no recursion
						// so we explicitly clear the output word (HACK)
						output_word.invalidate();
						return output_word.val;
					}
				}
				Dwarf_Off offset_here = get_offset();
				walk_type(self, self,
					/* pre_f */ [&output_word, offset_here](
						iterator_df<type_die> t, iterator_df<program_element_die> reason) -> bool {
						// ignore the starting DIE
						if (t && t.offset_here() == offset_here) return true;
						// void means 0
						if (!t) output_word << 0;
						else output_word << t->combined_summary_code_using_iterators<BaseType>();
						return false;
					},
					/* post_f */ [](iterator_df<type_die> t, iterator_df<program_element_die> reason) -> void
					{});

				// now we've accumulated a summary of our immediate children
				// but we've yet to distinguish ourselves
				
				// if we're not concrete, our children have captured everything
				auto concrete_t = t ? t->get_concrete_type() : t;
				if (t != concrete_t) return output_word.val;
				// void? our code is 0; keep going
				if (!t) { output_word << 0; return output_word.val; }
				// just a decl, no def? try to replace ourselves with the definition, and if not, null out. */
				if (t->get_declaration() && *t->get_declaration())
				{
					iterator_df<> found = t->find_definition();
					t = found.as_a<type_die>();
					if (!t)
					{
						debug_expensive(2, << "Detected that we have a declaration with no definition; returning no code" << std::endl);
						// NOTE that we will still get a post-visit, just no recursion
						// so we explicitly clear the output word (HACK)
						output_word.invalidate();
						return output_word.val;
					}
				}
// 				/* If we have a "reason" that is a member_die or inheritance_die, 
// 				 * */
// 				if (reason && (reason.is_a<data_member_die>()))
// 				{
// 					debug(2) << "This type is walked for reason of a member: ";
// 					reason.print(debug(2), 0);
// 					debug(2) << std::endl;
// 					// skip members that are mere declarations 
// 					if (reason->get_declaration() && *reason->get_declaration()) return false;
// 
// 					// calculate its offset
// 					opt<Dwarf_Unsigned> opt_offset = reason.as_a<data_member_die>()
// 						->byte_offset_in_enclosing_type();
// 					if (!opt_offset)
// 					{
// 						debug(2) << "Warning: saw member " << *reason << " with no apparent offset." << endl;
// 						return false;
// 					}
// 					auto member_type = reason.as_a<with_dynamic_location_die>()->get_type();
// 					assert(member_type);
// 					assert(member_type.is_a<type_die>());
// 
// 					output_word << (opt_offset ? *opt_offset : 0);
// 					// FIXME: also its bit offset!
// 
// 					// type-visiting logic will do the member type
// 				}

				if (t.is_a<base_type_die>())
				{
					auto base_t = t.as_a<core::base_type_die>();
					unsigned encoding = base_t->get_encoding();
					assert(base_t->get_byte_size());
					unsigned byte_size = *base_t->get_byte_size();
					unsigned bit_size = base_t->get_bit_size() ? *base_t->get_bit_size() : byte_size * 8;
					unsigned bit_offset = base_t->get_bit_offset() ? *base_t->get_bit_offset() : 0;
					output_word << DW_TAG_base_type << encoding << byte_size << bit_size << bit_offset;
				} 
				else if (t.is_a<enumeration_type_die>())
				{
					// shift in the enumeration name
					output_word << t->arbitrary_name();

					// shift in the names and values of each enumerator
					auto enum_t = t.as_a<enumeration_type_die>();
					auto enumerators = enum_t.children().subseq_of<enumerator_die>();
					int last_enum_value = -1;
					for (auto i_enum = enumerators.first; i_enum != enumerators.second; ++i_enum)
					{
						output_word << *i_enum->get_name();
						if (i_enum->get_const_value())
						{
							last_enum_value = *i_enum->get_const_value();
							output_word << last_enum_value;
						} else output_word << last_enum_value++;
					}
				}
				else if (t.is_a<subrange_type_die>())
				{
					auto subrange_t = t.as_a<subrange_type_die>();
					output_word << t->arbitrary_name();
				} 
				else if (t.is_a<type_describing_subprogram_die>())
				{
					auto subp_t = t.as_a<type_describing_subprogram_die>();

					// first, shift in something to distinguish void(void) from void
					output_word << "()";
					if (t.as_a<type_describing_subprogram_die>()->is_variadic())
					{ output_word << "..."; }
				}
				else if (t.is_a<address_holding_type_die>())
				{
					/* Danger: recursive data types! But walk_type deals with this. */
					auto ptr_t = t.as_a<core::address_holding_type_die>();
					unsigned ptr_size = *ptr_t->calculate_byte_size();
					unsigned addr_class = ptr_t->get_address_class() ? *ptr_t->get_address_class() : 0;
					if (addr_class != 0)
					{
						switch(addr_class) 
						{
							default:
								assert(false); // nobody seems to use this feature so far
							/* NOTE: There is also something called DWARF Pointer-Encoding (PEs).
							   This is a DWARF representation issue, used in frame info, and is not 
							   something we care about. */
						}
					}
					output_word << t.tag_here() << ptr_size << addr_class;
				}
				else if (t.is_a<with_data_members_die>())
				{
					output_word << t->arbitrary_name();
				}
				else if (t.is_a<array_type_die>())
				{
					// nothing here -- it's in post_f
				}
				else if (t.is_a<string_type_die>())
				{
					// Fortran strings can be fixed-length or variable-length
					auto opt_dynamic_length = t.as_a<string_type_die>()->get_string_length();
					unsigned byte_len;
					if (opt_dynamic_length) byte_len = 0; /* treat as length 0 */ 
					else
					{
						auto opt_byte_size = t.as_a<string_type_die>()->fixed_length_in_bytes();
						assert(opt_byte_size);
						byte_len = *opt_byte_size;
					}
					output_word << DW_TAG_string_type << byte_len;
				}
			}
			else
			{
				/* We're cyclic. The cycle itself has a summary word
				 * determined by its edge set. We then shift in something
				 * to distinguish ourselves by our position in the cycle;
				 * for this we use our abstract name. */
				output_word << maybe_scc->edges_summary.val;
				output_word << abstract_name_for_type(self);
			}

			this->cached_summary_code = output_word.val;
			//get_root().type_summary_code_cache.insert(
			//	make_pair(get_offset(), output_word.val)
			//);
			return output_word.val;
		}

		opt<uint32_t> type_die::summary_code_using_walk_type() const
		{
			debug(2) << "Computing summary code for " << *this << std::endl;
			/* Here we compute a 4-byte hash-esque summary of a data type's 
			 * definition. The intentions here are that 
			 *
			 * binary-incompatible definitions of two types will always
			   compare different, even if the incompatibility occurs 

			   - in compiler decisions (e.g. bitfield positions, pointer
				 encoding, padding, etc..)

			   - in a child (nested) object.

			 * structurally distinct definitions will always compare different, 
			   even if at the leaf level, they are physically compatible.

			 * binary compatible, structurally compatible definitions will compare 
			   alike iff they are nominally identical at the top-level. It doesn't
			   matter if field names differ. HMM: so what about nested structures' 
			   type names? Answer: not sure yet, but easiest is to require that they
			   match, so our implementation can just use recursion.

			 * WHAT about prefixes? e.g. I define struct FILE with some padding, 
			   and you define it with some implementation-private fields? We handle
			   this at the libcrunch level; here we just want to record that there
			   are two different definitions out there.

			 *
			 * Consequences: 
			 * 
			 * - encode all base type properties
			 * - encode pointer encoding
			 * - encode byte- and bit-offsets of every field
			 */
			/* if we have it cached, return that */
			if (this->cached_summary_code) return this->cached_summary_code;
// 			auto found_cached = get_root().type_summary_code_cache.find(get_offset());
// 			if (found_cached != get_root().type_summary_code_cache.end())
// 			{
// 				this->cached_summary_code = found_cached->second;
// 				return found_cached->second;
// 			}
			/* helper function that understands the null (void) case */
			//auto type_summary_code = [](core::iterator_df<core::type_die> t) -> opt<uint32_t> {
			//	if (!t) return opt<uint32_t>(0);
			//	else return t->summary_code();
			//};
			auto name_for_type_die = [](core::iterator_df<core::type_die> t) -> opt<string> {
				if (t.is_a<dwarf::core::subprogram_die>())
				{
					/* When interpreted as types, subprograms don't have names. */
					return opt<string>();
				}
				else return *t.name_here();
			};
			opt<uint32_t> code_to_return;
			summary_code_word<uint32_t> output_word;
			auto outer_t = find_self().as_a<type_die>();
			decltype(outer_t) concrete_outer_t;
			if (!outer_t) { code_to_return = opt<uint32_t>(0); goto out; }
			concrete_outer_t = outer_t->get_concrete_type();
			
			using lib::Dwarf_Unsigned;
			using lib::Dwarf_Half;
			using namespace dwarf::core;
			/* Visit all the DIEs making up the type. At each one,
			 * in the relevant sequence, "<<" in its code.  */
			walk_type(outer_t, outer_t, /* pre_f */ [&output_word, name_for_type_die](
				iterator_df<type_die> t, iterator_df<program_element_die> reason) {
				debug(2) << "Pre-walking ";
				t.print(debug(2), 0); debug(2) << std::endl;
				// don't want concrete_t to be defined for most of this function...
				{
					auto concrete_t = t ? t->get_concrete_type() : t;
					// non-concrete? ignore but keep going -- walk_type will go down the chain
					// ACTUALLY: when we hit another type, 
					// FIXME: don't continue the walk -- use summary_code() on it, to benefit from caching
					// + ditto on other cases.
					// AH, but "stopping the walk" needs to distinguish skipping a subtree from stopping completely;
					// incrementalise "walk_type" as another kind of iterator? model on iterator_bf.
					// what about getting both pre- and post-order in one? we seem to get that here; use currently_walking?
					if (t != concrete_t) return true;
				}
				// void? our code is 0; keep going
				if (!t) { output_word << 0; return true; }
				// just a decl, no def? try to replace ourselves with the definition, and if not, null out. */
				if (t->get_declaration() && *t->get_declaration())
				{
					iterator_df<> found = t->find_definition();
					t = found.as_a<type_die>();
					if (!t)
					{
						debug(2) << "Detected that we have a declaration with no definition; returning no code" << std::endl;
						// NOTE that we will still get a post-visit, just no recursion
						// so we explicitly clear the output word (HACK)
						output_word.invalidate();
						return false;
					}
				}
				/* If we have a "reason" that is a member_die or inheritance_die, 
				 * */
				if (reason && (reason.is_a<data_member_die>()))
				{
					debug(2) << "This type is walked for reason of a member: ";
					reason.print(debug(2), 0);
					debug(2) << std::endl;
					// skip members that are mere declarations 
					if (reason->get_declaration() && *reason->get_declaration()) return false;

					// calculate its offset
					opt<Dwarf_Unsigned> opt_offset = reason.as_a<data_member_die>()
						->byte_offset_in_enclosing_type();
					if (!opt_offset)
					{
						debug(2) << "Warning: saw member " << *reason << " with no apparent offset." << endl;
						return false;
					}
					auto member_type = reason.as_a<with_dynamic_location_die>()->get_type();
					assert(member_type);
					assert(member_type.is_a<type_die>());

					output_word << (opt_offset ? *opt_offset : 0);
					// FIXME: also its bit offset!

					// type-visiting logic will do the member type
				}

				Dwarf_Half tag = t.tag_here();
				opt<string> maybe_fq_str = t->get_decl_file() ? t.enclosing_cu()->source_file_fq_pathname(
						*t->get_decl_file()) : opt<string>();
				std::ostringstream tmp;
				string fq_pathname_str = maybe_fq_str 
					? *maybe_fq_str 
					: t->get_decl_file() ? 
						t.enclosing_cu()->source_file_name(*t->get_decl_file())
						: /* okay, give up and use the offset after all */
					(tmp << std::hex << t.offset_here(), tmp.str());

				if (t.is_a<base_type_die>())
				{
					auto base_t = t.as_a<core::base_type_die>();
					unsigned encoding = base_t->get_encoding();
					assert(base_t->get_byte_size());
					unsigned byte_size = *base_t->get_byte_size();
					unsigned bit_size = base_t->get_bit_size() ? *base_t->get_bit_size() : byte_size * 8;
					unsigned bit_offset = base_t->get_bit_offset() ? *base_t->get_bit_offset() : 0;
					output_word << DW_TAG_base_type << encoding << byte_size << bit_size << bit_offset;
				} 
				else if (t.is_a<enumeration_type_die>())
				{
					// shift in the enumeration name
					if (t.name_here())
					{
						output_word << *name_for_type_die(t);
					} else output_word << std::hash<string>()(fq_pathname_str);

					// shift in the names and values of each enumerator
					auto enum_t = t.as_a<enumeration_type_die>();
					auto enumerators = enum_t.children().subseq_of<enumerator_die>();
					int last_enum_value = -1;
					for (auto i_enum = enumerators.first; i_enum != enumerators.second; ++i_enum)
					{
						output_word << *i_enum->get_name();
						if (i_enum->get_const_value())
						{
							last_enum_value = *i_enum->get_const_value();
							output_word << last_enum_value;
						} else output_word << last_enum_value++;
					}

					// walk_type will shift in the base type's summary code
				}
				else if (t.is_a<subrange_type_die>())
				{
					auto subrange_t = t.as_a<subrange_type_die>();

					// shift in the name, if any
					if (t.name_here())
					{
						output_word << *name_for_type_die(t);
					} else output_word << std::hash<string>()(fq_pathname_str);

					// walk_type will shift in the base type's summary code
					// post_f will shift in the upper bound and lower bound, if present
				} 
				else if (t.is_a<type_describing_subprogram_die>())
				{
					auto subp_t = t.as_a<type_describing_subprogram_die>();

					// first, shift in something to distinguish void(void) from void
					output_word << "()";

					// walk_type will shift in the argument and return types

					// post_f will deal with variadics
				}
				else if (t.is_a<address_holding_type_die>())
				{
					/* Danger: recursive data types! But walk_type deals with this. */
					auto ptr_t = t.as_a<core::address_holding_type_die>();
					unsigned ptr_size = *ptr_t->calculate_byte_size();
					unsigned addr_class = ptr_t->get_address_class() ? *ptr_t->get_address_class() : 0;
					if (addr_class != 0)
					{
						switch(addr_class) 
						{
							default:
								assert(false); // nobody seems to use this feature so far
							/* NOTE: There is also something called DWARF Pointer-Encoding (PEs).
							   This is a DWARF representation issue, used in frame info, and is not 
							   something we care about. */
						}
					}
					output_word << tag << ptr_size << addr_class;

					// walk_type deals with the target type
				}
				else if (t.is_a<with_data_members_die>())
				{
					// add in the name if we have it
					if (t.name_here())
					{
						output_word << *name_for_type_die(t);
					} else output_word << std::hash<string>()(fq_pathname_str);

					// for each member -- walk_type does that, and we do the member offset at the top of pre_f
				}
				else if (t.is_a<array_type_die>())
				{
					// nothing here -- it's in post_f
				}
				else if (t.is_a<string_type_die>())
				{
					// Fortran strings can be fixed-length or variable-length
					auto opt_dynamic_length = t.as_a<string_type_die>()->get_string_length();
					unsigned byte_len;
					if (opt_dynamic_length) byte_len = 0; /* treat as length 0 */ 
					else
					{
						auto opt_byte_size = t.as_a<string_type_die>()->fixed_length_in_bytes();
						assert(opt_byte_size);
						byte_len = *opt_byte_size;
					}
					output_word << DW_TAG_string_type << byte_len;
				}
				else if (t.is_a<unspecified_type_die>())
				{
					debug(2) << "Warning: saw unspecified type " << t;
					output_word.val = opt<uint32_t>();
				}
				else 
				{
					debug(2) << "Warning: didn't understand type " << t;
				}
				return true;
			},
			/* post_f */ [&output_word](
				iterator_df<type_die> t, iterator_df<program_element_die> reason) -> void {
				debug(2) << "Post-walking ";
				t.print(debug(2), 0); debug(2) << std::endl;
				if (t && t != t->get_concrete_type()) return;
				if (t.is_a<type_describing_subprogram_die>())
				{
					auto subp_t = t.as_a<type_describing_subprogram_die>();
					// distinguish with variadics
					if (subp_t->is_variadic()) output_word << "...";
				}
				else if (t.is_a<subrange_type_die>())
				{
					auto subrange_t = t.as_a<subrange_type_die>();

					/* Then shift in the upper bound and lower bound, if present
					 * NOTE: this means unnamed boundless subrange types have the 
					 * same code as their underlying type. This is probably what we want. */
					if (subrange_t->get_upper_bound()) output_word << *subrange_t->get_upper_bound();
					if (subrange_t->get_lower_bound()) output_word << *subrange_t->get_lower_bound();
				}
				else if (t.is_a<array_type_die>())
				{
					// walk_type has done the element type
					
					// if we're a member of something, we should be bounded in all dimensions
					auto opt_el_count = t.as_a<array_type_die>()->ultimate_element_count();
					output_word << (opt_el_count ? *opt_el_count : 0);
					// FIXME: also the factoring into dimensions needs to be taken into account
				}
				// pointer-to-incomplete, etc., will still give us incomplete answer
				assert (!t || !(output_word.val) || *output_word.val != 0);
			}
			); /* end call to walk_type */
			debug(2) << "Finished walk" << std::endl;
			code_to_return = output_word.val; 
		out:
			//get_root().type_summary_code_cache.insert(
			//	make_pair(get_offset(), code_to_return)
			//);
			debug(2) << "Got summary code: ";
			if (code_to_return) debug(2) << std::hex << *code_to_return << std::dec;
			else debug(2) << "(no code)";
			debug(2) << endl;
			this->cached_summary_code = code_to_return;
			return code_to_return;
		}
		opt<uint32_t> type_die::summary_code() const
		{
			if (this->cached_summary_code) return this->cached_summary_code;
			//return this->summary_code_using_walk_type();
			// return this->combined_summary_code_using_iterators<uint32_t>();
			
			/* BIG problem: incompleteness. The "combined summary" code, which
			 * factors in types reached by pointer traversals, is broken by
			 * this. */
			
			/* What about where our exploration is "cut off" by incompleteness?
			 * This is a real problem. We might need two summary codes -- one "local",
			 * where pointees are cut off and represented by their abstract names alone,
			 * and another that records the whole cycle *if it is available*.
			 * Need to think about the soundness of this. It might reduce to the sort
			 * of uniqueness we're going for with the symbol alias policy. FIXME.
			 * In the meantime, just to to canonicalise to definitions whenever we
			 * follow an edge, and bail if we can't do it. That's not right, because
			 * it means if we have
			 *
			 * struct opaque;
			 * struct Foo { double d; struct opaque *p; }
			 *
			 * and also
			 * struct opaque;
			 * struct Foo { int i; struct opaque *p; }
			 *
			 * They will both come out as without-code.
			 *
			 * So what happens? Will we will emit two __uniqtype__Foo instances?
			 * NO! We should *never* emit a real uniqtype without a code.
			 * ASSERT THIS somewhere.
			 *
			 * We need to define type_die::summary_code in terms of
			 * two helpers: local summary code and
			 * reachable summary code.
			 * (Note that reachability is not the same as cyclicity.
			 * The reachable thing still makes sense for acyclic DIEs.)
			 *
			 * Things without pointers have 0 for the reachable part.
			 * Things that reach incompletes have ffff for the reachable part.
			 * If we only care about local compatibility, we can ignore the
			 * reachability-based 16 bits.
			 * Modify the summary code code (the thing defining operator<<)
			 * to be parameterisable by width.
			 * Then make two 16-bit ones.
			 * Also FIXME the problem where bitfields of different bit offsets
			 * come out with the same code.
			 *
			 * NOTE there are several cases:
			 * 1. (acyclic, does not reach cycle,              graph-complete)
			 * 2. (acyclic, does not reach cycle,              graph-incomplete) "apparently acyclic"
			 * 3. (acyclic but reaches graph-complete cycle,   graph-complete)
			 * 4. (acyclic but reaches graph-complete cycle,   graph-incomplete) "apparently acyclic"
			 *    (acyclic but reaches graph-incomplete cycle, graph-complete) <-- impossible
			 * 5. (acyclic but reaches graph-incomplete cycle, graph-incomplete) "apparently acyclic"
			 *    (on a graph-incomplete cycle,                graph-complete) <-- impossible
			 * 6. (on a graph-incomplete cycle,                graph-incomplete)
			 * 7. (on a graph-complete cycle,                  graph-complete)
			 *    (on a graph-complete cycle,                  graph-incomplete) <-- impossible
			 *
			 * Here a "graph-incomplete cycle" means the SCC has outgoing edges
			 * to an incomplete type.
			 * If we're graph-incomplete but acyclic and reach only complete cycles if any,
			 * it means we can reach such an outgoing edge
			 * but its origin vertex isn't part of an SCC.
			 *
			 * Case 1 is straightforward and compositional by immediate-successor walk.
			 * Case 2 must return traversal ffff, compositional by immediate-successor walk.
			 * Case 3 is straightforward and compositional by immediate-successor walk.
			 * Case 4 must return traversal ffff, compositional by immediate-successor walk.
			 * Case 5 must return traversal ffff, compositional by SCC (SCC summary ffff)
			 * Case 6 must return traversal ffff, compositional by SCC (SCC summary ffff)
			 * Case 7 is nonstraightforward but compositional by the SCC summary algorithm.
			 *
			 * WAIT. The whole point of this is not to falsely distinguish
			 * two compilation units' views of what is actually the same type.
			 * So the summary code we use should be based on containment.
			 * Otherwise, if I have one CU in which a type has traversal-reachable other types
			 * that are opaque, its reachability summary will be ffff,
			 * whereas in a CU where the whole graph is fully defined it will be something else.
			 *
			 * The other options it not to compute summary codes on a per-DIE / CU basis
			 * at all. I.e. we're heading towards splitting the summary codes back into
			 * liballocstool when working at a whole-dynobj granularity.
			 * This makes sense.
			 * In short: we can compute per-CU containment-based codes,
			 * and we can make a good go of reachability-based codes in a whole-dynobj setting
			 * but not in a per-CU setting.
			 * Of course some structs will be incomplete even across the whole dynobj.
			 * So, anything that reaches them will have ffff reachability.
			 * It would be unusual, but completely possible, for some compilation unit
			 * in another dynobj
			 * to define such an opaque-to-clients struct
			 * such that it forms a cycle reaching back to itself via one of the client's
			 * own struct definitions. So, cutting off the abstraction boundary doesn't work.
			 * We would have to compute the summary codes in whole-process style,
			 * in liballocs proper!
			 *
			 * A likely compromise is
			 * - use containment-based summaries only
			 * - incompletes have no summary (but have an abstract name)
			 * - pointers to incompletes *do* have a summary?
			 *     -- we can compute one from the pointee abstract name
			 *     -- what about a pointer to the same type but complete?
			 *     -- what about a pointer to a concidentally like-named type, also complete?
			 *     -- do we want these three pointers to have the same uniqtype?
			 * - can we use link-time laziness to resolve this?
			 *     -- emit the pointee as simply the abstract name
			 *     -- say it has no summary code
			 *     -- BUT... a structure containing such a thing *does* have a summary
			 *     -- in this way, contained pointer fields can be emitted to reference
			 *                "__uniqtype____PTR_struct_name"
			 *         or equally to   "__uniqtype_0123456a___PTR_struct_name"
			 *         and they *should* come out the same at link time.
			 * - structures containing pointers to incompletes *do* have a summary
			 *      but it is computed using those pointers' abstract names
			 *
			 * YES i.e. pointer members are summarised using their abstract names,
			 * regardless of whether a summary is available.
			 * Incompletes have no summary.
			 * Pointers to incompletes have no summary.
			 * Pointers to completes do have a summary.
			 * ARGH. But this violates our "never emit without a summary code" invariant.
			 * If we have "struct S" defined in some compilation unit,
			 * we might reasonably *never* make S* except in a CU where S is opaque/incomplete.
			 * So some client that just emits a reference to __uniqtype____PTR_S
			 * will cause a link failure.
			 * Answer:
			 * we do emit the codeless definition,
			 * but as a *weak* definition and *not* in a section group.
			 * HMM. The dynamic linking boundary might mess with this. A "winning" weak definition
			 * in an executable or preloaded library
			 * will trump an actual alias-of-summarised-full-definition symbol in the meta-obj.
			 */
			auto computed = this->containment_summary_code<uint32_t>(
				/* Pass ourselves as the recursive call, to take advantage of caching. */
				[](iterator_df<type_die> arg) -> opt<uint32_t> {
					return summary_code_for_type(arg);
				}
			);
			this->cached_summary_code = computed;
			return computed;
		}
		
		/* reverse-engineering helper. */
		iterator_df<type_die> type_iterator_df_base::source_vertex_for(
			const iterator_df<type_die>& target,
			const iterator_df<program_element_die>& reason
		) const
		{
			if (!reason) return iterator_base::END;
			if (reason.is_a<type_describing_subprogram_die>())
			{
				/* The target is the return type, so the source is the reason itself. */
				return reason.as_a<type_describing_subprogram_die>();
			}
			else if (reason.is_a<data_member_die>())
			{
				/* The source is the containing with_data_members_type. */
				auto parent = reason.parent();
				assert(parent.is_a<with_data_members_die>());
				return parent.as_a<with_data_members_die>();
			}
			else if (reason.is_a<formal_parameter_die>())
			{
				/* The source is the containing type_describing_subprogram. */
				auto parent = reason.parent();
				assert(parent.is_a<type_describing_subprogram_die>());
				return parent.as_a<type_describing_subprogram_die>();
			}
			else if (reason.is_a<type_chain_die>() || reason.is_a<subrange_type_die>()
				|| reason.is_a<enumeration_type_die>())
			{
				/* It's just the type that we came from. */
				return reason.as_a<type_die>();
			}
			else
			{
				assert(false);
				abort();
			}
		}

		inline type_edge_compare::type_edge_compare() : function([](const type_edge& arg1, const type_edge& arg2) -> bool {
			auto& source1 = arg1.source();
			auto& label1 = arg1.label();
			auto& target1 = arg1.target();
			auto& source2 = arg2.source();
			auto& label2 = arg2.label();
			auto& target2 = arg2.target();

			/* The main trick here is that we want the comparison to follow
			 * abstract equality. What does that mean? Among other things,
			 * if we have two identical type graphs in different CUs,
			 * then regardless of their numerical DWARF offsets or relative
			 * spacing/ordering in the file, their edges should compare equal.
			 * BUT WAIT. Do we ever compare edges originating in different CUs?
			 * Remember that we're building the SCCs so that we can characterise
			 * cyclic types in terms of their edge sets. During the building of
			 * SCCs, we use this comparator to store the edges in sets. There,
			 * an arbitrary ordering is fine -- everything in the set is from
			 * some common cycle and hence from the same CU. It's when we're
			 * actually processing SCCs into summary codes (HOW does that work?)
			 * that we need an abstract relation among the edges.
			 * 
			 * Each source/dest vertex has an abstract name. So it's only really 
			 * the "reason" that needs abstracting. We don't want to use DWARF offsets
			 * or offset differences because they may vary across CUs. We could use
			 * ordinals ("first fp child", "seventh member child") etc. if they were cheap
			 * to compute. We could make this happen by having type_iterator_df remember
			 * the ordinal for its "reason".
			 */
	// 		/* Logically the edge label refers to the "reason" DIE. 
	// 		 * Can we say this must be the source DIE or an an immediate child?
	// 		 * Currently reasons are either members or inheritances or formal parameters
	// 		 * or the DIE itself. */
	// 		if (source_vertex() == reason())
	// 		{
	// 			return 0;
	// 		}
	// 		else
	// 		{
	// 			assert(source_vertex().parent() == reason());
	// 			// we have to linear-search :-(
	// 			auto seq = source_vertex().parent().children();
	// 			unsigned n = 1;
	// 			for (i_child = seq.first; i_child != seq.second; ++n, ++i_child)
	// 			{
	// 				if (i_child == reason()) return n;
	// 			}
	// 			assert(false); abort();
	// 			// FIXME: why use a number? why not just use the reason DIE?
	// 			// I think it's something about collating edge sets correctly;
	// 			// it's the analogous abstraction as "abstract names" for type DIEs.
	// 			// So can we factor this into the SCC std::set comparator?
	// 		}
			return arg1 < arg2; // FIXME
		}){}
		
		bool type_edge::reason_is_traversal() const
		{
			return this->source().is_a<address_holding_type_die>();
		}

		opt<type_scc_t> type_die::get_scc() const
		{
			assert(get_root().live_dies.find(get_offset()) != get_root().live_dies.end());
			assert(get_root().live_dies.find(get_offset())->second
				== dynamic_cast<const basic_die *>(this));
			
			/* This is a generic implementation. If subclasses know that they can 
			 * never be part of a cycle, they're allowed to override to just the following.
				return opt<type_scc_t>();
			 * FIXME: do this. Base types. others? */
			
			// cheque the cache. we might have a null pointer cached, meaning "not cyclic"
			if (opt_cached_scc) return *opt_cached_scc ? **opt_cached_scc : opt<type_scc_t>();
			
			iterator_df<type_die> start_t = find_self();
			
			// if we're a declaration, that's bad
			if (start_t->get_declaration() && *start_t->get_declaration()) return opt<type_scc_t>();
			
			//get_all_sccs(start_t.root());
			/* We could run DFS on the whole graph. However,
			 * it's no better value than what we really do.
			 * We are committed to doing a depth-first exploration of the type graph *reachable
			 * from our current position*. So the efficient thing is to compute *all* SCCs that
			 * are reachable from the start position. Then, even if we find that *we* are
			 * not part of any cycle/SCC, we might cache some SCCs in other type_dies that
			 * we find that *are* in cycles.
			 *
			 * Note that from many positions we can reach a cycle, but 
			 * we are not part of that cycle.
			 * If we are not part of *any* cycle, we will come out as a singleton SCC.
			 * We do not cache singleton SCCs as such, because they are not cyclic.
			 * Recall: we're interested in summary codes; we can use the simpler
			 * recursive summary code algorithm for acyclic cases.
			 */
			
			struct type_edges_df_scc_builder_iterator : public type_iterator_df_base,
						  public boost::iterator_facade<
							type_edges_df_scc_builder_iterator
						  , type_die
						  , boost::forward_traversal_tag
						  , type_die& /* Reference */
						  , Dwarf_Signed /* difference */
						  >
			{
				typedef type_edges_df_scc_builder_iterator self;
				typedef type_iterator_df_base super;
				friend class boost::iterator_core_access;

				std::deque< iterator_df<type_die> > seen_but_not_assigned;
				std::deque< iterator_df<type_die> > perhaps_same_scc;
				unsigned seen_count;
				map< iterator_df<type_die>, unsigned > preorder_numbers;
				unsigned component_count;
				map< iterator_df<type_die>, unsigned > component_numbers;
				multimap< unsigned, iterator_df<type_die> > component_members;

				type_edges_df_scc_builder_iterator(const iterator_df<type_die>& t)
				 : type_iterator_df_base(t),
				   seen_but_not_assigned(), perhaps_same_scc(), seen_count(0), preorder_numbers(),
				   component_count(0), component_numbers(), component_members()
				{
					base_reference() = t.base();
					this->m_reason = END;
					assert(pos_colour() == WHITE);
				}
				
				type_edges_df_scc_builder_iterator(const self& arg)
				 : type_iterator_df_base(arg),
				   seen_but_not_assigned(arg.seen_but_not_assigned),
				   perhaps_same_scc(arg.perhaps_same_scc),
				   seen_count(arg.seen_count),
				   preorder_numbers(arg.preorder_numbers),
				   component_count(arg.component_count), component_numbers(arg.component_numbers),
				   component_members(arg.component_members)
				{
					// we should not copy ourselves
					assert(false);
				}
				
				/* According to Wikipedia
				 * https://en.wikipedia.org/wiki/Path-based_strong_component_algorithm
				 * we can build SCCs like this.
				 
				 * "Stack S contains all the vertices that have not yet been assigned
				 * to a strongly connected component,
				 * in the order in which the depth-first search reaches the vertices.
				 *
				 * "Stack P contains vertices that have not yet been determined
				 * to belong to different strongly connected components from each other.
				 *
				 * "It also uses a counter C of the number of vertices reached so far,
				 * which it uses to compute the preorder numbers of the vertices.
				 
				 * "The overall algorithm consists of a loop through the vertices of the graph,
				 * calling [the following] recursive search on each vertex
				 * that does not yet have a preorder number assigned to it.
				 
				 * "When the depth-first search reaches a vertex v, 
				 * the algorithm performs the following steps:

				 * 1. Set the preorder number of v to C, and increment C.
				 * 2. Push v onto S and also onto P.
				 * 3. For each edge from v to a neighboring vertex w:
				 * 3a. If the preorder number of w has not yet been assigned, recursively search w;
				 * 3b: Otherwise, if w has not yet been assigned to a strongly connected component:
				 * 3b(i)  Repeatedly pop vertices from P 
				 *        until the top element of P has a preorder number
				 *        less than or equal to the preorder number of w.
				 * 4. If v is the top element of P:
				 * 4a. Pop vertices from S until v has been popped,
				 *       and assign the popped vertices to a new component.
				 * 4b. Pop v from P.
				 *
				 * The first non-obvious thing is step 3a. But this just means that
				 * when we turn a node from white to grey, we assign it a preorder number.
				 * Similarly, step 3b happens when we're at a white node
				 * and see an outgoing edge to a grey or black node.
				 * Step 4 is trickier.
				 * It happens when we've explored all outgoing edges *to white nodes*.
				 * In other words, it happens after the last such edge.
				 * So just test whether we're the last such edge.
				 * If we are, we need to schedule step 4 for *after*
				 * we're recursively finished exploring that edge.
				 * How to do that?
				 * We can overload increment().
				 *
				 * We also need to short-circuit the algorithm in the case where
				 * we reach a node that already has an SCC cached.
				 * If we can reach an SCC,
				 * and we're not in *that* SCC,
				 * we know we're not reachable from that SCC.
				 * Does it follow that we're not in *any* SCC?
				 * NO! Imagine two loops joined by a single directed arc.
				 * If we want to short-circuit the algorithm in such cases,
				 * what do we do?
				 * We could "collapse those SCCs to a point". That means remembering their
				 * outgoing edges.
				 * Alternatively we could just pretend that the edge does not exist.
				 * I don't think this affects our SCC calculation.
				 * It also doesn't cut us off from finding other SCCs reachable from there,
				 * because they were explored when the reached SCC was formed.
				 *
				 * This sounds too complicated for now; maybe better just to re-explore?
				 * Or, hmm, we can factor this optimisation into our increment() overload
				 * quite easily.
				 */

				//iterator_df<type_die> target() const { return *this; }

				void print_stack() const
				{
					debug_expensive(5, << "[");
					for (auto i = m_stack.begin(); i != m_stack.end(); ++i)
					{
						if (i != m_stack.begin()) debug_expensive(5, << ", ");
						debug_expensive(5, << std::hex << 
							(i->first.is_end_position() ? (Dwarf_Off)-1 : i->first.offset_here()) 
								<< std::dec);
					}
					debug_expensive(5, << "]" << std::endl);
				}
				
				void increment()
				{
					debug_expensive(5, << "Incrementing SCC progress from "
						<< base().summary() << "; reason is " << reason().summary()
						<< "; stack has " << this->m_stack.size() << " elements." << std::endl);
					// FIXME: this is pasted from type_iterator_df_edges::increment
					pair<iterator_df<type_die>, iterator_df<program_element_die> > found_next;
					while ((base() || reason()) && (!m_stack.empty() || pos_colour() == WHITE))
					{
						type_iterator_outgoing_edges i_succ;
						switch (pos_colour())
						{
						case GREY:
							assert(false); abort();
							// we walk through all outgoing edges below, so we're either
							// white or we're black
						case BLACK:
							debug_expensive(5, << "Backtracking from now-black node  "
								<< base().summary() << std::endl);
							// re-construct i_succ; if from a blackened position, and it backtracks
							i_succ = type_iterator_outgoing_edges(m_stack);
							m_stack.pop_back();
							// if our stack is empty, it means we just blackened the start node
							if (m_stack.empty()) { assert(!i_succ); goto hit_end; } // will break
							// our position is whatever the stack says we've backtracked to
							this->base_reference() = m_stack.back().first;
							this->m_reason = m_stack.back().second;
							// ... but it might still be effectively black,
							// i.e. it might have no more outgoing edges.
							// If so, then i_succ will be null, and we'll skip the loop.
							// Else we're at a grey node i.e. one which we've partially explored
							// (i.e. at least the first outgoing edge)
							// and i_succ is iterating through the remaining outgoing edges
							goto resume_grey;
						case WHITE:
							// give it a preorder number; push v onto S and onto P
							preorder_numbers.insert(make_pair(base(), seen_count++));
							debug_expensive(5, << "Preorder number of "
								<< base().summary() << " assigned: " << seen_count - 1
								<< std::endl);

							seen_but_not_assigned.push_back(base());
							perhaps_same_scc.push_back(base());

							// make ourselves grey now -- pushing ourselves onto stack is a "silent transition"
							m_stack.push_back(make_pair(base(), reason()));
							assert(pos_colour() == GREY);
							// look at our outgoing edges. we will follow any to white nodes.
							i_succ = type_iterator_outgoing_edges(*this);
							// now i_succ points at the first outgoing edge (successor) of *this,
							// and our stack's back position is *this (effectively)
						resume_grey:
							while (i_succ.base() || i_succ.reason())
							{
								debug_expensive(5, << "Saw outgoing edge from originally-white node: "
									<< base().summary() << " -----> " << i_succ.summary() << std::endl);
								
								// if this node already has an SCC, then by construction it
								// doesn't reach back to us, and isn't in our SCC. So we
								// cut off our exploration here.
								if (i_succ.base() && i_succ->opt_cached_scc)
								{
									debug_expensive(5, << "Pretending edge doesn't exist "
										<< "because target already has a cached SCC result: "
										<< base().summary() << " -----> " << i_succ.summary() << std::endl);
									goto next_edge;
								}

								// if we're pointing at the null DIE (void type)... what?
								// I think we can treat it like any node.

								// i_succ does *not* know our node colours
								switch (colour_of(i_succ))
								{
									case WHITE:
										debug(5) << "Edge target is white" << std::endl;
										// okay -- we move here
										// next increment() will recursively search
										this->base_reference() = std::move(i_succ);
										this->m_reason = i_succ.reason();
										// now we are white again
										assert(pos_colour() == WHITE);
										return;
									case GREY:
									case BLACK:
										debug(5) << "Edge target is grey or black" << std::endl;
										// we got some other node w
										// "if w has not yet been assigned..."
										if (component_numbers.find(i_succ)
											== component_numbers.end())
										{
											auto found = preorder_numbers.find(i_succ);
											assert(found != preorder_numbers.end());
											unsigned this_preorder_number = found->second;
											while (preorder_numbers[perhaps_same_scc.back()]
													> this_preorder_number)
											{
												perhaps_same_scc.pop_back();
											}
										}
										break;
								}
							next_edge:
								++i_succ;
							}

							debug_expensive(5, << "Exhausted children of "
									<< base().summary() << "; stack is: ");
							print_stack();
							debug(5) << std::endl;
							// if we got here, we exhausted the children so we're onto step 4
							if (perhaps_same_scc.back() == base())
							{
								iterator_df<type_die> popped;
								unsigned new_component = component_count++;
								debug_expensive(5, << "Created component number " << new_component
									<< std::endl);
								do
								{
									popped = seen_but_not_assigned.back();
									seen_but_not_assigned.pop_back();
									component_numbers.insert(make_pair(popped, new_component));
									component_members.insert(make_pair(new_component, popped));
									debug_expensive(5, << "Component number " << new_component
										<< " contains seen-but-not-assigned " << popped.summary() << std::endl);
								} while (popped != base());

								perhaps_same_scc.pop_back();
							}

							// exhausted children means we're blackened
							black_offsets.insert(base());
							// we're still pointing at the black pos;
							// if we have anything left on the stack, we'll go round the
							// while loop again
							break;
						} // end switch
					} // end while
				hit_end:
					this->base_reference() = END;
					this->m_reason = END;
				}
			};

			/* We will visit all reachable edges.... */
			type_edges_df_scc_builder_iterator i_white(start_t.as_a<type_die>());
			assert(i_white.component_count == 0);
			assert(!start_t || i_white);
			for (; i_white; ++i_white)
			{
				/* REMEMBER: type iterators are allowed to walk the "no DIE" (void) case,
				 * so we might have END here. */
				debug_expensive(5, << "Reached white node: " << i_white.summary()
					<< "; reason: " << i_white.reason().summary() << "; stack is: ");
				i_white.print_stack();
			}
			debug(5) << "Explored all reachable nodes. Total SCCs: "
				<< i_white.component_count << std::endl;

			/* We've now built a load of SCCs. */
			std::vector< shared_ptr<type_scc_t> > created_sccs;
			for (unsigned i = 0; i < i_white.component_count; ++i)
			{
				shared_ptr<type_scc_t> p_scc = std::make_shared<type_scc_t>();
				debug_expensive(5, << "Created a shared SCC structure at " << p_scc.get() << std::endl);
				created_sccs.push_back(p_scc);
			}
			for (auto i_pair = i_white.component_numbers.begin();
				i_pair != i_white.component_numbers.end();
				++i_pair)
			{
				// the current type is in exactly one SCC
				auto p_scc = created_sccs.at(i_pair->second);
				debug_expensive(1, << i_pair->first << " is in component " << i_pair->second
					<< std::endl);
				type_scc_t& scc = *p_scc;
				// we've got all the nodes in the scc, but we want all the edges.
				// we simply add all the outgoing edges whose target is in the SCC
				// How do we walk all the outgoing edges? Use the iterator for the job.
				
				// follow its outgoing edges
				type_iterator_outgoing_edges i_t(
					type_iterator_df_edges(
						//type_iterator_df_base(
							i_pair->first
						//)
					)
				);
				// for each outgoing edge of the current type...
				std::set< pair<string, string> > edges_sorted;
				for (; i_t; ++i_t)
				{
					/* insert this edge iff the target is in the same scc as our i_pair. */
					auto found_component = i_white.component_numbers.find(i_t);
					if (found_component != i_white.component_numbers.end()
						&& found_component->second == i_pair->second)
					{
						scc.insert(i_t.as_incoming_edge());
						/* We also need to calculate the summary word for the
						 * SCC. We make a sorted list of all the edges, as pairs of
						 * abstract names. We then stuff them into the summary
						 * word in order. */
						auto pair = make_pair(
							abstract_name_for_type(i_t),
							abstract_name_for_type(i_t.as_incoming_edge().first.first)
						);
						edges_sorted.insert(pair);
					}
				}
				
				debug_expensive(5, << "SCC number " << i_pair->second <<
					" has the following abstract name edges" << std::endl);
				for (auto i_edge = edges_sorted.begin(); i_edge != edges_sorted.end();
						++i_edge)
				{
					scc.edges_summary << i_edge->first;
					scc.edges_summary << i_edge->second;
					debug_expensive(5, << i_edge->first << " ----> " << i_edge->second << std::endl);
				}
			}

			/* Install the SCCs in the relevant type DIEs. */
			for (unsigned i = 0; i < i_white.component_count; ++i)
			{
				auto types = i_white.component_members.equal_range(i);
				auto p_scc = created_sccs.at(i);
				type_scc_t& scc = *p_scc;
				if (scc.size() == 0)
				{
					/* Edgeless SCCs are not SCCs at all. They're DIEs that are
					 * not in any cycle. We want to cache this fact. */
					for (auto i_t = types.first; i_t != types.second; ++i_t)
					{
						if (i_t->second) i_t->second->opt_cached_scc
						= optional<shared_ptr<type_scc_t> >(shared_ptr<type_scc_t>());
					}
				}
				else
				{
					for (auto i_t = types.first; i_t != types.second; ++i_t)
					{
						/* NOTE that naively, we might have come up empty for ourselves,
						 * i.e. found we're in only a singleton SCC (no cycle),
						 * but still discovered other DIEs that don't reach back to us.
						 * So we can't be sure that we haven't installed an SCC here
						 * before.
						 * We avoid this by pretending (above) edges to SCC-having nodes 
						 * don't exist. */
						debug_expensive(5, << "Installing SCC in DIE " << i_t->second.summary()
							<< std::endl);
						assert(!i_t->second->opt_cached_scc);
						i_t->second->opt_cached_scc = p_scc;
						// now we've calculated the SCC, make it sticky
						root_die::ptr_type p = &i_t->second.dereference();
						i_t->second.root().sticky_dies.insert(make_pair(i_t->second.offset_here(), p));
					}
					debug_expensive(5, << "SCC number " << i << " has summary code "
						<< (scc.edges_summary.val ? *scc.edges_summary.val : 0)
						<< std::endl);
				}
			}
			debug(5) << "Finished installing SCCs" << std::endl;

			if (this->opt_cached_scc && *this->opt_cached_scc)
			{
				return opt<type_scc_t>(**this->opt_cached_scc);
			} else return opt<type_scc_t>();
		}
		type_die::equal_result_t type_die::equal(iterator_df<type_die> t, 
			const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal,
			opt<string&> reason_for_caller /* = opt<string&>() */
			) const
		{
			/* Remember that this is a stricter test than type synonymy, e.g.
			 * a typedef of 'int' does not equal the real 'int'. */
			auto& r = get_root();
			auto self = find_self();
			set< pair< iterator_df<type_die>, iterator_df<type_die> > > new_assuming_equal;
			new_assuming_equal = assuming_equal;// new_assuming_equal.insert(make_pair(self, t));
			/* this is our core equality test. */
			auto equal_nocache = [&new_assuming_equal]
			(iterator_df<type_die> t1, iterator_df<type_die> t2) -> pair<equal_result_t, string> {
				string may_equal_reason;
				auto r1 = t1->may_equal(t2, new_assuming_equal, may_equal_reason);
				if (!r1) return make_pair(UNEQUAL, "self may_equal: " + may_equal_reason);

				// to make the reversed may_equal call, we no longer need to flip our set of pairs
				// because we test above for the elements in either order, i.e. it's effectively
				// a set of *unordered* pairs.
				auto r2 = t2->may_equal(t1, /*flipped_set*/ new_assuming_equal, may_equal_reason);
				if (!r2) return make_pair(UNEQUAL, "t may_equal: " + may_equal_reason);
				if (r1 == EQUAL_BY_ASSUMPTION || r2 == EQUAL_BY_ASSUMPTION) return make_pair(
					EQUAL_BY_ASSUMPTION, "");
				return make_pair(EQUAL, "");
			};

			// iterator equality always implies type equality
			if (self == t) return EQUAL;
			
			/* We use 'assuming_equal' to break cycles: when recursing from a pair of
			 * with_data_members types, we assume they are equal. If they can be
			 * equal given that assumption, they are equal.
			 *
			 * This affects caching of results. Right here, we obviously return without
			 * caching the result: we're just acting on an assumption. But if our
			 * *caller* is working on the same assumption, it also should refrain from
			 * caching the result it sees for us. Does this arise naturally? No because
			 * we're happy to call may_equal passing new_assuming_equal. It is wrong to
			 * then cache the result of that. See 'goto return_after_cache' below. */
			if (assuming_equal.find(make_pair(self, t)) != assuming_equal.end()
			 || assuming_equal.find(make_pair(t, self)) != assuming_equal.end())
			{
				return EQUAL_BY_ASSUMPTION;
			}
			debug_expensive(5, << "type_die::equal called non-trivially on "
				<< self << " and " << t << " assuming " << assuming_equal.size()
				<< " pairs equal" << endl);
// #define DISABLE_TYPE_EQUALITY_CACHE
#ifndef DISABLE_TYPE_EQUALITY_CACHE
			/* If the two iterators share a root, check the cache.
			 * For positive results, we use a linked list of equivalence classes
			 * (sets of offsets), with a map from offsets to a pointer to their
			 * equivalence class. Then our initial cache check is a search in this
			 * set. A negative result is cached by ensuring equivalence classes
			 * exist for the two compared DIEs. */
			auto equiv_class_ptr = [=](iterator_df<type_die> t1) -> opt<list<set<Dwarf_Off>>::iterator> {
				auto &m = t1.root().equivalence_class_of;
				auto found = m.find(t1.offset_here());
				if (found == m.end()) return opt<list<set<Dwarf_Off>>::iterator>();
				return found->second;
			};
			auto check_cached_result = [=](iterator_df<type_die> t1, iterator_df<type_die> t2)
			 -> opt<bool> {
				/* Check for positive cached result. Do they both have an equivalence class? */
				auto eq_class_of_t1 = equiv_class_ptr(t1);
				auto eq_class_of_t2 = equiv_class_ptr(t2);
				if (eq_class_of_t1 && eq_class_of_t2 &&
					eq_class_of_t1 == eq_class_of_t2)
				{
					// this is a cached 'true' result
					debug_expensive(5, << "Hit equivalence-class equality cache positively (size: "
						<< t1.root().equivalence_class_of.size() << ") comparing "
						<< t1.summary() << " with " << t2.summary() << endl);
					return opt<bool>(true);
				}
				if (eq_class_of_t1 && eq_class_of_t2 &&
					eq_class_of_t1 != eq_class_of_t2)
				{
					// do we need to merge the classes? only if they have the same root
					Dwarf_Off t1_rep = *(*eq_class_of_t1)->begin();
					Dwarf_Off t2_rep = *(*eq_class_of_t2)->begin();
					if (&t1.root() == &t2.root() &&
						t1.root().pos(t1_rep).as_a<type_die>() ==
						t2.root().pos(t2_rep).as_a<type_die>())
					{
						// this is a cached 'true' result
						debug_expensive(5, << "Hit equivalence-class equality cache positively after merging (size: "
							<< t1.root().equivalence_class_of.size() << ") comparing "
							<< t1.summary() << " with " << t2.summary() << endl);
						return opt<bool>(true);
					}
					// this is a negative cached result
					if (summary_code_for_type(self) == summary_code_for_type(t)
						&& !self.is_a<subprogram_die>()
						&& self.as_a<type_die>()->get_concrete_type().as_a<basic_die>() == self.as_a<basic_die>()
						&& t.as_a<type_die>()->get_concrete_type().as_a<basic_die>() == t.as_a<basic_die>())
					{
						// this is fishy
						debug(1) << "Surprising: same summary code("
							<< std::hex << summary_code_for_type(self)
							<< "), concrete, but unequal: "
							<< self.summary() << " and " << t.summary() << std::endl;
						if (self.is_a<with_data_members_die>())
						{
							auto p1 = equal_nocache(self, t);
							debug(1) << "self equal t? " << std::boolalpha << p1.first
								<< ", reason " << p1.second << endl;
						}
					}
					debug_expensive(5, << "Hit equivalence-class equality cache negatively (size: "
						<< self.root().equivalence_class_of.size() << ") comparing "
						<< summary() << " with " << t.summary()
						<< " (summary codes " << std::hex << summary_code_for_type(self) << " and "
							<< std::hex << summary_code_for_type(t) << ")"
						<< " (equiv classes " << &*eq_class_of_t1 << " (size " << (*eq_class_of_t1)->size()
						<< ") and "
						<< &*eq_class_of_t2 << " (size " << (*eq_class_of_t2)->size() << ")" << endl);
					return opt<bool>(false); // i.e. the cached result
				}
				return opt<bool>();
			}; // end check_cached_result lambda
			opt<bool> cached = check_cached_result(self, t);
			if (cached)
			{
				opt<bool> reversed = check_cached_result(t, self);
				assert(reversed.is_initialized());
				assert(*reversed == *cached);
				return *cached ? EQUAL : UNEQUAL; // we never cache 'by assumption' results
			}
			if (t && &t.root() == &self.root())
			{
				debug_expensive(5, << "Missed equivalence-class equality cache (size: "
					<< self.root().equivalence_class_of.size() << ") comparing "
					<< summary() << " with " << t.summary() << endl);
			}
			else
			{
				debug_expensive(5, << "Fishy: comparing DIEs from distinct roots" << std::endl);
			}
#endif
			equal_result_t ret;
			opt<uint32_t> this_summary_code;
			opt<uint32_t> t_summary_code;
			pair<equal_result_t, string> retpair;
			string reason = "";
			std::function<string(list<set<Dwarf_Off>>::iterator)> print_equivalence_class;
			opt<bool> re_checked;

			// quick tests that can rule out a match -- all may_equal definitions
			// should respect this
			if (t.tag_here() != get_tag()) { ret = UNEQUAL; reason = "tag"; goto return_and_cache; }
			if (t.name_here() && get_name() && *t.name_here() != *get_name())
			{ ret = UNEQUAL; reason = "name"; goto return_and_cache; }

			// try using summary codes
			this_summary_code = this->summary_code();
			t_summary_code = t->summary_code();
			if (this_summary_code && t_summary_code)
			{
				/* HMM. toyed with just using equality, BUT: summary codes may collide,
				 * and typedefs etc. have the same summary code but are distinct DIEs. */
				if (*this_summary_code != *t_summary_code)
				{
					ret = UNEQUAL;
					std::ostringstream s; s << "summary code ("
						<< std::hex << *this_summary_code << " vs " 
						<< std::hex << *t_summary_code
						<< std::dec << ")";
					reason = s.str();
					goto return_and_cache;
				}
			}
			if (!!this_summary_code != !!t_summary_code)
			{
				/* If one has a summary code and the other doesn't, it means
				 * one is incomplete (or dependent on an incomplete) and the
				 * other is not. */
				debug(5) << "Interesting: incompleteness mismatch comparing  " << summary() << " with " << t.summary() << endl;
				ret = UNEQUAL; reason = "summary presence";
				goto return_and_cache;
			}
			// ran out of shortcuts, so use equal_nocache to call the actual may_equal methods
			retpair = equal_nocache(self, t);
			ret = retpair.first;
			reason = retpair.second;
			// fall through, joining earlier paths that skipped the equal_nocache
		return_and_cache:
			debug_expensive(5, << "type_die::equal() after missing cache found " << self << " and " << t
				<< (ret ?  " equal" : " not equal, for reason ")
				<< (ret ? string("") : reason)
				<< endl);
			/* If we're returning false, we'd better not be the same DIE. */
			assert(ret || !t || 
				!(&t.get_root() == &self.get_root() && t.offset_here() == self.offset_here()));
			/* If we're testing modulo an assumption, the result is not cacheable.
			 * NOTE: this is not the only way that positive-test pairs get into the
			 * cache: by merging into equivalence classes we exploit transitivity
			 * to, in effect, cache many pairs at once.
			 * XXX: we also never seem to merge into an existing equivalence class. */
			if (ret == EQUAL_BY_ASSUMPTION)
			{
				debug_expensive(5,
					<< "Positive result is not cacheable because of assumptions " << std::dec << assuming_equal.size()
					<< " (first pair: "
					<< assuming_equal.begin()->first.summary() << " and "
					<< assuming_equal.begin()->second.summary() << ")"
					<< endl);

				goto return_after_cache;
			}
			else
			{
				debug_expensive(5, << (ret ? "Positive" : "Negative") 
					<< " result is cacheable" << endl);
			}
#ifndef DISABLE_TYPE_EQUALITY_CACHE
			/* If the two iterators share a root, cache the result. One quirk is that
			 * in the case of recursive types, the may_equal check we did above may
			 * have already cached us, because it may have run the test we're doing
			 * a second time. So the cache state may have changed since we first checked.
			 *
			 * Perhaps we can avoid this by *always* adding ourselves to the assuming_equal
			 * set? That will cut off the recursion at the earliest possible repeat.
			 * Previously we relied on with_data_members_die::may_equal expanding the
			 * assumed-equal set. We were seeing it take multiple hits to cut off the recursion
			 * because if we were a pointer-to-struct type, then we might hit ourselves
			 * as a struct member before we hit the struct itself a second time. The
			 * struct itself would be compared with an expanded assuming_equal, preventing
			 * infinite recursion, but our call chain still has two comparisons of the
			 * pointer type active, and the inner one will cache a result before the
			 * outer one runs the code below.
			 *
			 * Always adding ourselves to the assumed-equal set means any sub-check we
			 * do will not be cached, now that we have (correctly) fixed the cache
			 * logic to avoid caching these predicated-on-assumption results. So it is
			 * not a good value option. Instead the code below needs to be robust to the
			 * cache having changed. The easiest thing is to re-run the check_cached_result
			 * test and do nothing if it returns something (anything).
			 */
			// there wasn't a result in the cache earlier; is there one now?
			/* opt<bool> */ re_checked = check_cached_result(self, t);
			if (re_checked) { assert(*re_checked == ret); goto return_after_cache; }
			print_equivalence_class = [](list<set<Dwarf_Off>>::iterator i_cl) -> string {
				std::ostringstream s;
				s << "{";
				for (auto i_off = i_cl->begin();
					i_off != i_cl->end();
					++i_off)
				{
					if (i_off != i_cl->begin()) s << ", ";
					s << std::hex << *i_off;
				}
				s << "}" << std::endl;
				return s.str();
			};
			if (t && &t.root() == &self.root())
			{
				assert(self.offset_here() != t.offset_here());
				opt<list<set<Dwarf_Off>>::iterator> eq_class_of_t = equiv_class_ptr(t);
				opt<list<set<Dwarf_Off>>::iterator> eq_class_of_self = equiv_class_ptr(self);
				//if (!ret) assert(!eq_class_of_t || eq_class_of_t != eq_class_of_self);

				/* We index equivalence classes by summary code to help find existing ones.
				 * See comment below about 'don't rush into creating a new class'. */
				auto find_or_create_eq_class = [equal_nocache, print_equivalence_class]
				(iterator_df<type_die> new_rep,
				 opt<uint32_t> summary_code) -> list<set<Dwarf_Off>>::iterator {
					/* A small number of equivalence classes (usually 1) will
					 * apply to type DIEs having this summary code. Identify them. */
					auto matching_by_summary_code = new_rep.root().
						equivalence_classes_by_summary_code.equal_range(summary_code);
					auto i_ent = matching_by_summary_code.first;
					for (; i_ent != matching_by_summary_code.second; ++i_ent)
					{
						auto& iter = i_ent->second;
						auto& the_set = *iter;
						Dwarf_Off rep_off = *(*iter).begin();
						iterator_df<type_die> rep_t = new_rep.root().pos(rep_off);
						if (equal_nocache(rep_t, new_rep).first) /* found it! */
						{
							the_set.insert(new_rep.offset_here());
							debug_expensive(5, << "Inferred we can merge "
								<< new_rep << " into existing equivalence class at "
								<< &*i_ent->second
								<< " s.t. it is now "
								<< print_equivalence_class(i_ent->second)
								<< endl);
							break;
							// we want to assert that they all equal rep, not just the first one
						}
					}
					// if we exited the loop early, we're ready to return
					if (i_ent != matching_by_summary_code.second) return i_ent->second;
					// otherwise make a new equiv class and index it by summary code
					new_rep.root().equivalence_classes.emplace_back(
						set<Dwarf_Off>());
					auto& new_set = new_rep.root().equivalence_classes.back();
					auto iter = std::prev(new_rep.root().equivalence_classes.end());
					new_rep.root().equivalence_classes_by_summary_code.insert(
						make_pair(
							summary_code,
							iter
						)
					);
					debug_expensive(5, << "Created fresh equivalence class at " << &new_set
						<< " to hold " << new_rep << endl);
					return iter;
				}; // end find_or_create_eq_class lambda

				if (UNEQUAL != ret) // cache positive result
				{
					/* If either one has an equivalence class already, we simply
					 * add the other to it *and* associate the first with that eq. */
#define ASSERT_EQ_CLASS_ABSENT(oi) \
   do { if ((oi)) { \
           debug(0) << "Erk: " << &(**(oi)) << endl; \
           debug(0) << "In type_die::equal of " << self << " and " << t << endl; \
        } \
        assert(!(oi)); \
   } while (0)
					if (eq_class_of_t)
					{
						ASSERT_EQ_CLASS_ABSENT(eq_class_of_self);
						debug_expensive(5, << "Adding to equivalence class at " << &**eq_class_of_t
							<< ": " << self << endl);
						(*eq_class_of_t)->insert(self.offset_here());
						self.root().equivalence_class_of.insert(make_pair(self.offset_here(), *eq_class_of_t));
					}
					else if (eq_class_of_self)
					{
						ASSERT_EQ_CLASS_ABSENT(eq_class_of_t);
						debug(5) << "Adding to equivalence class at " << &**eq_class_of_self
							<< ": " << t << endl;
						(*eq_class_of_self)->insert(t.offset_here());
						t.root().equivalence_class_of.insert(make_pair(t.offset_here(), *eq_class_of_self));
					}
					else
					{
						/* Don't rush into creating a new class. The equivalence
						 * class we think we want to create may already exist, just not
						 * yet having had these DIEs be compared against its members
						 * i.e. the equality has not yet been discovered.
						 *
						 * What to do? Test against all existing classes? Seems slow.
						 * Just don't cache anything? This omits to cache a positive result.
						 * Start a new class but allow for lazily coalescing later?
						 * This omits to cache a negative result, because we can't
						 * use inequality of equivalence classes as a negative
						 * result.
						 *
						 * Let's use summary codes to narrow down the possible classes
						 * to test against. */
						ASSERT_EQ_CLASS_ABSENT(eq_class_of_t);
						ASSERT_EQ_CLASS_ABSENT(eq_class_of_self);
						auto new_set_iter = find_or_create_eq_class(self, self.as_a<type_die>()->summary_code());
						set<Dwarf_Off>& new_set = *new_set_iter;
						new_set.insert(t.offset_here());
						new_set.insert(self.offset_here());
						debug_expensive(5, << "Found-or-created equivalence class at " << &new_set
							<< " holding " << self << " and " << t << endl);
						// create one equiv class and add both to it
						self.root().equivalence_class_of.insert(make_pair(self.offset_here(), new_set_iter));
						self.root().equivalence_class_of.insert(make_pair(t.offset_here(), new_set_iter));
					}
					debug_expensive(5, << "Installed positive result in equality cache after comparison of "
						<< summary() << " with " << t.summary()
						<< "; cache size is now " << self.root().equivalence_class_of.size() << endl);
					opt<bool> cached1 = check_cached_result(self, t);
					assert(cached1);
					assert(*cached1);
					opt<bool> cached2 = check_cached_result(t, self);
					assert(cached2);
					assert(*cached2);
				}
				else // negative result is cached simply by ensuring distinct equiv classes exist
				     // We need to do this eagerly because our '<' impl (compare_with_type_equality)
				     // relies on a total (but arbitrary) ordering existin between equiv classes
				{
					auto maybe_fail_on = [&](opt<list<set<Dwarf_Off>>::iterator> should_not_equal_other) {
						if (!should_not_equal_other) return;
						if (should_not_equal_other == eq_class_of_self
						 && should_not_equal_other == eq_class_of_t)
						{
							std::cerr << "Internal error: caching negative equality comparison of "
								 << self.summary() << " and " << t.summary()
								 << " (reason: " << reason << ")"
								 << " but both are already in same equivalence class: "
								 << print_equivalence_class(*should_not_equal_other)
								 << endl;
							if (reason == "self may_equal")
							{
								// let's re-call may_equal after a delay
								sleep(10);
								this->may_equal(t, assuming_equal, reason);
							}
							assert(false);
						}
					};
					if (!eq_class_of_t)
					{
						auto new_set_iter = find_or_create_eq_class(t, t.as_a<type_die>()->summary_code());
						set<Dwarf_Off>& new_set = *new_set_iter;
						new_set.insert(t.offset_here());
						self.root().equivalence_class_of.insert(make_pair(t.offset_here(), new_set_iter));
					} else maybe_fail_on(eq_class_of_t); //assert(!eq_class_of_self || eq_class_of_t != eq_class_of_self);
					if (!eq_class_of_self)
					{
						auto new_set_iter = find_or_create_eq_class(self, self.as_a<type_die>()->summary_code());
						set<Dwarf_Off>& new_set = *new_set_iter;
						new_set.insert(self.offset_here());
						self.root().equivalence_class_of.insert(make_pair(self.offset_here(), new_set_iter));
					} else maybe_fail_on(eq_class_of_self); //assert(!eq_class_of_t || eq_class_of_t != eq_class_of_self);
					debug_expensive(5, << "Installed negative result in equality cache after comparison of "
						<< summary() << " with " << t.summary()
						<< " (reason " << reason << ")"
						<< "; cache size is now " << self.root().equivalence_class_of.size() << endl);
					opt<bool> cached1 = check_cached_result(self, t);
					assert(cached1);
					assert(!*cached1);
					opt<bool> cached2 = check_cached_result(t, self);
					assert(cached2);
					assert(!*cached2);
				}
			}
			else // not cacheable
			{
				debug_expensive(5, << "Not caching in equality result (different roots!)"
					" after comparison of " << summary()
					<< " with " << t.summary()
					<< " returned " << std::boolalpha << ret << endl);
			}
#endif /* DISABLE_TYPE_EQUALITY_CACHE */
		return_after_cache:
			if (reason != "" && reason_for_caller) *reason_for_caller = reason;
			return ret;
		}
		bool type_die::operator==(const dwarf::core::type_die& t) const
		{ return equal(get_root().find(t.get_offset()), {}); }
/* from base_type_die */
		type_die::equal_result_t base_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason /* = opt<string&>() */) const
		{
			if (!t) { if (reason) *reason = "one is void"; return UNEQUAL; }
			if (get_tag() != t.tag_here()) { if (reason) *reason = "one is not base"; return UNEQUAL; }
			if (get_name() != t.name_here()) { if (reason) *reason = "one has no name"; return UNEQUAL; }
			auto other_base_t = t.as_a<base_type_die>();
			bool encoding_equal = get_encoding() == other_base_t->get_encoding();
			if (!encoding_equal) { if (reason) *reason = "encodings differ"; return UNEQUAL; }
			bool byte_size_equal = get_byte_size() == other_base_t->get_byte_size();
			if (!byte_size_equal) { if (reason) *reason = "byte sizes differ"; return UNEQUAL; }
			
			bool bit_size_equal =
			// presence equal
				(!get_bit_size() == !other_base_t->get_bit_size())
			// and if we have one, it's equal
			&& (!get_bit_size() || *get_bit_size() == *other_base_t->get_bit_size());
			if (!bit_size_equal) { if (reason) *reason = "bit sizes differ"; return UNEQUAL; }
			
			bool bit_offset_equal = 
			// presence equal
				(!get_bit_offset() == !other_base_t->get_bit_offset())
			// and if we have one, it's equal
			&& (!get_bit_offset() || *get_bit_offset() == *other_base_t->get_bit_offset());
			if (!bit_offset_equal) { if (reason) *reason = "bit offsets differ"; return UNEQUAL; }
			
			return EQUAL;
		}
		opt<Dwarf_Unsigned> base_type_die::calculate_byte_size() const
		{
			/* DWARF4 allows us to have bit_size but not byte_size. */
			auto opt_byte_size = get_byte_size();
			if (opt_byte_size) return opt_byte_size;
			auto opt_bit_size = get_bit_size();
			if (opt_bit_size) return opt<Dwarf_Unsigned>(
				(*opt_bit_size % 8 == 0) ? *opt_bit_size / 8 : 1 + *opt_bit_size / 8
			);
			return opt<Dwarf_Unsigned>();
		}
		pair<Dwarf_Unsigned, Dwarf_Unsigned> base_type_die::bit_size_and_offset() const
		{
			opt<Dwarf_Unsigned> opt_bsz = get_bit_size();
			opt<Dwarf_Unsigned> opt_boff = get_bit_offset();
			opt<Dwarf_Unsigned> opt_data_boff = get_data_bit_offset();
			
			unsigned bit_size = opt_bsz ? *opt_bsz : 8 * *calculate_byte_size();
			unsigned bit_offset = opt_data_boff ? *opt_data_boff : 
				opt_boff ? *opt_boff : 0;
			
			return make_pair(bit_size, bit_offset);
		}
		bool base_type_die::is_bitfield_type() const
		{
			auto opt_byte_sz = this->calculate_byte_size();
			pair<Dwarf_Unsigned, Dwarf_Unsigned> bit_size_and_offset = this->bit_size_and_offset();
			return !(bit_size_and_offset.second == 0
				&& bit_size_and_offset.first == 8 * *opt_byte_sz);
		}
		
		string
		base_type_die::canonical_name_for(spec& spec, unsigned encoding,
			unsigned byte_size, unsigned bit_size, unsigned bit_offset)
		{
			std::ostringstream name;
			string encoding_name = spec.encoding_lookup(encoding);
			assert(encoding_name.substr(0, sizeof "DW_ATE_" - 1) == "DW_ATE_");
			switch (encoding)
			{
				case DW_ATE_signed:
					name << "int";
					break;
				case DW_ATE_unsigned: 
					name << "uint";
					break;
				default:
					name << encoding_name.substr(sizeof "DW_ATE_" - 1);
					break;
			}

			bool needs_suffix = !(bit_offset == 0 && bit_size == 8 * byte_size);
			name << "$" << bit_size;
			if (needs_suffix) name << "$" << bit_offset;
			return name.str();
		}
		
		string
		base_type_die::get_canonical_name() const
		{
			if (get_encoding()) return canonical_name_for(find_self().enclosing_cu().spec_here(),
				get_encoding(), *get_byte_size(),
				bit_size_and_offset().first, bit_size_and_offset().second);
			// FIXME: this is a liballocs-ism that shouldn't be in here -- push it to our
			// caller
			else return "__uninterpreted_byte";
		}
		std::ostream& operator<<(std::ostream& s, enum type_die::equal_result_t r)
		{
			switch (r)
			{
				case type_die::UNEQUAL: s << "unequal"; break;
				case type_die::EQUAL_BY_ASSUMPTION: s << "equal (by assumption)"; break;
				case type_die::EQUAL: s << "equal"; break;
				default: assert(false); abort();
			}
			return s;
		}
/* from ptr_to_member_type_die */
		type_die::equal_result_t ptr_to_member_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason /* = opt<string&>() */) const
		{
			if (!t) return UNEQUAL;
			
			debug(2) << "Testing ptr_to_member_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			auto other_t = t->get_concrete_type();
			auto other_t_as_pmem = other_t.as_a<ptr_to_member_type_die>();
			return (other_t.tag_here() == DW_TAG_ptr_to_member_type
				&& other_t_as_pmem->get_type()->equal(get_type(), assuming_equal, reason)) ? EQUAL : UNEQUAL;
		}
/* from array_type_die */
		iterator_df<type_die> array_type_die::get_concrete_type() const
		{
			return find_self();
		}
		type_die::equal_result_t array_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason /* = opt<string&>() */) const
		{
			if (!t) return UNEQUAL;
			
			debug(2) << "Testing array_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return UNEQUAL;
			if (get_name() != t.name_here()) return UNEQUAL;

			// our subrange type(s) should be equal, if we have them
			auto our_subr_children = children().subseq_of<subrange_type_die>();
			auto their_subr_children = t->children().subseq_of<subrange_type_die>();
			auto i_theirs = their_subr_children.first;
			for (auto i_subr = our_subr_children.first; i_subr != our_subr_children.second;
				++i_subr, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_subr_children.second) return UNEQUAL;
				
				bool types_equal = 
				// presence equal
					(!i_subr->get_type() == !i_subr->get_type())
				// and if we have one, it's equal to theirs
				&& (!i_subr->get_type() || i_subr->get_type()->equal(i_theirs->get_type(), assuming_equal, reason));
				
				if (!types_equal) return UNEQUAL;
			}
			// if they had more, we're unequal
			if (i_theirs != their_subr_children.second) return UNEQUAL;
			
			// our element type(s) should be equal
			bool types_equal = get_type()->equal(t.as_a<array_type_die>()->get_type(), assuming_equal, reason);
			if (!types_equal) return UNEQUAL;
			
			return EQUAL;
		}
/* from address_holding_type_die: */
		pair<unsigned, iterator_df<type_die> > address_holding_type_die::get_ultimate_reached_type() const
		{
			unsigned indir_level = 0;
			iterator_df<type_die> pointee_t = find_self();
			while (pointee_t.is_a<address_holding_type_die>())
			{
				++indir_level;
				pointee_t = pointee_t.as_a<address_holding_type_die>()->get_type();
				if (pointee_t) pointee_t = pointee_t->get_concrete_type();
			}
			return make_pair(indir_level, pointee_t);
		}
		pair<unsigned, iterator_df<type_die> > address_holding_type_die::find_ultimate_reached_type() const
		{
			unsigned indir_level = 0;
			iterator_df<type_die> pointee_t = find_self();
			while (pointee_t.is_a<address_holding_type_die>())
			{
				++indir_level;
				/* Using "find_type" will turn a declaration (i.e. with DW_AT_declaration)
				 * into a definition if it can, and look for a "type" attribute there.
				 * But that's not what we want! Address-holding types generally don't
				 * appear as declarations. Instead, when we get to the end....*/
				pointee_t = pointee_t.as_a<address_holding_type_die>()->get_type();
				if (pointee_t) pointee_t = pointee_t->get_concrete_type();
			}
			/* Now we do the declaration-to-definition bit. */
			if (pointee_t)
			{
				auto maybe_def = pointee_t->find_definition();
				if (maybe_def) return make_pair(indir_level, maybe_def);
				// otherwise, stick with the not-nothing pointee we've got (i.e. a specification)
			}
			return make_pair(indir_level, pointee_t);
		}

/* from string_type_die */
		type_die::equal_result_t string_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason /* = opt<string&>() */) const
		{
			if (!t) return UNEQUAL;
			
			debug(2) << "Testing string_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return UNEQUAL;
			if (get_name() != t.name_here()) return UNEQUAL;

			// our has-dynamic-lengthness should be equal
			bool dynamic_lengthness_equal = (t.as_a<string_type_die>()->get_string_length()
				== get_string_length());
			if (!dynamic_lengthness_equal) return UNEQUAL;
			// if we don't have dynamic length, any static length should be equal
			if (!get_string_length())
			{
				auto our_opt_byte_size = get_byte_size();
				auto other_opt_byte_size = t.as_a<string_type_die>()->get_byte_size();
				if (our_opt_byte_size != other_opt_byte_size) return UNEQUAL;
			}
			return EQUAL;
		}
		opt<Dwarf_Unsigned> string_type_die::fixed_length_in_bytes() const
		{
			if (this->get_string_length()) return opt<Dwarf_Unsigned>();
			return this->get_byte_size();
		}
		opt<Dwarf_Unsigned> string_type_die::calculate_byte_size() const
		{
			return this->fixed_length_in_bytes();
		}
		opt<encap::loclist> string_type_die::dynamic_length_in_bytes() const
		{
			auto opt_string_length = this->get_string_length();
			if (!opt_string_length) return opt<encap::loclist>();
			return *opt_string_length;
		}
/* from subrange_type_die */
		type_die::equal_result_t subrange_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason /* = opt<string&>() */) const
		{
			if (!t) return UNEQUAL;
			debug(2) << "Testing subrange_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			if (get_tag() != t.tag_here()) return UNEQUAL;
			if (get_name() != t.name_here()) return UNEQUAL;

			auto subr_t = t.as_a<subrange_type_die>();
			
			// our base type(s) should be equal
			bool types_equal = 
			// presence equal
				(!get_type() == !subr_t->get_type())
			// if we have one, it should equal theirs
			&& (!get_type() || get_type()->equal(subr_t->get_type(), assuming_equal, reason));
			if (!types_equal) return UNEQUAL;
			
			// our upper bound and lower bound should be equal
			bool lower_bound_equal = get_lower_bound() == subr_t->get_lower_bound();
			if (!lower_bound_equal) return UNEQUAL;
			
			bool upper_bound_equal = get_upper_bound() == subr_t->get_upper_bound();
			if (!upper_bound_equal) return UNEQUAL;
			
			bool count_equal = get_count() == subr_t->get_count();
			if (!count_equal) return UNEQUAL;
			
			return EQUAL;
		}
/* from enumeration_type_die */
		type_die::equal_result_t enumeration_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason /* = opt<string&>() */) const
		{
			if (!t) return UNEQUAL;
			debug(2) << "Testing enumeration_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return UNEQUAL;
			
			if (get_name() != t.name_here()) return UNEQUAL;
		
			auto enum_t = t.as_a<enumeration_type_die>();
			
			// our base type(s) should be equal
			bool types_equal = 
			// presence equal
				(!get_type() == !enum_t->get_type())
			// if we have one, it should equal theirs
			&& (!get_type() || get_type()->equal(enum_t->get_type(), assuming_equal, reason));
			if (!types_equal) return UNEQUAL;

			/* We need like-named, like-valued enumerators. */
			auto our_enumerator_children = children().subseq_of<enumerator_die>();
			auto their_enumerator_children = t->children().subseq_of<enumerator_die>();
			auto i_theirs = their_enumerator_children.first;
			for (auto i_memb = our_enumerator_children.first; i_memb != our_enumerator_children.second;
				++i_memb, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_enumerator_children.second) return UNEQUAL;

				if (i_memb->get_name() != i_theirs->get_name()) return UNEQUAL;
				
				if (i_memb->get_const_value() != i_theirs->get_const_value()) return UNEQUAL;
			}
			if (i_theirs != their_enumerator_children.second) return UNEQUAL;
			
			return EQUAL;
		}
/* from qualified_type_die */
		iterator_df<type_die> qualified_type_die::get_unqualified_type() const
		{
			// for qualified types, our unqualified self is our get_type, recursively unqualified
			opt<iterator_df<type_die> > opt_next_type = get_type();
			if (!opt_next_type) return iterator_base::END; 
			if (!opt_next_type.is_a<qualified_type_die>()) return opt_next_type;
			else return iterator_df<qualified_type_die>(std::move(opt_next_type))
				->get_unqualified_type();
		} 
/* from spec::type_chain_die */
		opt<Dwarf_Unsigned> type_chain_die::calculate_byte_size() const
		{
			// Size of a type_chain is always the size of its concrete type
			// which is *not* to be confused with its pointed-to type!
			auto next_type = get_concrete_type();
			if (get_offset() == next_type.offset_here())
			{
				assert(false); // we're too generic to know our byte size; should have hit a different overload
			}
			else if (next_type != iterator_base::END)
			{
				auto to_return = next_type->calculate_byte_size();
				if (!to_return)
				{
					debug(2) << "Type chain concrete type " << *get_concrete_type()
						<< " returned no byte size" << endl;
				}
				return to_return;
			}
			else
			{
				debug(2) << "Type with no concrete type: " << *this << endl;
				return opt<Dwarf_Unsigned>();
			}
		}
/* from typedef_die */
		type_die::equal_result_t typedef_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason /* = opt<string&>() */) const
		{
			debug(2) << "Testing typedef_die::may_equal() on " << *get_name() << endl;
			/* This is like type_chain_die but also the names must match. Check that first. */
			if (!(t.is_a<typedef_die>()
			 && t.as_a<typedef_die>().name_here()
			 && *t.as_a<typedef_die>().name_here() == *this->get_name()))
			{ if (reason) *reason = "names differ"; return UNEQUAL; }
			// if we got here, it's down to the usual type chaining
			return this->type_chain_die::may_equal(t, assuming_equal, reason);
		}
		type_die::equal_result_t type_chain_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason_for_caller /* = opt<string&>() */) const
		{
			debug(2) << "Testing type_chain_die::may_equal() (default case)" << endl;
			equal_result_t ret;
			equal_result_t sub_equal;
			string reason;
			string sub_reason;
			if (!(get_tag() == t.tag_here())) { ret = UNEQUAL; reason = "tag mismatch"; goto ret; }
			if (!get_type() && !t.as_a<type_chain_die>()->get_type()) { ret = EQUAL; goto ret; }
			if (!get_type() && t.as_a<type_chain_die>()->get_type())
			{ ret = UNEQUAL; reason = "exactly one is chaining void (1)"; goto ret; }
			if (!!get_type() && !t.as_a<type_chain_die>()->get_type())
			{ ret = UNEQUAL; reason = "exactly one is chaining void (2)"; goto ret; }
			sub_equal = get_type()->equal(t.as_a<type_chain_die>()->get_type(), assuming_equal, sub_reason);
			if (!sub_equal)
			{
				ret = sub_equal;
				std::ostringstream s; s << "sub-equality of "
					<< get_type().summary() << " and "
					<< t.as_a<type_chain_die>()->get_type().summary()
					<< ", reason: " << sub_reason;
				reason = s.str();
				goto ret;
			}
			ret = EQUAL;
		ret:
			debug(2) << "type_chain_die::may_equal() result was " << ret
				<< " between " << this->summary() << " and " << t.summary();
			if (!ret) debug(2) << ", reason: " << reason;
			if (reason != "" && reason_for_caller) *reason_for_caller = reason;
			debug(2) << endl;
			return ret;
		}
		iterator_df<type_die> type_chain_die::get_concrete_type() const
		{
			// pointer and reference *must* override us -- they do not follow chain
			assert(get_tag() != DW_TAG_pointer_type
				&& get_tag() != DW_TAG_reference_type
				&& get_tag() != DW_TAG_rvalue_reference_type
				&& get_tag() != DW_TAG_array_type);
			
			root_die& r = get_root(); 
			auto opt_next_type = get_type();
			if (!opt_next_type) return iterator_base::END; // a.k.a. None
			if (!get_spec(r).tag_is_type(opt_next_type.tag_here()))
			{
				debug(2) << "Warning: following type chain found non-type " << opt_next_type << endl;
				return find_self();
			} 
			else return opt_next_type->get_concrete_type();
		}
/* from spec::address_holding_type_die */  
		iterator_df<type_die> address_holding_type_die::get_concrete_type() const 
		{
			return find_self();
		}
		opt<Dwarf_Unsigned> address_holding_type_die::calculate_byte_size() const 
		{
			root_die& r = get_root();
			auto opt_size = get_byte_size();
			if (opt_size) return opt_size;
			else return r.cu_pos(get_enclosing_cu_offset())->get_address_size();
		}
/* from spec::array_type_die */
		opt<Dwarf_Unsigned> array_type_die::element_count() const
		{
			auto all_counts = this->dimension_element_counts();
			opt<Dwarf_Unsigned> opt_total_count;
			
			for (auto i_count = all_counts.begin(); i_count != all_counts.end(); ++i_count)
			{
				if (*i_count) 
				{
					opt_total_count = opt_total_count ? 
						opt<Dwarf_Unsigned>(*opt_total_count * (**i_count))
						: opt<Dwarf_Unsigned>(*i_count);
				}
				else
				{
					/* We have a subrange with no bounds. So we have no overall count. */
					opt_total_count = opt<Dwarf_Unsigned>();
					break;
				}
			}
			
			return opt_total_count;
		}
		type_die::equal_result_t unspecified_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason /* = opt<string&>() */) const
		{
			if (!t) return UNEQUAL;
			if (t->get_tag() == get_tag()) return EQUAL;
			return UNEQUAL;
		}

		vector< opt<Dwarf_Unsigned> > array_type_die::dimension_element_counts() const
		{
			auto element_type = get_type();
			assert(element_type != iterator_base::END);
			vector< opt<Dwarf_Unsigned> > all_counts;

			root_die& r = get_root();
			auto self = find_self();
			assert(self != iterator_base::END);

			auto enclosing_cu = r.cu_pos(get_enclosing_cu_offset());
			auto opt_implicit_lower_bound = enclosing_cu->implicit_array_base();
			
			auto subrs = self.children_here().subseq_of<subrange_type_die>();
			for (auto i_subr = std::move(subrs.first); i_subr != subrs.second; ++i_subr)
			{
				/* The subrange might come with a "count" or upper/lower bounds. */
				auto opt_this_subr_count = i_subr->get_count();
				if (!opt_this_subr_count)
				{
					auto opt_lower_bound = i_subr->get_lower_bound();
					if (!opt_lower_bound && !opt_implicit_lower_bound)
					{
						/* do nothing -- we have no count */
					}
					else
					{
						Dwarf_Unsigned lower_bound;
						if (opt_lower_bound) lower_bound = *opt_lower_bound;
						else if (opt_implicit_lower_bound) lower_bound = *opt_implicit_lower_bound;
						else assert(0);
						
						opt<Dwarf_Unsigned> opt_upper_bound = i_subr->get_upper_bound();
						if (opt_upper_bound) 
						{
							Dwarf_Unsigned upper_bound = *opt_upper_bound;
							opt_this_subr_count = opt<Dwarf_Unsigned>(upper_bound - lower_bound + 1);
						}
						else
						{
							/* again, do nothing  */
						}
					}
				}
				
				all_counts.push_back(opt_this_subr_count);
			}
			return all_counts;
		}

		opt<Dwarf_Unsigned> array_type_die::calculate_byte_size() const
		{
			auto element_type = get_type();
			assert(element_type != iterator_base::END);
			opt<Dwarf_Unsigned> count = element_count();
			opt<Dwarf_Unsigned> calculated_byte_size = element_type->calculate_byte_size();
			if (count && calculated_byte_size) return *count * *calculated_byte_size;
			else return opt<Dwarf_Unsigned>();
		}
		
		iterator_df<type_die> array_type_die::ultimate_element_type() const
		{
			iterator_df<type_die> cur = get_concrete_type();
			while (cur != iterator_base::END
				 && cur.is_a<array_type_die>())
			{
				cur = cur.as_a<array_type_die>()->get_type();
				if (cur != iterator_base::END) cur = cur->get_concrete_type();
			}
			return cur;
		}
		
		opt<Dwarf_Unsigned> array_type_die::ultimate_element_count() const 
		{
			Dwarf_Unsigned count = 1;
			iterator_df<type_die> cur = get_concrete_type();
			while (cur != iterator_base::END
				 && cur.is_a<array_type_die>())
			{
				auto opt_count = cur.as_a<array_type_die>()->element_count();
				if (!opt_count) return opt<Dwarf_Unsigned>();
				else 
				{
					count *= *opt_count;
					cur = cur.as_a<type_chain_die>()->get_type();
					if (cur != iterator_base::END) cur = cur->get_concrete_type();
				}
			}
			return opt<Dwarf_Unsigned>(count);
		}
		
		
/* from spec::structure_type_die */
		opt<Dwarf_Unsigned> structure_type_die::calculate_byte_size() const
		{
			/* The overall size of a struct is language- and implementation-dependent.
			 * Even if the DWARF tells us a size, it may not be accurate according to
			 * the rules of the language. For example, GNU C or C99 variable-length arrays
			 * tend to come out with a specific size in DWARF, even though it's wrong.
			 * We need to special-case this, and recursively: if our last member does
			 * not have a definite size, then we ignore whatever size we have.
			 * This needs to be recursive, to handle the case where the last member is
			 * a struct. PROBLEM: what about other languages that might allow data-
			 * dependent array lengths in the middle of a structure? We should probably
			 * check all members.
			 */
			auto default_answer = this->type_die::calculate_byte_size();
			auto members = this->children().subseq_of<member_die>();
			// FIXME: guard on language? check only last member for C?
			for (auto i_memb = members.first; i_memb != members.second; ++i_memb)
			{
				auto t = i_memb->get_type();
				auto opt_byte_size = t->calculate_byte_size();
				if (!opt_byte_size)
				{
					// keep on a separate line for breakpointability
					return opt<Dwarf_Unsigned>();
				}
			}
			return default_answer; // just does get_byte_size()
		}
/* from spec::with_data_members_die */
		type_die::equal_result_t with_data_members_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason_for_caller /* = opt<string&>() */) const
		{
			string reason;
			equal_result_t ret = EQUAL;
			if (!t) { reason = "t is void"; ret = UNEQUAL; goto out; }
			debug(2) << "Testing with_data_members_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) { reason = "tags"; ret = UNEQUAL; goto out; }
			
			if (get_name() != t.name_here()) { reason = "name"; ret = UNEQUAL; goto out; }
			if (!get_name() && !t.name_here())
			{
				/* Two anonymous structs with the same types of fields
				 * should not be deemed the same type. Use source context.
				 * FIXME: should really be using containment, not raw file coords.
				 * e.g. if two different elf.h files are included by different
				 * CUs, the file/line/column coords of their anonymous Elf_* structures
				 * should not matter. Maybe keep the test below if parents are CUs,
				 * otherwise need a binary test for "same source context". */
#if 0
#define MUST_EQUAL_IF_PRESENT(frag) \
				if (get_ ## frag () || t->get_ ## frag()) \
				{ if (!get_ ## frag() || !t->get_ ## frag() || \
				  *get_##frag() != *t->get_ ##frag()) {\
				     reason = #frag ; ret = UNEQUAL; goto out; \
				  }\
			    }
				MUST_EQUAL_IF_PRESENT(decl_file);
				MUST_EQUAL_IF_PRESENT(decl_line);
				MUST_EQUAL_IF_PRESENT(decl_column);
#endif
				/* New simpler solution: if associated name and tag are equal,
				 * we *might* be equal (subject to per-member equality),
				 * otherwise we're unequal. This allows structs of the same
				 * *associated* name and contents to be equal, much like
				 * structs of the same name and actual contents are equal.
				 * See liballocs GitHub issue 71 for a rationale. */
				auto our_ass_name = find_associated_name();
				auto their_ass_name = t->find_associated_name();
				if ((!our_ass_name && their_ass_name)
				  ||( our_ass_name && !their_ass_name))
				{ reason = "existence of associated names"; ret = UNEQUAL; goto out; }

				if (our_ass_name && their_ass_name &&
					(*our_ass_name != *their_ass_name))
				{ reason = "associated names unequal"; ret = UNEQUAL; goto out; }
				// no tag test here because we know the tags are equal -- tested earlier
			}
			
			/* We need like-named, like-located members. 
			 * GAH. We really need to canonicalise location lists to do this properly. 
			 * That sounds difficult (impossible in general). Nevertheless for most
			 * structs, it's likely to be that they are identical. */
			
			/* Another GAH: recursive structures. What to do about them? */
			{ /* HACK to allow goto */
			auto our_member_children = children().subseq_of<member_die>();
			auto their_member_children = t->children().subseq_of<member_die>();
			auto i_theirs = their_member_children.first;
			unsigned i = 0;
			for (auto i_memb = our_member_children.first; i_memb != our_member_children.second;
				++i_memb, ++i_theirs, ++i)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_member_children.second)
				{ reason = "they have fewer"; ret = UNEQUAL; goto exit_member_loop; }
				
				auto this_test_pair = make_pair(
					find_self().as_a<type_die>(),
					t
				);
				/* DW_TAG_member DIEs may have bitfield attributes separate from
				 * the base type DIE, so ensure these are accounted for. */
				auto our_type = i_memb->/*get_type*/find_or_create_type_handling_bitfields();
				auto their_type = i_theirs->/*get_type*/find_or_create_type_handling_bitfields();
				assert(!!our_type); // member type cannot be void
				assert(!!their_type);
				equal_result_t sub_result = UNEQUAL;
				/* RECURSION: here we may get into an infinite loop 
				 * if equality of get_type() depends on our own equality. 
				 * So we use the 'assuming_equal' set: add the two test
				 * with_data_members DIEs to it. */
				auto recursive_test_set = assuming_equal;
				recursive_test_set.insert(this_test_pair);
				string sub_reason;
				sub_result = our_type->equal(their_type, recursive_test_set, sub_reason);
				if (!sub_result)
				{
					std::ostringstream s;
					s << "member type unequal (" << i << ", "
						<< our_type.summary()
						<< " and " << their_type.summary() << "), reason: " << sub_reason;
					reason = s.str();
					ret = UNEQUAL;
					goto exit_member_loop;
				}
				/* How does a member result affect whether we are EQUAL or EQUAL_BY_ASSUMPTION?
				 * If the only assumption is the one we added,
				 * or if no assumption was used by the recursive test,
				 * EQUAL can stay EQUAL.
				 * If the recursive test relied on an assumption that wasn't
				 * ours, downgrade to EQUAL_BY_ASSUMPTION. */
				if (sub_result == EQUAL_BY_ASSUMPTION
					&&  assuming_equal.size() != 0)
				{
					ret = EQUAL_BY_ASSUMPTION;
				}
				// otherwise it stays where it was -- either EQUAL or,
				// if an earlier member downgraded it, EQUAL_BY_ASSUMPTION
				auto loc1 = i_memb->get_data_member_location();
				auto loc2 = i_theirs->get_data_member_location();
				bool locations_equal = 
					loc1 == loc2; /* FIXME: this should allow for different exprs that always compute the same result (thoughts Alan?) */
				if (!locations_equal)
				{ reason = "member loc unequal"; ret = UNEQUAL; goto exit_member_loop; }
				
				// FIXME: test names too? not for now
			}
			// if they had more, we're unequal
			if (i_theirs != their_member_children.second) { reason = "they have more"; ret = UNEQUAL; }
		exit_member_loop: ;
			} // end hacky extra scope
		out:
			if (!ret)
			{
				debug_expensive(5, << "with_data_members types " << this->summary() << " and " << t.summary()
					<< " unequal for reason: " << reason << endl);
				if (reason != "" && reason_for_caller) *reason_for_caller = reason;
			}
			return ret;
		}
		iterator_base with_data_members_die::find_definition() const
		{
			root_die& r = get_root();
			if (!get_declaration() || !*get_declaration()) 
			{
				/* we are a definition already */
				return find_self();
			}
			
			// if we have a cached result, use that
			if (this->maybe_cached_definition) return *maybe_cached_definition;
			
			debug(2) << "Looking for definition of declaration " << summary() << endl;
			
			// if we don't have a name, we have no way to proceed
			auto opt_name = get_name(/*r*/);
			if (!opt_name) goto return_no_result;
			else
			{
				string my_name = *opt_name;

				/* Otherwise, we search forwards from our position, for siblings
				 * that have the same name but no "declaration" attribute. */
				auto iter = find_self();

				/* Are we a CU-toplevel DIE? We only handle this case at the moment. 
				   PROBLEM:
				   
				   declared C++ classes like like this:
				 <2><1d8d>: Abbrev Number: 56 (DW_TAG_class_type)
				    <1d8e>   DW_AT_name        : (indirect string, offset: 0x17b4): reverse_iterator
				<__gnu_cxx::__normal_iterator<char const*, std::basic_string<char, std::char_traits<
				char>, std::allocator<char> > > >       
				    <1d92>   DW_AT_declaration : 1      

				   The definition of the class has name "reverse_iterator"!
				   The other stuff is encoded in the DW_TAG_template_type_parameter members.
				   These act a lot like typedefs, so we should make them type_chains.
				   
				
				*/
				if (iter.depth() != 2) goto return_no_result;

				iterator_sibs<with_data_members_die> i_sib = iter; ++i_sib;/* i.e. don't check ourselves */; 
				for (; i_sib != iterator_base::END; ++i_sib)
				{
					opt<bool> opt_decl_flag;
					if (i_sib.is_a<with_data_members_die>()
					 && i_sib->get_name() && *i_sib->get_name() == my_name
						&& (opt_decl_flag = i_sib->get_declaration(), 
							!opt_decl_flag || !*opt_decl_flag))
					{
						debug(2) << "Found definition " << i_sib->summary() << endl;
						this->maybe_cached_definition = i_sib;
						return i_sib;
					}
				}
			}
		return_no_result:
			debug(2) << "Failed to find definition of declaration " << summary() << endl;
			this->maybe_cached_definition = iterator_base::END;
			return iterator_base::END;
		}

		bool variable_die::has_static_storage() const
		{
			// don't bother testing whether we have an enclosing subprogram -- too expensive
			//if (nonconst_this->nearest_enclosing(DW_TAG_subprogram))
			//{
				// we're either a local or a static -- skip if local
				root_die& r = get_root();
				auto attrs = find_all_attrs();
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
					
					// if our loclist is empty, we're probably an optimised-out local,
					// so return false
					if (loclist.begin() == loclist.end()) return false;
					
					for (auto i_loc_expr = loclist.begin(); 
						i_loc_expr != loclist.end(); 
						++i_loc_expr)
					{
						for (auto i_instr = i_loc_expr->begin(); 
							i_instr != i_loc_expr->end();
							++i_instr)
						{
							if (i_instr->lr_atom == DW_OP_stack_value)
							{
								/* This means it has *no storage* in the range of the current 
								 * loc_expr, so we guess that it has no storage or stack storage
								 * for its whole lifetime, and return false. */
								return false;
							}

							if (get_spec(r).op_reads_register(i_instr->lr_atom)) return false;
						}
					}
				}
			//}
			return true;
		}
		
/* from spec::data_member_die */ 
		opt<Dwarf_Unsigned> 
		data_member_die::byte_offset_in_enclosing_type(
			bool assume_packed_if_no_location /* = false */) const
		{
			// if we're a declaration, that's bad
			if (get_declaration() && *get_declaration())
			{
				return opt<Dwarf_Unsigned>();
			}
			
			root_die& r = get_root();
			iterator_df<data_member_die> self = find_self();
			auto parent = r.parent(self);
			auto enclosing_type_die = parent.as_a<core::with_data_members_die>();
			if (!enclosing_type_die) return opt<Dwarf_Unsigned>();
			
			opt<encap::loclist> data_member_location 
			 = self.is_a<data_member_die>() ? self.as_a<data_member_die>()->get_data_member_location()
			 : opt<encap::loclist>();
			if (!data_member_location)
			{
				// if we don't have a location for this field,
				// we tolerate it iff it's the first non-declaration one in a struct/class
				// OR contained in a union
				// OR if the caller passed assume_packed_if_no_location
				// HACK: support class types (and others) here
				auto parent_members = parent.children().subseq_of<data_member_die>();
				assert(parent_members.first != parent_members.second);
				auto real_first_memb = parent_members.first;
				// skip decls
				while (
					(real_first_memb->get_declaration() && *real_first_memb->get_declaration())
				)
				{
					++real_first_memb;
					// at the latest, we should hit ourselves
					assert(real_first_memb != iterator_base::END);
				}
				
				// if we are the first member of a struct, or any member of a union, we're okay
				if (self == real_first_memb
				 || enclosing_type_die.is_a<union_type_die>())
				{
					return opt<Dwarf_Unsigned>(0U);
				}
				
				/* Otherwise we might still be okay. Needed e.g. for synthetic DIEs from dwarfidl */
				if (assume_packed_if_no_location)
				{
					auto a_previous_memb = real_first_memb;
					assert(a_previous_memb);
					// if there is one member or more before us... linear search
					if (a_previous_memb != self)
					{
						do
						{
							auto next_memb = a_previous_memb;
							// advance to the next non-decl member or inheritance DIE 
							do
							{
								do
								{
									++next_memb;
								} while (next_memb->get_declaration() && *next_memb->get_declaration());
							} while (next_memb && next_memb != self);
							// break if we hit ourselves
							if (next_memb == self) break;
							a_previous_memb = std::move(next_memb);
						} while (true);

						if (a_previous_memb) // it's the 
						{
							auto& the_previous_memb = a_previous_memb;
							auto prev_memb_t = the_previous_memb->get_type();
							if (prev_memb_t)
							{
								auto opt_prev_byte_size = prev_memb_t->calculate_byte_size();
								if (opt_prev_byte_size)
								{
									/* Do we have an offset for the previous member? */
									auto opt_prev_member_offset = the_previous_memb->byte_offset_in_enclosing_type(
										true);
									// FIXME: quadratic

									/* If that succeeded, we can go ahead. */
									if (opt_prev_member_offset)
									{
										return opt<Dwarf_Unsigned>(*opt_prev_member_offset + *opt_prev_byte_size);
									}
								}
							}
						}
					}
				}
				
				// if we got here, we really can't figure it out
				debug() << "Warning: encountered DWARF member lacking a location: "
					<< self << std::endl;
				return opt<Dwarf_Unsigned>();
			}
			else if (data_member_location->size() != 1)
			{
				debug() << "Bad location: " << *data_member_location << std::endl;
				goto location_not_understood;
			}
			else
			{
				/* If we have an indirection here, we will get some memory access 
				 * happening, and our evaluator should bail out. Q: how? A. DW_OP_deref
				 * has no implementation, because we don't pass a memory. 
				 *
				 * FIXME: when we add support for memory operations, the error we
				 * get will be different, and we need to update the catch case. */
				try {
					return expr::evaluator(
						data_member_location->at(0), 
						self.enclosing_cu().spec_here(), 
						{ 0 }).tos();
				} 
				catch (expr::Not_supported)
				{
					goto location_not_understood;
				}
			}
		location_not_understood:
				// error
				debug() << "Warning: encountered DWARF member with location I didn't understand: "
					<< self << std::endl;
				return opt<Dwarf_Unsigned>();
		}
		
/* from spec::with_dynamic_location_die */
		opt<string> with_static_location_die::get_linkage_name() const
		{
			/* What is the name, if any, of a symbol that maps to this DIE?
			 * Note that this is ill-posed: in general there may be more than one.
			 * There may be none, if the definition is not exported.
			 * However, if there is one or more, there should be one that is 'primary'
			 * in the sense that:
			 * - it is the default expected name on this platform, for this language/impl
			 * - OR it is an exception that is recorded via DW_AT_linkage_name or similar (..._MIPS_...)
			 */
			encap::attribute_map attrs = find_all_attrs();
			auto found_linkage_name = attrs.find(DW_AT_linkage_name);
			if (found_linkage_name != attrs.end()) return found_linkage_name->second.get_string();
			auto found_mips_linkage_name = attrs.find(DW_AT_MIPS_linkage_name);
			if (found_mips_linkage_name != attrs.end()) return found_mips_linkage_name->second.get_string();
			auto found_visibility = attrs.find(DW_AT_visibility);
			/* Do we have a name, and are we exported (or not not exported)?
			 * If so, get the default mangler for this CU and pass it our name i_subp.name_here() */
			if (!get_name()
				|| (found_visibility != attrs.end() && found_visibility->second.get_signed() != DW_VIS_exported))
			{
				// no name not exported, so no linkage name
				return opt<string>();
			}
			return find_self().enclosing_cu()->mangled_name_for(find_self());
		}

		// To understand the return value, see comment in dies.hpp
		boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned> 
		with_static_location_die::file_relative_intervals(
			root_die& r, 
			sym_resolver_t sym_resolve,
			void *arg /* = 0 */) const
		{
			encap::attribute_map attrs = find_all_attrs();
			
			using namespace boost::icl;
			auto& right_open = interval<Dwarf_Addr>::right_open;
			interval_map<Dwarf_Addr, Dwarf_Unsigned> retval;

			// HACK: if we're a local variable, return false. This function
			// only deals with static storage. Mostly the restriction is covered
			// by the fact that only certain tags are with_static_location_dies,
			// but both locals and globals show up with DW_TAG_variable.
			if (this->get_tag() == DW_TAG_variable &&
				!dynamic_cast<const variable_die *>(this)->has_static_storage())
				goto out;
			else
			{
				auto found_low_pc = attrs.find(DW_AT_low_pc);
				auto found_high_pc = attrs.find(DW_AT_high_pc);
				auto found_ranges = attrs.find(DW_AT_ranges);
				auto found_location = attrs.find(DW_AT_location);
				auto found_linkage_name = attrs.find(DW_AT_linkage_name); // ... be in a non-default spec

				if (found_ranges != attrs.end())
				{
					iterator_df<compile_unit_die> i_cu = r.cu_pos(d.enclosing_cu_offset_here());
					auto rangelist = i_cu->normalize_rangelist(found_ranges->second.get_rangelist());
					Dwarf_Unsigned cumulative_bytes_seen = 0;
					for (auto i_r = rangelist.begin(); i_r != rangelist.end(); ++i_r)
					{
						auto& r = *i_r;
						if (r.dwr_addr1 == r.dwr_addr2) 
						{
							// I have seen this happen...
							//assert(r.dwr_addr1 == 0);
							continue;
						}
						auto ival = interval<Dwarf_Addr>::right_open(r.dwr_addr1, r.dwr_addr2);
						//clog << "Inserting interval " << ival << endl;
						// HACK: icl doesn't like zero codomain values??
						cumulative_bytes_seen += r.dwr_addr2 - r.dwr_addr1;
						retval.insert(
							make_pair(
								ival, 
								cumulative_bytes_seen 
							)
						);
						assert(retval.find(ival) != retval.end());
						assert(r.dwr_addr2 > r.dwr_addr1);
					}
					// sanity check: assert that the first interval is included
					assert(rangelist.size() == 0 
						|| (rangelist.begin())->dwr_addr1 == (rangelist.begin())->dwr_addr2
						|| retval.find(right_open(
							(rangelist.begin())->dwr_addr1, 
							(rangelist.begin())->dwr_addr2
							)) != retval.end());
				}
				else if (found_low_pc != attrs.end() && found_high_pc != attrs.end() && found_high_pc->second.get_form() == encap::attribute_value::ADDR)
				{
					auto hipc = found_high_pc->second.get_address().addr;
					auto lopc = found_low_pc->second.get_address().addr;
					if (hipc > lopc)
					{
						retval.insert(make_pair(right_open(
							lopc, 
							hipc
						), hipc - lopc));
					} else assert(hipc == lopc);
				}
				else if (found_low_pc != attrs.end() && found_high_pc != attrs.end() && found_high_pc->second.get_form() == encap::attribute_value::UNSIGNED)
				{
					auto lopc = found_low_pc->second.get_address().addr;
					auto hipc = lopc + found_high_pc->second.get_unsigned();
					if (hipc > 0) {
						retval.insert(make_pair(right_open(
								lopc, 
								hipc
							), hipc - lopc));
					}
				}
				else if (found_location != attrs.end())
				{
					/* Location lists can be vaddr-dependent, where vaddr is the 
					 * offset of the current PC within the containing subprogram.
					 * Since we're a with_static_location_die, we *probably* don't
					 * have vaddr-dependent location. FIXME: check this is okay. */

					opt<Dwarf_Unsigned> opt_byte_size;
					auto found_byte_size = attrs.find(DW_AT_byte_size);
					if (found_byte_size != attrs.end())
					{
						opt_byte_size = found_byte_size->second.get_unsigned();
					}
					else
					{	
						/* Look for the type. "Type" means something different
						 * for a subprogram, which should be covered by the
						 * high_pc/low_pc and ranges cases, so assert that
						 * we don't have one of those. */
						assert(this->get_tag() != DW_TAG_subprogram);
						auto found_type = attrs.find(DW_AT_type);
						if (found_type == attrs.end()) goto out;
						else
						{
							iterator_df<type_die> t = r.find(found_type->second.get_ref().off);
							auto calculated_byte_size = t->calculate_byte_size();
							// VLAs won't tell us how big they are
							if (!calculated_byte_size) goto out;
							opt_byte_size = *calculated_byte_size; // assign to *another* opt
						}
					}
					assert(opt_byte_size);
					Dwarf_Unsigned byte_size = *opt_byte_size;
					if (byte_size == 0)
					{
						debug(2) << "Zero-length object: " << summary() << endl;
						goto out;
					}
					
					auto loclist = found_location->second.get_loclist();
					std::vector<std::pair<dwarf::encap::loc_expr, Dwarf_Unsigned> > expr_pieces;
					try
					{
						expr_pieces = loclist.loc_for_vaddr(0).byte_pieces();
					}
					catch (No_entry)
					{
						if (loclist.size() > 0)
						{
							debug(2) << "Vaddr-dependent static location " << *this << endl;
						}
						else debug(2) << "Static var with no location: " << *this << endl;
						//if (loclist.size() > 0)
						//{
						//	expr_pieces = loclist.begin()->byte_pieces();
						//}
						/*else*/ goto out;
					}
					
					try
					{
						Dwarf_Off current_offset_within_object = 0UL;
						for (auto i = expr_pieces.begin(); i != expr_pieces.end(); ++i)
						{
							/* Evaluate this piece. */
							Dwarf_Unsigned piece_size = i->second;
							Dwarf_Unsigned piece_start = expr::evaluator(i->first,
								this->get_spec(r), {}).tos();

							/* If we have only one piece, it means there might be no DW_OP_piece,
							 * so the size of the piece will be unreliable (possibly zero). */
							if (expr_pieces.size() == 1 && expr_pieces.begin()->second == 0)
							{
								piece_size = byte_size;
							}
							// HACK: increment early to avoid icl zero bug
							current_offset_within_object += piece_size;

							retval.insert(make_pair(
								right_open(piece_start, piece_start + piece_size),
								current_offset_within_object
							));
						}
						assert(current_offset_within_object == byte_size);
					}
					catch (expr::Not_supported)
					{
						// some opcode we don't recognise
						debug() << "Unrecognised opcode in " << *this << endl;
						goto out;
					}

				}
				else if (sym_resolve &&
					found_linkage_name != attrs.end())
				{
					std::string linkage_name = found_linkage_name->second.get_string();

					sym_binding_t binding;
					try
					{
						binding = sym_resolve(linkage_name, arg);
						retval.insert(make_pair(right_open(
								binding.file_relative_start_addr,
								binding.file_relative_start_addr + binding.size
							), binding.size)
						);
					}
					catch (lib::No_entry)
					{
						debug() << "Warning: couldn't resolve linkage name " << linkage_name
							<< " for DIE " << *this << std::endl;
					}
				}

			}
		out:
			debug(6) << "Intervals of " << this->summary() << ": " << retval << endl;
			return retval;
		}

		opt<Dwarf_Off> // returns *offset within the element*
		with_static_location_die::spans_addr(Dwarf_Addr file_relative_address,
			root_die& r, 
			sym_resolver_t sym_resolve /* = sym_resolver_t() */,
			void *arg /* = 0 */) const
		{
			// FIXME: cache intervals
			auto intervals = file_relative_intervals(r, sym_resolve, arg);
			auto found = intervals.find(file_relative_address);
			if (found != intervals.end())
			{
				Dwarf_Off interval_end_offset_from_object_start = found->second;
				assert(file_relative_address >= found->first.lower());
				Dwarf_Off addr_offset_from_interval_start
				 = file_relative_address - found->first.lower();
				assert(file_relative_address < found->first.upper());
				auto interval_size = found->first.upper() - found->first.lower();
				return interval_end_offset_from_object_start
				 - interval_size
				 + addr_offset_from_interval_start;
			}
			else return opt<Dwarf_Off>();
		}
/* helpers */		
// 		static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc, Dwarf_Addr high_pc);
// 		static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc, Dwarf_Addr high_pc)
// 		{
// 			Dwarf_Unsigned opcodes[] 
// 			= { DW_OP_constu, low_pc, 
// 				DW_OP_piece, high_pc - low_pc };
// 
// 			/* */
// 			encap::loclist list(
// 				encap::loc_expr(
// 					opcodes, 
// 					low_pc, std::numeric_limits<Dwarf_Addr>::max()
// 				)
// 			); 
//             return list;
//         }
//         static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc);
//         static encap::loclist loclist_from_pc_values(Dwarf_Addr low_pc)
//         {
//             Dwarf_Unsigned opcodes[] 
//             = { DW_OP_constu, low_pc };
// 			/* FIXME: I don't think we should be using the max Dwarf_Addr here -- 
// 			 * the libdwarf manual claims we should set them both to zero. */
//             encap::loclist list(encap::loc_expr(opcodes, 0, std::numeric_limits<Dwarf_Addr>::max())); 
//             return list;
// 			
// 			/* NOTE: libdwarf seems to give us ADDR_MAX as the high_pc 
// 			 * in the case of single-shot location expressions (i.e. not lists)
// 			 * encoded as attributes. We don't re-encode them 
// 			 * when they pass through: see lib::loclist::loclist in lib.hpp 
// 			 * and the block_as_dwarf_expr case in attr.cpp. */
//         }
		encap::loclist with_static_location_die::get_static_location() const
        {
        	auto attrs = find_all_attrs();
            if (attrs.find(DW_AT_location) != attrs.end())
            {
            	return attrs.find(DW_AT_location)->second.get_loclist();
            }
            else
        	/* This is a dieset-relative address. */
            if (attrs.find(DW_AT_low_pc) != attrs.end() 
            	&& attrs.find(DW_AT_high_pc) != attrs.end())
            {
				auto low_pc = attrs.find(DW_AT_low_pc)->second.get_address().addr;
				auto high_pc = attrs.find(DW_AT_high_pc)->second.get_address().addr;
				Dwarf_Unsigned opcodes[] 
				= { DW_OP_constu, low_pc, 
					DW_OP_piece, high_pc - low_pc };

				/* If we're a "static" object, what are the validity vaddrs of our 
				 * loclist entry? It's more than just our own vaddrs. Using the whole
				 * vaddr range seems sensible. But (FIXME) it's not very DWARF-compliant. */
				encap::loclist list(
					encap::loc_expr(
						opcodes, 
						0, std::numeric_limits<Dwarf_Addr>::max()
					)
				); 
				return list;
			}
			else
			{
				assert(attrs.find(DW_AT_low_pc) != attrs.end());
				auto low_pc = attrs.find(DW_AT_low_pc)->second.get_address().addr;
				Dwarf_Unsigned opcodes[] 
				 = { DW_OP_constu, low_pc };
				/* FIXME: I don't think we should be using the max Dwarf_Addr here -- 
				 * the libdwarf manual claims we should set them both to zero. */
				encap::loclist list(
					encap::loc_expr(
						opcodes, 
						0, std::numeric_limits<Dwarf_Addr>::max()
					)
				); 
				return list;
			}
		}
		
		/* We would define this inside spans_addr_in_frame_locals_or_args, 
		 * but we can't use forward_constructors (a "member template")
		 * inside a local class. */
		struct frame_subobject_iterator :  public iterator_bf<with_dynamic_location_die>
		{
			typedef iterator_bf<with_dynamic_location_die> super;
			void increment()
			{
				/* If our current DIE is 
				 * a with_dynamic_location_die
				 * OR
				 * is in the "interesting set"
				 * of DIEs that have no location but might contain such DIEs,
				 * we increment *with* enqueueing children.
				 * Otherwise we increment without enqueueing children.
				 */
				if (dynamic_cast<with_dynamic_location_die*>(&dereference())) { 
					super::increment();
				} else {
					switch (tag_here())
					{
						case DW_TAG_lexical_block:
							super::increment();
							break;
						default:
							super::increment_skipping_subtree();
							break;
					}					
				}
			}

			forward_constructors(super, frame_subobject_iterator)
		};
/* from spec::subprogram_die */
		opt< pair<Dwarf_Off, iterator_df<with_dynamic_location_die> > >
		subprogram_die::spans_addr_in_frame_locals_or_args( 
					Dwarf_Addr absolute_addr, 
					root_die& r, 
					Dwarf_Off dieset_relative_ip, 
					Dwarf_Signed *out_frame_base,
					expr::regs *p_regs/* = 0*/) const
		{
			typedef opt< pair<Dwarf_Off, iterator_df<with_dynamic_location_die> > >
				return_type;
			
			/* auto nonconst_this = const_cast<subprogram_die *>(this); */
			assert(this->get_frame_base());
			auto i = find_self();
			assert(i != iterator_base::END);
			
			// Calculate the vaddr which selects a loclist element
			auto frame_base_loclist = *get_frame_base();
			iterator_df<compile_unit_die> enclosing_cu
			 = r.cu_pos(i.enclosing_cu_offset_here());
			debug(2) << "Enclosing CU is " << enclosing_cu->summary() << endl;
			Dwarf_Addr low_pc = enclosing_cu->get_low_pc()->addr;
			assert(low_pc <= dieset_relative_ip);
			Dwarf_Addr vaddr = dieset_relative_ip - low_pc;
			/* Now calculate our frame base address. */
			auto frame_base_addr = expr::evaluator(
				frame_base_loclist,
				vaddr,
				get_spec(r),
				p_regs).tos();
			if (out_frame_base) *out_frame_base = frame_base_addr;
			
			auto child = r.first_child(i);
			if (child == iterator_base::END) return return_type();
			/* Now we walk children
			 * (not just immediate children, because more might hide under lexical_blocks), 
			 * looking for with_dynamic_location_dies, and 
			 * call spans_addr on what we find.
			 * We skip contained DIEs that do not contain objects located in this frame. 
			 */
			frame_subobject_iterator start_iter(child);
			debug(2) << "Exploring stack-located children of " << summary() << std::endl;
			unsigned initial_depth = start_iter.depth();
			for (auto i_bfs = start_iter;
					i_bfs.depth() >= initial_depth;
					++i_bfs)
			{
				debug(2) << "Considering whether DIE has stack location: " 
					<< i_bfs->summary() << std::endl;
				auto with_stack_loc = dynamic_cast<with_dynamic_location_die*>(&i_bfs.dereference());
				if (!with_stack_loc) continue;
				
				opt<Dwarf_Off> result = with_stack_loc->spans_addr(absolute_addr,
					frame_base_addr,
					r, 
					dieset_relative_ip,
					p_regs);
				if (result) return make_pair(
					*result, 
					iterator_df<with_dynamic_location_die>(i_bfs)
				);
			}
			return return_type();
		}
		iterator_df<type_die> subprogram_die::get_return_type() const
		{
			return get_type(); 
		}
/* from type_describing_subprogram_die */
		bool type_describing_subprogram_die::is_variadic() const
		{
			auto i = find_self();
			assert(i != iterator_base::END);
			auto children = i.children_here();
			auto unspec = children.subseq_of<unspecified_parameters_die>();
			return unspec.first != unspec.second;
		}
		type_die::equal_result_t type_describing_subprogram_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal, opt<string&> reason /* = opt<string&>() */) const
		{
			if (!t) return UNEQUAL;
			debug(2) << "Testing type_describing_subprogram_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			if (get_tag() != t.tag_here()) return UNEQUAL;
			if (get_name() != t.name_here()) return UNEQUAL;
			auto other_sub_t = t.as_a<type_describing_subprogram_die>();
			equal_result_t return_types_equal;
			// presence equal
			if (!get_return_type() && !other_sub_t->get_return_type()) return_types_equal = EQUAL;
			else if ((!get_return_type() && !!other_sub_t->get_return_type())
				|| (!!get_return_type() && !other_sub_t->get_return_type())) return_types_equal = UNEQUAL;
			else /* it depends on the type */
			{ return_types_equal = get_return_type()->equal(
				other_sub_t->get_return_type(), assuming_equal, reason); }
			if (!return_types_equal) return UNEQUAL;
			bool variadicness_equal
			 = is_variadic() == other_sub_t->is_variadic();
			if (!variadicness_equal) return UNEQUAL;
			equal_result_t ret = return_types_equal;
			/* We need like-named, like-located fps. 
			 * GAH. We really need to canonicalise location lists to do this properly. 
			 * That sounds difficult (impossible in general). */
			auto our_fps = children().subseq_of<formal_parameter_die>();
			auto their_fps = t->children().subseq_of<formal_parameter_die>();
			auto i_theirs = their_fps.first;
			for (auto i_fp = our_fps.first; i_fp != our_fps.second;
				++i_fp, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_fps.second) return UNEQUAL;
				auto their_type = i_theirs->get_type();
				auto our_type = i_fp->get_type();
				assert(!!their_type);
				assert(!!our_type);
				equal_result_t sub_result = their_type->equal(our_type, assuming_equal, reason);
				/* How does a member result affect whether we are EQUAL or EQUAL_BY_ASSUMPTION?
				 * See comment for with_data_members_die above. But this case is simpler
				 * because we never add an assumption (CHECK: can we really not get recursion
				 * via a subroutine type? Not in C but this might not hold in other languages.) */
				if (!sub_result) { ret = UNEQUAL; break; }
				if (sub_result == EQUAL_BY_ASSUMPTION) ret = EQUAL_BY_ASSUMPTION;
				// FIXME: test names too? not for now
			}
			// if they had more, we're unequal
			if (i_theirs != their_fps.second) return UNEQUAL;
			return ret;
		}
/* from subroutine_type_die */
		iterator_df<type_die> subroutine_type_die::get_return_type() const
		{
			return get_type();
		}
/* from spec::with_dynamic_location_die */
		iterator_df<program_element_die> 
		with_dynamic_location_die::get_instantiating_definition() const
		{
			/* We want to return a parent DIE describing the thing whose instances
			 * contain instances of us. 
			 *
			 * If we're a member or inheritance, it's our nearest enclosing type.
			 * If we're a variable or fp, it's our enclosing subprogram.
			 * This might be null if we're actually a static variable. */

			auto i = find_self();
			assert(i != iterator_base::END);

			// HACK: this should arguably be in overrides for formal_parameter and variable
			if (get_tag() == DW_TAG_formal_parameter
			||  get_tag() == DW_TAG_variable) 
			{
				return i.nearest_enclosing(DW_TAG_subprogram);
			}
			else // return the nearest enclosing data type
			{
				auto candidate = i.parent();
				while (candidate != iterator_base::END
					&& !dynamic_cast<type_die *>(&candidate.dereference()))
				{
					candidate = candidate.parent();
				}
				return candidate; // might be END
			}
		}

/* from spec::with_dynamic_location_die */
		opt<Dwarf_Off> 
		with_dynamic_location_die::spans_stack_addr(
					Dwarf_Addr absolute_addr,
					Dwarf_Signed frame_base_addr,
					root_die& r, 
					Dwarf_Off dieset_relative_ip,
					expr::regs *p_regs) const
		{
			auto attrs = find_all_attrs();
			if (attrs.find(DW_AT_location) == attrs.end())
			{
				debug(2) << "Warning: " << this->summary() << " has no DW_AT_location; "
					<< "assuming it does not cover any stack locations." << endl;
				return opt<Dwarf_Off>();
			}
			auto base_addr = calculate_addr_on_stack(
				frame_base_addr,
				r, 
				dieset_relative_ip,
				p_regs);
			debug(2) << "Calculated that an instance of DIE" << summary()
				<< " has base addr 0x" << std::hex << base_addr << std::dec;
			assert(attrs.find(DW_AT_type) != attrs.end());
			auto size = *(attrs.find(DW_AT_type)->second.get_refiter_is_type()->calculate_byte_size());
			debug(2) << " and size " << size
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
			
			// see note in expr.hpp
			if (!this->get_location()) return encap::loclist::NO_LOCATION; //(/*encap::loc_expr::NO_LOCATION*/);
			return absolute_loclist_to_additive_loclist(
				*this->get_location());
		
		}
		encap::loclist variable_die::get_dynamic_location() const
		{
			// see note in expr.hpp
			if (!this->get_location()) return encap::loclist::NO_LOCATION; //(/*encap::loc_expr::NO_LOCATION*/);
			
			root_die& r = get_root();
			auto i = find_self();
			assert(i != iterator_base::END);
			
			// we need an enclosing subprogram or lexical_block
			auto i_lexical = i.nearest_enclosing(DW_TAG_lexical_block);
			auto i_subprogram = i.nearest_enclosing(DW_TAG_subprogram);
			if (i_lexical == iterator_base::END && i_subprogram == iterator_base::END)
			{ throw No_entry(); }
			
			return 	absolute_loclist_to_additive_loclist(
				*this->get_location());
		}
/* from spec::with_dynamic_location_die */
		opt<Dwarf_Off> with_dynamic_location_die::spans_addr_in_object(
					Dwarf_Addr absolute_addr,
					Dwarf_Signed object_base_addr,
					root_die& r, 
					Dwarf_Off dieset_relative_ip,
					expr::regs *p_regs) const
		{
			auto attrs = find_all_attrs();
			auto base_addr = calculate_addr_in_object(
				object_base_addr, r, dieset_relative_ip, p_regs);
			assert(attrs.find(DW_AT_type) != attrs.end());
			auto size = *(attrs.find(DW_AT_type)->second.get_refiter_is_type()->calculate_byte_size());
			if (absolute_addr >= base_addr
			&&  absolute_addr < base_addr + size)
			{
				return absolute_addr - base_addr;
			}
			return opt<Dwarf_Off>();
		}
		
		/* We want to iterate over visible named grandchildren, in a way that
		 * exploits the cache in resolve_all_visible_from_root. How to do this?
		 *
		 * I think we have to write a new kind of iterator -- visible_named_grandchildren_iterator.
		 * Oh, but that's not quite enough, and doesn't exploit the cache.
		 * How would it work? 
		 * We walk the visible named grandchildren and look for one that matches,
		 * preferentially using the cache.
		 * But the cache is by name.
		 * We could maintain a cache by offset too.
		 * But walking over visible named grandchildren is too easy... we just
		 * do a filter_iterator over grandchildren, since
		 * walking in offset order should be fast anyway.
		 * Okay -- if it's that easy, add it!
		 * 
		 * But also add another kind of iterator: 
		 * a *name-specific* visible-named-grandchildren iterator.
		 * If the cache is complete, we just iterate over the cache.
		 * If it's not complete, we do the naive thing, but ensure that
		 * when we hit the end, we mark the cache as complete. Can we do this?
		 * GAH. Polymorphism on return: is there any easy way to do this?
		 * Can we return a shared ptr to a sequence of some vtable'd type?
		 * NO because the return type of the vtable methods still needs to be fixed.
		 * Iterators need to be passed around by value.
		 * We could easily make our own vtable'd "late-bound iterator" abstract class,
		 * and use that. Hmpf.
		 * */
		
		
/* from data_member_die, special helper for bitfields */
		iterator_df<type_die> data_member_die::find_or_create_type_handling_bitfields() const
		{
			return find_type();
		}
		iterator_df<type_die> member_die::find_or_create_type_handling_bitfields() const
		{
			/* Just do find_type if we don't have the bitfield attributes. */
			if (!get_bit_size() && !get_bit_offset() && !get_data_bit_offset()) return find_type();

			debug(2) << "Handling bitfield member at 0x" << std::hex << get_offset() << std::dec
				<< std::endl;
			
			opt<Dwarf_Unsigned> opt_bsz = get_bit_size();
			opt<Dwarf_Unsigned> opt_boff = get_bit_offset();
			opt<Dwarf_Unsigned> opt_data_boff = get_data_bit_offset();
			iterator_df<type_die> t = find_type();
			
			if (!t.is_a<base_type_die>()) return t;
			auto bt = t.as_a<base_type_die>();
			
			pair<Dwarf_Unsigned, Dwarf_Unsigned> bt_bit_size_and_offset = bt->bit_size_and_offset();
			
			if (!bt->get_name()) return t;
			
			Dwarf_Unsigned effective_bit_size = opt_bsz ? *opt_bsz : bt_bit_size_and_offset.first;
			Dwarf_Unsigned effective_bit_offset = opt_data_boff ? *opt_data_boff : 
				opt_boff ? *opt_boff : bt_bit_size_and_offset.second;
			
			/* Decide whether to create -- need to search existing base types.
			 * HACK: use naming and visible_named_grandchildren:
			 * Varied-width/size types should always have the same name as the full-width one.
			 * To exploit the cache in resolve_all_visible_from_root, I added a new method
			 * find_all_visible_named_grandchildren() that takes a name. */
			auto name = *bt->get_name();
			vector<iterator_base> all_found = get_root().find_all_visible_grandchildren_named(name);
			debug() << "Found " << all_found.size() 
				<< " (in any CU) matching '" << name << "'" << std::endl;
			
			/* Does any of them really match? */
			auto not_equal_in_bits
			 = [effective_bit_size, effective_bit_offset](const iterator_base &i) {
				if (!i.is_a<base_type_die>()) return true;
				auto other_bt = i.as_a<base_type_die>();
				pair<Dwarf_Unsigned, Dwarf_Unsigned> bit_size_and_offset = other_bt->bit_size_and_offset();
				
				return !(effective_bit_size == bit_size_and_offset.first
					&& effective_bit_offset == bit_size_and_offset.second);
			};
			
			auto new_end = std::remove_if(all_found.begin(), all_found.end(), not_equal_in_bits);
			unsigned count_after_removal = srk31::count(all_found.begin(), new_end);
			debug() << "Of those, " << count_after_removal << " are equal in bits (sz "
				 << effective_bit_size << ", offs " << effective_bit_offset << ")" << std::endl;
			switch (count_after_removal)
			{
				case 0: {
					// we need to create
					// where to create -- new CU? yes, I guess so
					auto cu = get_root().get_or_create_synthetic_cu();
					auto created = get_root().make_new(cu, DW_TAG_base_type);
					auto& attrs = dynamic_cast<core::in_memory_abstract_die&>(created.dereference())
						.attrs();
					encap::attribute_value v_name(*bt->get_name()); // must have a name
					attrs.insert(make_pair(DW_AT_name, v_name));
					encap::attribute_value v_bit_size(effective_bit_size);
					attrs.insert(make_pair(DW_AT_bit_size, v_bit_size));
					encap::attribute_value v_bit_offset(effective_bit_offset);
					attrs.insert(make_pair(DW_AT_data_bit_offset, v_bit_offset));
					encap::attribute_value v_encoding(bt->get_encoding());
					attrs.insert(make_pair(DW_AT_encoding, v_encoding));
					Dwarf_Unsigned effective_byte_size = 
						((effective_bit_offset + effective_bit_size) % 8 == 0) ?
						(effective_bit_offset + effective_bit_size) / 8 :
						1 + (effective_bit_offset + effective_bit_size) / 8;
					encap::attribute_value v_byte_size(effective_byte_size);
					attrs.insert(make_pair(DW_AT_byte_size, v_byte_size));
					// debugging
					get_root().print_tree(std::move(cu), debug(2));
					return created;
				}
				default:
					// just go with the first, so fall through
				case 1:
					// pick the first and return
					return all_found.begin()->as_a<base_type_die>();
			}
		}
/* from data_member_die */
		encap::loclist data_member_die::get_dynamic_location() const
		{
			/* These guys have loclists that add to what's on the
			   top-of-stack, which is what we want. */
			opt<encap::loclist> opt_location = get_data_member_location();
			return opt_location ? *opt_location : encap::loclist();
		}
/* from spec::with_dynamic_location_die */
		Dwarf_Addr 
		with_dynamic_location_die::calculate_addr_on_stack(
				Dwarf_Addr frame_base_addr,
				root_die& r, 
				Dwarf_Off dieset_relative_ip,
				expr::regs *p_regs/* = 0*/) const
		{
			auto attrs = find_all_attrs();
			assert(attrs.find(DW_AT_location) != attrs.end());
			
			/* We have to find ourselves. Well, almost -- enclosing CU. */
			auto found = r.cu_pos(get_enclosing_cu_offset());
			iterator_df<compile_unit_die> i_cu = found;
			assert(i_cu != iterator_base::END);
			Dwarf_Addr dieset_relative_cu_base_ip
			 = i_cu->get_low_pc() ? i_cu->get_low_pc()->addr : 0;
			
			if (dieset_relative_ip < dieset_relative_cu_base_ip)
			{
				debug(2) << "Warning: bad relative IP (0x" << std::hex << dieset_relative_ip << std::dec
					<< ") for stack location of DIE in compile unit "
					<< i_cu 
					<< ": " << *this << endl;
				throw No_entry();
			}
			
			auto& loclist = attrs.find(DW_AT_location)->second.get_loclist();
			auto intervals = loclist.intervals();
			assert(intervals.begin() != intervals.end());
			auto first_interval = intervals.begin();
			auto last_interval = intervals.end(); --last_interval;
			encap::loc_expr fb_loc_expr((Dwarf_Unsigned[]) { DW_OP_plus_uconst, frame_base_addr }, 
					first_interval->lower(), last_interval->upper());
			encap::loclist fb_loclist(fb_loc_expr);
			
			auto rewritten_loclist = encap::rewrite_loclist_in_terms_of_cfa(
				loclist, 
				r.get_frame_section(),
				fb_loclist
			);
			debug(2) << "After rewriting, loclist is " << rewritten_loclist << endl;
			
			return (Dwarf_Addr) expr::evaluator(
				rewritten_loclist,
				dieset_relative_ip // needs to be CU-relative
				 - dieset_relative_cu_base_ip,
				found.spec_here(),
				p_regs,
				frame_base_addr).tos();
		}
		Dwarf_Addr
		with_dynamic_location_die::calculate_addr_in_object(
				Dwarf_Addr object_base_addr,
				root_die& r, 
				Dwarf_Off dieset_relative_ip,
				expr::regs *p_regs /*= 0*/) const
		{
			auto attrs = find_all_attrs();
			iterator_df<compile_unit_die> i_cu = r.cu_pos(get_enclosing_cu_offset());
			assert(attrs.find(DW_AT_data_member_location) != attrs.end());
			return (Dwarf_Addr) expr::evaluator(
				attrs.find(DW_AT_data_member_location)->second.get_loclist(),
				dieset_relative_ip == 0 ? 0 : // if we specify it, needs to be CU-relative
				 - (i_cu->get_low_pc() ? 
				 	i_cu->get_low_pc()->addr : (Dwarf_Addr)0),
				i_cu.spec_here(), 
				p_regs,
				object_base_addr, // ignored
				{ 0 }).tos();
		}
/* from spec::with_named_children_die */
//         std::shared_ptr<spec::basic_die>
//         with_named_children_die::named_child(const std::string& name) 
//         { 
// 			try
//             {
//             	for (auto current = this->get_first_child();
//                 		; // terminates by exception
//                         current = current->get_next_sibling())
//                 {
//                 	if (current->get_name() 
//                     	&& *current->get_name() == name) return current;
//                 }
//             }
//             catch (No_entry) { return shared_ptr<spec::basic_die>(); }
//         }
// 
//         std::shared_ptr<spec::basic_die> 
//         with_named_children_die::resolve(const std::string& name) 
//         {
//             std::vector<std::string> multipart_name;
//             multipart_name.push_back(name);
//             return resolve(multipart_name.begin(), multipart_name.end());
//         }
// 
//         std::shared_ptr<spec::basic_die> 
//         with_named_children_die::scoped_resolve(const std::string& name) 
//         {
//             std::vector<std::string> multipart_name;
//             multipart_name.push_back(name);
//             return scoped_resolve(multipart_name.begin(), multipart_name.end());
//         }
/* from spec::compile_unit_die */
		encap::rangelist
		compile_unit_die::normalize_rangelist(const encap::rangelist& rangelist) const
		{
			encap::rangelist retval;
			/* We create a rangelist that has no address selection entries. */
			for (auto i = rangelist.begin(); i != rangelist.end(); ++i)
			{
				switch(i->dwr_type)
				{
					case DW_RANGES_ENTRY:
						retval.push_back(*i);
					break;
					case DW_RANGES_ADDRESS_SELECTION: {
						assert(i->dwr_addr1 == 0xffffffff || i->dwr_addr1 == 0xffffffffffffffffULL);
						assert(false);
					} break;
					case DW_RANGES_END: 
						assert(i->dwr_addr1 == 0);
						assert(i+1 == rangelist.end()); 
						retval.push_back(*i);
						break;
					default: assert(false); break;
				}
			}
			return retval;
		}

		bool compile_unit_die::is_generic_pointee_type(iterator_df<type_die> t) const
		{
			switch(get_language())
			{
				/* See DWARF 3 sec. 5.12! */
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99:
					return (!t || (t.is_a<unspecified_type_die>() && 
								(!t.name_here() || *t.name_here() == "void")))
						|| (t.is_a<base_type_die>() &&
							t.as_a<base_type_die>()->get_byte_size() == 1 &&
							(t.as_a<base_type_die>()->get_encoding() == DW_ATE_signed_char
							|| t.as_a<base_type_die>()->get_encoding() == DW_ATE_unsigned_char));
				default:
					return opt<Dwarf_Unsigned>();
			}
		}

		unsigned compile_unit_die::alignment_of_type(iterator_df<type_die> t) const
		{
			/* The alignment is actually something the compiler should
			 * document for us, but doesn't. It is largely a function of
			 * the language and ABI pair. So we should really do a
			 * switch on this pair. For now, a gross approximation:
			 * it's MAX(size, word-size). FIXME: this is just wrong. */
			switch (get_language())
			{
				default:
				// case DW_LANG_C:
				// case DW_LANG_C89:
				// case DW_LANG_C_plus_plus:
				// case DW_LANG_C99:
					/* Our default behaviour is true of C-family languages.
					 * PROBLEM: this is impl-defined but the DWARF does not
					 * describe it explicitly. */
// 					if (t.is_a<qualified_type_die>())
// 					{
// 						// C11 6.2.5 pt 27
// 						return alignment_of_type(t.as_a<qualified_type_die>()->find_type());
// 						// FIXME: not true of atomics
// 					}
// 					if (t.is_a<with_data_members_die>())
// 					{
// 						// look for the biggest alignment of any member
// 						
// 					}
					{
						auto maybe_array_element_type = t.is_a<array_type_die>() ?
						    t.as_a<array_type_die>()->ultimate_element_type()
						    : opt<iterator_df<type_die>>();
						auto maybe_byte_size =
						    (maybe_array_element_type ? maybe_array_element_type : t)
							->calculate_byte_size();
						if (!maybe_byte_size) return 1; // FIXME: better error report
						return std::max<unsigned>(get_address_size(),
							std::min<Dwarf_Unsigned>(1, *maybe_byte_size));
					}
			}
		}
		static string default_cxx_mangle(const string& s)
		{ assert(false); exit(-1); }
		opt<string> compile_unit_die::mangled_name_for(iterator_base i) const
		{
			switch(get_language())
			{
#define Cplusplus_cases \
				case DW_LANG_C_plus_plus: \
				maybe_Cplusplus_03_and_later_cases

#ifdef DW_LANG_C_plus_plus_03
#define maybe_Cplusplus_03_and_later_cases case DW_LANG_C_plus_plus_03: \
	maybe_Cplusplus_11_and_later_cases
#else
#define maybe_Cplusplus_03_and_later_cases
#endif

#ifdef DW_LANG_C_plus_plus_11
#define maybe_Cplusplus_11_and_later_cases case DW_LANG_C_plus_plus_11: \
	maybe_Cplusplus_14_and_later_cases
#else
#define maybe_Cplusplus_11_and_later_cases
#endif

#ifdef DW_LANG_C_plus_plus_14
#define maybe_Cplusplus_14_and_later_cases case DW_LANG_C_plus_plus_14: \
	maybe_Cplusplus_17_and_later_cases
#else
#define maybe_Cplusplus_14_and_later_cases
#endif

#ifdef DW_LANG_C_plus_plus_17
#define maybe_Cplusplus_17_and_later_cases case DW_LANG_C_plus_plus_17:
#else
#define maybe_Cplusplus_17_and_later_cases
#endif
				Cplusplus_cases
					return default_cxx_mangle(*i.name_here());

#define C_cases \
				case DW_LANG_C: \
				case DW_LANG_C89: \
				maybe_C99_and_later_cases

#ifdef DW_LANG_C99
#define maybe_C99_and_later_cases case DW_LANG_C99: \
	maybe_C11_and_later_cases
#else
#define maybe_C99_and_later_cases
#endif

#ifdef DW_LANG_C11
#define maybe_C11_and_later_cases case DW_LANG_C11:
#else
#define maybe_C11_and_later_cases
#endif

				C_cases
					return i.name_here();
				default:
					return opt<string>(); // FIXME
			}
		}
		opt<Dwarf_Unsigned> compile_unit_die::implicit_array_base() const
		{
			switch(get_language())
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
		iterator_df<type_die> compile_unit_die::implicit_enum_base_type() const
		{
			if (cached_implicit_enum_base_type) return cached_implicit_enum_base_type; // FIXME: cache "not found" result too
			/* Language-specific knowledge. */
			switch(get_language())
			{
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99: {
					const char *attempts[] = { "signed int", "int" };
					size_t total_attempts = sizeof attempts / sizeof attempts[0];
					for (unsigned i_attempt = 0; i_attempt < total_attempts; ++i_attempt)
					{
						auto found = named_child(attempts[i_attempt]);
						if (found != iterator_base::END && found.is_a<type_die>())
						{
							cached_implicit_enum_base_type = found.as_a<type_die>();
							return found;
						}
					}
					assert(false && "enum but no int or signed int");
				}
				default:
					break;
			}
			// FIXME: anything left to do here?
			return iterator_base::END;
		}
		iterator_df<type_die> compile_unit_die::implicit_subrange_base_type() const
		{
			if (cached_implicit_subrange_base_type) return cached_implicit_subrange_base_type; // FIXME: cache "not found" result too

			/* Subranges might have no type, at least in that I've seen gcc generate them.
			 * This does not necessarily mean they are the empty subrange!
			 * Could be a gcc bug, but we need to do something.
			 * Since these subranges are under array types, and because array
			 * ranges are unsigned, we relax a bit and look for unsigneds too.
			 * BUT problem: it need not have the C name for unsigned int.
			 * We used to look for "sizetype" but turns out that's a gcc bug.
			 * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80263
			 * Instead, look first for an unnamed unsigned integer type,
			 * then for "sizetype" as a bug workaround,
			 * then whatever we do for enums.
			 */
			switch(get_language())
			{
				case DW_LANG_C:
				case DW_LANG_C89:
				case DW_LANG_C_plus_plus:
				case DW_LANG_C99: {
					const char *attempts[] = { "unsigned int", "unsigned" };
					size_t total_attempts = sizeof attempts / sizeof attempts[0];
					for (unsigned i_attempt = 0; i_attempt < total_attempts; ++i_attempt)
					{
						auto found = named_child(attempts[i_attempt]);
						if (found != iterator_base::END && found.is_a<type_die>())
						{
							cached_implicit_subrange_base_type = found.as_a<type_die>();
							return found;
						}
					}
				}
				default:
					break;
			}
			auto child_bts = children().subseq_of<base_type_die>();
			for (auto i_bt = std::move(child_bts.first); i_bt != child_bts.second; ++i_bt)
			{
				if (i_bt->get_encoding() == DW_ATE_unsigned
					 && (!i_bt.name_here()
						|| /* gcc bug workaround */ *i_bt.name_here() == "sizetype"))
				{
					cached_implicit_subrange_base_type = i_bt.as_a<type_die>();
					return cached_implicit_subrange_base_type;
				}
			}
			return this->implicit_enum_base_type();
		}
	}
} // end namespace dwarf
