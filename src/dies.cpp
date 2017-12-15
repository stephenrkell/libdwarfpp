/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * dies.cpp: methods specific to each DIE tag
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#include "dwarfpp/abstract.hpp"
#include "dwarfpp/abstract-inl.hpp"
#include "dwarfpp/root.hpp"
#include "dwarfpp/root-inl.hpp"
#include "dwarfpp/iter.hpp"
#include "dwarfpp/iter-inl.hpp"
#include "dwarfpp/dies.hpp"
#include "dwarfpp/dies-inl.hpp"

#include <memory>
#include <srk31/algorithm.hpp>

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
					unsigned element_size = 1; /* FIXME: always 1? */
					auto opt_byte_size = fixed_length_in_bytes();
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
			// should never recurse
			return print_type_abstract_name(s, get_concrete_type());
		}
		std::ostream& array_type_die::print_abstract_name(std::ostream& s) const
		{
			opt<Dwarf_Unsigned> element_count = find_self().as_a<array_type_die>()->element_count();
			s << "__ARR" << (element_count ? *element_count : 0) << "_";
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
		type_iterator_base::first_outgoing_edge_target() const
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
		type_iterator_base::predecessor_node_next_outgoing_edge_target() const
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
		
		
		void type_iterator_base::increment_edgewise(bool skip_dependencies)
		{
			// this should be true for a type_iterator_df_walk -- not for other iterators
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
		void type_iterator_base::decrement()
		{
			assert(false); // FIXME
		}
		void type_iterator_df_edges::increment()
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
			 *   make ourselves grey, and try to follow an outgoing edge.
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
				 * for the fake name string is a bad idea. Instead, use the defining
				 * source file path, if we have it. */
				if (get_decl_file() && get_decl_line())
				{
					std::ostringstream s;
					opt<string> maybe_fqp = find_self().enclosing_cu()->source_file_fq_pathname(*get_decl_file());
					s << (maybe_fqp ? 
						*maybe_fqp : 
						find_self().enclosing_cu()->source_file_name(*get_decl_file())) 
						<< "_" << *get_decl_line();
					name_to_use = s.str();
				}
				else name_to_use = offsetstr;
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
		/* begin pasted from adt.cpp */
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
		opt<uint32_t> type_die::summary_code() const
		{
			//return this->summary_code_using_walk_type();
			using lib::Dwarf_Unsigned;
			using lib::Dwarf_Half;
			using namespace dwarf::core;

			opt<uint32_t> code_to_return;
			summary_code_word_t output_word;
			/* if we have it cached, return that */
			auto found_cached = get_root().type_summary_code_cache.find(get_offset());
			if (found_cached != get_root().type_summary_code_cache.end())
			{
				return found_cached->second;
			}
		
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
			if (concrete_t->get_declaration() && *concrete_t->get_declaration())
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
						summary_code_word_t tmp_output_word;
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
			get_root().type_summary_code_cache.insert(
				make_pair(get_offset(), code_to_return)
			);
			return code_to_return;
		}
		
		opt<uint32_t> type_die::summary_code_using_iterators() const
		{
			if (this->cached_summary_code) return this->cached_summary_code;
			
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
			summary_code_word_t output_word;
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
				Dwarf_Off offset_here = get_offset();
				walk_type(self, self,
					/* pre_f */ [&output_word, offset_here](
						iterator_df<type_die> t, iterator_df<program_element_die> reason) -> bool {
						if (t && t.offset_here() == offset_here) return true;
						if (!t) output_word << 0;
						else output_word << t->summary_code_using_iterators();
						return false;
					},
					/* post_f */ [](iterator_df<type_die> t, iterator_df<program_element_die> reason) -> void
					{});
				// distinguish ourselves, unless we're a typedef.
				if (!self.is_a<qualified_type_die>() && !self.is_a<typedef_die>())
				{
					output_word << get_tag();
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
			summary_code_word_t output_word;
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
		
		/* reverse-engineering helper. */
		iterator_df<type_die> type_iterator_base::source_vertex_for(
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

		void get_all_sccs(root_die& r)
		{
			/* It's not efficient to calculate SCCs piecewise, because large subgraphs
			 * get computed repeatedly. So... */
			struct all_type_edges_df_iterator : public type_iterator_base,
									  public boost::iterator_facade<
									    type_iterator_df_edges
									  , type_die
									  , boost::forward_traversal_tag
									  , type_die& /* Reference */
									  , Dwarf_Signed /* difference */
									  >
			{
				typedef all_type_edges_df_iterator self;
				friend class boost::iterator_core_access;
				
				sequence<
					typename subseq_t< iterator_sibs<basic_die>, is_a_t<type_die> >::transformed_iterator
				> top_level_sequence;
				subseq_t< iterator_sibs<basic_die>, is_a_t<type_die> >::transformed_iterator
					top_level_pos;
				multimap< iterator_df<type_die>, 
					pair<iterator_df<program_element_die>, iterator_df<type_die> > > reverse_edges;
				
				all_type_edges_df_iterator(const iterator_sibs<compile_unit_die>& cu)
				 : top_level_sequence(cu.children().subseq_of<type_die>()),
				   top_level_pos(top_level_sequence.first, top_level_sequence.second)
				{
					base_reference() = top_level_sequence.first;
					m_reason = END;
				}
				all_type_edges_df_iterator(iterator_sibs<compile_unit_die>&& cu)
				 : top_level_sequence(std::move(cu).children().subseq_of<type_die>()),
				   top_level_pos(top_level_sequence.first, top_level_sequence.second)
				{
					top_level_pos = top_level_sequence.first;
					base_reference() = top_level_sequence.first;
					m_reason = END;
				}
				all_type_edges_df_iterator(const self& arg)
				 : type_iterator_base(arg), top_level_sequence(arg.top_level_sequence),
				 	top_level_pos(arg.top_level_pos)
				{}
				all_type_edges_df_iterator(self&& arg)
				 : type_iterator_base(std::move(static_cast<type_iterator_base&&>(arg))),
				   top_level_sequence(arg.top_level_sequence),
				   top_level_pos(arg.top_level_pos)
				{}

				void increment()
				{
					/* We also want to maintain a reverse adjacency list representation
					 * of the entire type graph. We can then use this to compute
					 * strongly connected components. */
				
					this->increment_edgewise(false);
					if (base() == END)
					{
						// move on to the next
						++top_level_pos;
						if (top_level_pos)
						{
							base_reference() = top_level_pos;
							m_reason = END;
						}
						else
						{
							/* We've hit the end. Use the reverse-reachability info to
							 * compute all the SCCs. */
						}
					}
				}
			};
			
			auto cu_seq = r.children();
			for (auto i_cu = cu_seq.first; i_cu != cu_seq.second; ++i_cu)
			{
				all_type_edges_df_iterator i_cu_edges(std::move(i_cu));
				while (i_cu_edges) ++i_cu_edges;
			}
		}
		
		opt<type_scc_t> type_die::get_scc() const
		{
			assert(get_root().live_dies.find(get_offset()) != get_root().live_dies.end());
			assert(get_root().live_dies.find(get_offset())->second
				== dynamic_cast<const basic_die *>(this));
			
			/* In general, to get a representation of the SCC (if any)
			 * in which a type DIE participates,
			 * we walk it and look for back edges. 
			 * We remember all the edges we see.
			 * If we have seen no back edges, it's not in a cycle
			 * and hence not in an SCC. */

			/* This is a generic implementation. If subclasses know that they can 
			 * never be part of a cycle, they're allowed to override to just the following.
				return opt<type_scc_t>();
			 * FIXME: do this. Base types. others? */
			
			// cheque the cache. we might have a null pointer cached, meaning "not cyclic"
			if (opt_cached_scc) return *opt_cached_scc ? **opt_cached_scc : opt<type_scc_t>();
			
			assert(opt_cached_scc);
			return *opt_cached_scc ? **opt_cached_scc : opt<type_scc_t>();
			
			auto start_t = find_self();
			get_all_sccs(start_t.root());
			
			/* Since we are committed to doing a full depth-first exploration of the type graph
			 * from our current position, the efficient thing is to compute *all* SCCs that
			 * are reachable from the start position. Then, even if we find that *we* are
			 * not part of any cycle/SCC, we might cache some SCCs in other type_dies that
			 * we find that *are* in cycles.
			 *
			 * We collect a bunch of cycles,
			 * then
			 * for each node that participates in any cycle,
			 * we compute its SCC
			 * by recursively combining cycles until a fixed point. */
			
			typedef type_scc_t::value_type edge_t;
			/* We store cycles as vectors
			 * where the first edge's source is the last edge's target. */
			typedef vector<edge_t> cycle_t;
			vector< cycle_t > reached_cycles;
			multimap< iterator_df<type_die>, unsigned > cycles_by_participating_node;
			
			/* Once we've computed these, we elaborate the set of edges in the SCC,
			 * by first enumerating the cycles that the start node participates in,
			 * then by enumerating the cycles that any participating nodes participate in,
			 * and so on, until a fixed point. We don't need to traverse any cycle
			 * more than once. */
			debug_expensive(5, << "Starting SCC search at: " << start_t.summary()
				<< " (type_die object at " << dynamic_cast<const basic_die *>(&start_t.dereference())
				<< ", sanity check: " << dynamic_cast<const basic_die *>(this) << ")" << std::endl);
			
			auto add_edge = [&cycles_by_participating_node](cycle_t& cycle, unsigned idx, const edge_t& edge) {
				cycle.push_back(edge);

				cycles_by_participating_node.insert(make_pair(
					edge.first.first, idx
				));
				cycles_by_participating_node.insert(make_pair(
					edge.second, idx
				));
			};
			
			/* HMM. We might be better off defining a new iterator that is like
			 * DFS but expands the dag into a tree, i.e. visits non-back edges
			 * multiple times, once for every path from the start node.
			 * That seems to blow up the exploration unnecessarily... but
			 * without that, we need a reliable way of adding all cycles when
			 * we see a cross-edge.
			 *
			 * If we see a cross-edge, we can reach any cycle that is reachable
			 * from its target. Hmm. */
			
			for (type_iterator_df_edges i_edge = start_t; i_edge; ++i_edge)
			{
				/* REMEMBER: type iterators are allowed to walk the "no DIE" (void) case,
				 * so we might have END here. */
				debug_expensive(5, << "Walk reached: " << i_edge.summary()
					<< "; reason: " << i_edge.reason().summary());
				auto edge = i_edge.as_incoming_edge();
				
				switch (i_edge.incoming_edge_kind())
				{
					case type_iterator_base::TREE:
						debug_expensive(5, << "; edge TREE" << std::endl);
						break;
					case type_iterator_base::CROSS:
						debug_expensive(5, << "; edge CROSS" << std::endl);
						/* When we hit a cross edge A -> B, it means
						 *
						 * For any cycle involving the target T of the cross edge
						 * *and* any vertex V on the current tree path,
						 * there is a cycle that includes
						 * the current tree path from V onwards
						 * the cycle's nodes, from T to V.
						 * We choose the *earliest* such vertex on the tree path
						 * to get the biggest cycle.
						 */
						{
							iterator_df<type_die>& cross_edge_source = edge.first.first;
							iterator_df<type_die>& cross_edge_target = edge.second;
							iterator_df<program_element_die>& cross_edge_reason = edge.first.second;
							auto cycles_involving_target = cycles_by_participating_node.equal_range(cross_edge_target);
							std::set<unsigned> cycle_idxs_involving_target;
							for (auto i_cyc = cycles_involving_target.first;
								i_cyc != cycles_involving_target.second; ++i_cyc)
							{
								debug(5) << "Cycle involving cross-edge target: " << i_cyc->second
									<< std::endl;
								cycle_idxs_involving_target.insert(i_cyc->second);
							}
							
							/* Go along the tree-edge nodes. If any of the cycles involves
							 * that node, we have a new cycle to add.
							 
							 * Might we get an infinite loop here? If we add a cycle
							 * that also shows up in later tree-edges' intersections,
							 * i.e. if we have two cross-edges to the same subtree...
							 
                                         o
                                        / \
                                       o   o
                                     ...    \
                                             o
                                      cross-' \
                                               o
                                        cross-'
							
							 * ... where the target of the cross-edge is the same in both cases,
							 * then the first one of these will add a bunch of cycles
							 * that also get picked up second time around, because they involve
							 * the same cross-edge node and hit the current tree edges.
							 * Can we skip any of these?
							 */
							
							/* We want to remember only the cycles involving *any* 
							 * of our tree nodes, and the highest one they reach. */
							map<unsigned, decltype(i_edge.m_stack)::const_iterator > cycles_and_tree_edges;
							set<unsigned> cycles_involved;
							for (auto i_tree_edge = i_edge.m_stack.begin();
								i_tree_edge != i_edge.m_stack.end();
								++i_tree_edge)
							{
								auto tree_edge_source = i_edge.source_vertex_for(
									i_tree_edge->first, i_tree_edge->second
								);
								if (!tree_edge_source) continue; // it's the fake initial edge
								auto& tree_edge_target = i_tree_edge->first;
								auto& tree_edge_reason = i_tree_edge->second;

								auto cycles_involving_tree_source = cycles_by_participating_node.equal_range(tree_edge_source);
								for (auto i_cyc = cycles_involving_tree_source.first;
									i_cyc != cycles_involving_tree_source.second; ++i_cyc)
								{
									debug(5) << "Cycle involving a tree-edge source: " << i_cyc->second
										<< std::endl;
									if (cycle_idxs_involving_target.find(i_cyc->second)
										!= cycle_idxs_involving_target.end())
									{
										auto iter_and_inserted = cycles_involved.insert(i_cyc->second);
										auto& inserted = iter_and_inserted.second;
										if (inserted)
										{
											cycles_and_tree_edges.insert(make_pair(i_cyc->second,
												i_tree_edge));
// 												edge_t(
// 													make_pair(
// 														i_edge.source_vertex_for(
// 															i_tree_edge->first
// 														),
// 														i_tree_edge->second
// 													),
// 													i_tree_edge->first
// 												)
// 											));
										}
									}
								}
							}
							
							/* Now we know which cycles matter. We remembered the highest
							 * tree node in each case. */
							if (cycles_involved.size() > 0 )
							{
								debug_expensive(5, << "We think this cross edge forms " 
									<< cycles_involved.size()
									<< " new cycles, based on idxs {");
								for (auto i_idx = cycles_involved.begin();
									i_idx != cycles_involved.end(); ++i_idx)
								{
									if (i_idx != cycles_involved.begin()) debug_expensive(5, << ",");
									debug_expensive(5, << *i_idx);
								}
								debug_expensive(5, << "}" << std::endl);
							}
								
							for (auto i_cyc_pair = cycles_and_tree_edges.begin();
								i_cyc_pair != cycles_and_tree_edges.end();
								++i_cyc_pair)
							{
								debug_expensive(5, << "New cycle " << reached_cycles.size()
									<< " based on cycle " << i_cyc_pair->first << std::endl);

								/* We have a new cycle that involves 
								 * - the current tree path from V onwards;
								 * - the cycle's nodes, from T to V.
								 */
								cycle_t& existing_cycle = reached_cycles.at(i_cyc_pair->first);
								cycle_t new_cycle;
								unsigned new_idx = reached_cycles.size();
								auto tree_edge_source = i_edge.source_vertex_for(i_cyc_pair->second->first,
									i_cyc_pair->second->second);
								for (auto i_tree_edge_to_add = i_cyc_pair->second;
									i_tree_edge_to_add != i_edge.m_stack.end();
									++i_tree_edge_to_add)
								{
									auto tree_edge_to_add_source = i_edge.source_vertex_for(
										i_tree_edge_to_add->first, i_tree_edge_to_add->second
									);
									if (!tree_edge_to_add_source) continue; // it's the fake initial edge
									auto& tree_edge_to_add_target = i_tree_edge_to_add->first;
									auto& tree_edge_to_add_reason = i_tree_edge_to_add->second;
									debug_expensive(5, << "Tree edge "
										<< tree_edge_to_add_source.summary()
										<< " --> "
										<< tree_edge_to_add_target.summary()
										<< std::endl
									);
									type_edge new_e(
										make_pair(
											/* source */ iterator_df<type_die>(
												tree_edge_to_add_source
											),
											/* reason */ iterator_df<program_element_die>(
												tree_edge_to_add_reason
											)
										),
										/* target */ iterator_df<type_die>(
											tree_edge_to_add_target
										)
									);
									add_edge(new_cycle, new_idx, new_e);
								}
								/* Add the cross edge. */
								debug_expensive(5, << "Cross edge "
									<< cross_edge_source.summary()
									<< " --> "
									<< cross_edge_target.summary()
									<< std::endl
								);
								type_edge c_e(
									make_pair(
										/* source */ iterator_df<type_die>(cross_edge_source),
										/* reason */ iterator_df<program_element_die>(cross_edge_reason)
									),
									/* target */ iterator_df<type_die>(cross_edge_target)
								);
								add_edge(new_cycle, new_idx, c_e);
								/* Add the existing cycle's edges from the cross-edge target...
								 * We start with the edge whose source is the cross-edge
								 * target. */
								auto found = std::find_if(existing_cycle.begin(),
									existing_cycle.end(),
									[cross_edge_target](const edge_t & e) -> bool {
										return e.source() == cross_edge_target;
									});
								for (auto i_cyc_edge = std::move(found);
										/* The last one we do should have a *target*
										 * of the tree edge source. So stop if our source
										 * is the tree edge source. */
										i_cyc_edge->source() != i_edge.source_vertex_for(
										i_cyc_pair->second->first, i_cyc_pair->second->second
										);
										/* increment with wraparoud */
										((unsigned)(i_cyc_edge - existing_cycle.begin()) < existing_cycle.size() - 1u)
											? ++i_cyc_edge
											: (i_cyc_edge = existing_cycle.begin()))
								{
									debug_expensive(5, << "Existing cycle edge "
										<< i_cyc_edge->source().summary()
										<< " --> "
										<< i_cyc_edge->target().summary()
										<< std::endl
									);
									type_edge new_e(
										make_pair(
											/* source */ iterator_df<type_die>(i_cyc_edge->source()),
											/* reason */ iterator_df<program_element_die>(
												i_cyc_edge->label()
											)
										),
										/* target */ iterator_df<type_die>(i_cyc_edge->target())
									);
									add_edge(new_cycle, new_idx, new_e);
								}
								// check the vector invariant on edges
								assert(new_cycle.size() > 0);
								assert(new_cycle.begin()->source() == new_cycle.rbegin()->target());
								// give this cycle its identity
								reached_cycles.push_back(new_cycle);
								assert(reached_cycles.size() == new_idx + 1);
							} // end for each cycle in common
						} // end case block
						break;
					case type_iterator_base::BACK:
						debug_expensive(5, << "; edge BACK" << std::endl);
						/* If we see back-edges, we're exploring a partially-cyclic type.
						 * But just because we find back-edges doesn't mean the start node
						 * is part of a cycle. The cycle might not reach back to the start node.
						 * 
						 * Put differently: we are definitely seeing a cycle. But the cycle
						 * might not include the start node. How does this affect our caching
						 * logic? It's only when we know that the cycle includes the start
						 * node that we can make use of nodes' cached SCC presence/absence.
						 *
						 * Edges that are not in a cycle are those that 
						 * are not *under* the target of any back-edge, where "under" is 
						 * defined in terms of the depth-first tree. Our start node is the 
						 * root of the depth-first tree, by definition. So to figure out 
						 * whether the root node is part of a cycle, we only need to look
						 * for back-edges that directly reach *it*. We remember this for
						 * later; if we didn't see any cycle involving the root node,
						 * we're not cyclic and can skip the remaining SCC calculation stuff. */
						// is this a back-edge reaching back to the root?
						/* back edges are (target, reason) */
						/* general edges are ((source, reason), target). */
						iterator_df<type_die>& back_edge_source = edge.first.first;
						iterator_df<type_die>& back_edge_target = edge.second;
						iterator_df<program_element_die>& back_edge_reason = edge.first.second;
						debug_expensive(5, << "Back edge: from " << back_edge_source.summary() << " to "
							<< back_edge_target.summary() << std::endl);
						
						/* Build the cycle we're currently forming, and give it an index. */
						cycle_t cycle;
						unsigned idx = reached_cycles.size();
						debug_expensive(5, << "Recording a new cycle (may or may not involve start node), idx " << idx << std::endl);
						/* Stacks are (target, reason). 
						 * All the tree edges are also in the cycle. Or are they?
						 * Only if we've seen the back-edge's target node
						 * as the source of this tree edge
						 * or a later tree edge. So
						 * 

					                                  o
					                                   \
					                                   _\|
					                                     o  <--.
					                                      \    |
					                                      _\|  |
					                                        o '

						 * ... the lower of the two tree edges is in the cycle, but
						 * the upper one isn't. We will see the back edge target at some
						 * point as we walk down the tree edges; all tree edges from then
						 * on (up to the back edge source) are part of the cycle. BUT:

					                                 o <-------.
					                                  \         \
					                                  _\|        |
					                                    o  <--.  |
					                                     \    |  |
					                                     _\|  |  |
					                                       o '   |
					                                        \    |
					                                        _\|  |
					                                          o '

						 * Now the inner loop is part of a cycle involving the start node.
						 * It's just that we hadn't seen the relevant back edge yet. What's
						 * the actual problem, though? We record this cycle and the nodes
						 * that participate in it; we figure out later, via the transitive
						 * addition of cycles, which edges go in the SCC.

						 * Another interesting case:

					                                 o <----.
					                                  \      \
					                                  _\|     |
					                                    o  <-----.
					                                     \    |  |
					                                     _\|  |  |
					                                       o '   |
					                                        \    |
					                                        _\|  |
					                                          o '

						 */
						/* The stack records the target node and the reaching reason.
						 * To get the source node we have to do some reverse engineering. */
						bool seen_back_edge_target = false;
						for (auto i_tree_edge = i_edge.m_stack.begin();
							i_tree_edge != i_edge.m_stack.end();
							++i_tree_edge)
						{
							auto tree_edge_source = i_edge.source_vertex_for(
								i_tree_edge->first, i_tree_edge->second
							);
							if (!tree_edge_source) continue; // it's the fake initial edge
							auto& tree_edge_target = i_tree_edge->first;
							auto& tree_edge_reason = i_tree_edge->second;

							debug_expensive(5, << "Now walking tree edge from " << tree_edge_source.summary()
								<< " to " << tree_edge_target.summary() << "; is it in the cycle? ");

							seen_back_edge_target |= (tree_edge_source == back_edge_target);

							if (seen_back_edge_target)
							{
								debug_expensive(5, << "yes" << std::endl);
								/* This tree edge is in the cycle. */
								edge_t new_e(
									make_pair(
										/* source */ iterator_df<type_die>(tree_edge_source),
										/* reason */ iterator_df<program_element_die>(tree_edge_reason)
									),
									/* target */ iterator_df<type_die>(tree_edge_target)
								);
								add_edge(cycle, idx, new_e);
							} else { debug_expensive(5, << "no" << std::endl); }
						}
						add_edge(cycle, idx, edge); // add the back edge
						// check the vector invariant on edges
						assert(cycle.size() > 0);
						assert(cycle.begin()->source() == cycle.rbegin()->target());
						// give this cycle its identity
						reached_cycles.push_back(cycle);
						assert(reached_cycles.size() == idx + 1);
						break;
				}
			} // end while i_t
			/* Did we see a cycle involving the start node? */
			auto cycle_idxs = cycles_by_participating_node.equal_range(start_t);
			if (cycle_idxs.first == cycle_idxs.second)
			{
				// cache the negative result: a positive optional holding a null pointer
				start_t.as_a<type_die>()->opt_cached_scc
				 = optional<shared_ptr<type_scc_t> >(shared_ptr<type_scc_t>(nullptr));
				// return it
				return opt<type_scc_t>();
			}
			else if (debug_level > 0)
			{
				debug() << "We think the following cycles (non-transitively) involve the start node:"
					<< std::endl;
				set<unsigned> idxs;
				for (auto i_pair = cycle_idxs.first; i_pair != cycle_idxs.second; ++i_pair)
				{
					idxs.insert(i_pair->second);
				}
				auto print_cycle = [](const cycle_t& cycle, unsigned n) -> void {
					debug_expensive(5, << "- cycle " << n << ":" << std::endl);
					for (auto i_e = cycle.begin(); i_e != cycle.end(); ++i_e)
					{
						debug_expensive(5, << "\t" << std::hex << "0x" << i_e->source().offset_here()
							<< " ----> 0x" << i_e->target().offset_here()
							<< " (reason: " << i_e->label().offset_here() << ")"
							<< std::endl);
					}
				};
				for (auto i_i = idxs.begin(); i_i != idxs.end(); ++i_i)
				{
					unsigned i = *i_i;
					auto& cycle = reached_cycles.at(i);
					print_cycle(cycle, i);
				}
				debug_expensive(5, << "While walking, we found the following other cycles:"
					<< std::endl);
				for (unsigned i = 0; i < reached_cycles.size(); ++i)
				{
					if (idxs.find(i) != idxs.end()) continue;
					
					auto& cycle = reached_cycles.at(i);
					print_cycle(cycle, i);
				}
			}
			/* Okay, so we did see a cycle including the start node.
			 * Elaborate the SCC by coalecing all cycles. */
			vector<bool> done(reached_cycles.size(), false);
			shared_ptr<type_scc_t> p_scc = std::make_shared<type_scc_t>();
			debug_expensive(5, << "Created a shared SCC structure at " << p_scc.get() << std::endl);
			type_scc_t& scc = *p_scc;
			std::function<void(unsigned)> transitively_add_cycle_n;
			transitively_add_cycle_n = [&done, &transitively_add_cycle_n,
				cycles_by_participating_node, &reached_cycles, &scc]
			(unsigned n) -> void {
				if (done[n]) return;
				done[n] = true;
				cycle_t& cur = reached_cycles.at(n);
				for (auto i_e = cur.begin(); i_e != cur.end(); ++i_e)
				{
					debug_expensive(5, << "Adding edge: "
						<< std::hex << "0x" << i_e->source().offset_here()
						<< " ----> 0x" << i_e->target().offset_here()
						<< " (reason: " << i_e->label().offset_here() << ")"
						<< std::endl);
						
					scc.insert(*i_e);
					auto source_range = cycles_by_participating_node.equal_range(i_e->source());
					for (auto i_n = source_range.first; i_n != source_range.second; ++i_n)
					{
						transitively_add_cycle_n(i_n->second);
					}
					auto target_range = cycles_by_participating_node.equal_range(i_e->target());
					for (auto i_n = target_range.first; i_n != target_range.second; ++i_n)
					{
						transitively_add_cycle_n(i_n->second);
					}
				}
			};
			for (auto i_cycle_idx = cycle_idxs.first; i_cycle_idx != cycle_idxs.second; ++i_cycle_idx)
			{
				transitively_add_cycle_n(i_cycle_idx->second);
			}
			
			set<Dwarf_Off> seen_offsets;
			debug_expensive(5, << "--- begin SCC participant nodes:" << std::endl);
			for (auto i_e = scc.begin(); i_e != scc.end(); ++i_e)
			{
				/* Use get_offset to force creation of payloads *before*
				 * we print the "SCC participant nodes" header, to keep
				 * debugging output a bit tidier. */
				auto record_node_idem = [&seen_offsets, p_scc](const iterator_df<type_die>& n) {
					Dwarf_Off off = n.offset_here();
					if (seen_offsets.find(off) != seen_offsets.end()) return;
					seen_offsets.insert(off);
					/* Make the node sticky... otherwise, some nodes in the cycle
					 * might go away and later get re-created, without their SCC
					 * pointer. */
					root_die::ptr_type p = &n.dereference();
					n.root().sticky_dies.insert(make_pair(off, p));
					
					/* Cache the SCC in all participating nodes. */
					if (n->opt_cached_scc && n->opt_cached_scc != p_scc)
					{
						debug_expensive(5, << "This shouldn't happen: DIE object at " << p.get()
							<< ", " << p->summary() << ": "
							<< "already cached SCC, at " 
							<< n->opt_cached_scc->get() << std::endl);
					}
					assert(!n->opt_cached_scc || n->opt_cached_scc == p_scc);
					debug_expensive(5, << "Caching into DIE object at " 
						<< p.get() << "(" << n.summary()
						<< ") address of the SCC object " << p_scc.get() << std::endl);
					(dynamic_cast<type_die *>(p.get()))->opt_cached_scc = p_scc;
					/* We also compute the edges summary as we go. */
					p_scc->edges_summary << abstract_name_for_type(n);
					/* Print the SCC for debugging purposes. */
					debug_expensive(5, << "DIE object at " << p.get()
						<< ": " << n->summary() << std::endl);
				};
				
				record_node_idem(i_e->source());
				record_node_idem(i_e->target());
			}
			debug_expensive(5, << "--- end SCC participant nodes (" << seen_offsets.size() << ")"
				<< std::endl);
			return scc;
		}
		bool type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			
			debug_expensive(2, << "Testing type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl);
			
			return t.tag_here() == get_tag(); // will be refined in subclasses
		}
		bool type_die::equal(iterator_df<type_die> t, 
			const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal
			) const
		{
			set<pair< iterator_df<type_die>, iterator_df<type_die> > > flipped_set;
			auto& r = get_root();
			auto self = find_self();
			
			// iterator equality always implies type equality
			if (self == t) return true;
			
			if (assuming_equal.find(make_pair(self, t)) != assuming_equal.end())
			{
				return true;
			}
			/* If the two iterators share a root, check the cache */
			if (t && &t.root() == &self.root())
			{
				auto found_seq = self.root().equal_to.equal_range(t.offset_here());
				for (auto i_found = found_seq.first; i_found != found_seq.second; ++i_found)
				{
					if (i_found->second.first == self.offset_here())
					{
						return i_found->second.second;
					}
				}
			}
			// we have to find t
			bool ret;
			bool t_may_equal_self;
			bool self_may_equal_t = this->may_equal(t, assuming_equal);
			if (!self_may_equal_t) { ret = false; goto out; }
			
			// we need to flip our set of pairs
			for (auto i_pair = assuming_equal.begin(); i_pair != assuming_equal.end(); ++i_pair)
			{
				flipped_set.insert(make_pair(i_pair->second, i_pair->first));
			}
			
			t_may_equal_self = t->may_equal(self, flipped_set);
			if (!t_may_equal_self) { ret = false; goto out; }
			ret = true;
			// if we're unequal then we should not be the same DIE (equality is reflexive)
		out:
			/* If we're returning false, we'd better not be the same DIE. */
			assert(ret || !t || 
				!(&t.get_root() == &self.get_root() && t.offset_here() == self.offset_here()));
			/* If the two iterators share a root, cache the result */
			if (t && &t.root() == &self.root())
			{
				self.root().equal_to.insert(make_pair(self.offset_here(), make_pair(t.offset_here(), ret)));
				self.root().equal_to.insert(make_pair(t.offset_here(), make_pair(self.offset_here(), ret)));
			}
			
			return ret;
		}
		bool type_die::operator==(const dwarf::core::type_die& t) const
		{ return equal(get_root().find(t.get_offset()), {}); }
/* from base_type_die */
		bool base_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			
			if (get_tag() != t.tag_here()) return false;

			if (get_name() != t.name_here()) return false;

			auto other_base_t = t.as_a<base_type_die>();
			
			bool encoding_equal = get_encoding() == other_base_t->get_encoding();
			if (!encoding_equal) return false;
			
			bool byte_size_equal = get_byte_size() == other_base_t->get_byte_size();
			if (!byte_size_equal) return false;
			
			bool bit_size_equal =
			// presence equal
				(!get_bit_size() == !other_base_t->get_bit_size())
			// and if we have one, it's equal
			&& (!get_bit_size() || *get_bit_size() == *other_base_t->get_bit_size());
			if (!bit_size_equal) return false;
			
			bool bit_offset_equal = 
			// presence equal
				(!get_bit_offset() == !other_base_t->get_bit_offset())
			// and if we have one, it's equal
			&& (!get_bit_offset() || *get_bit_offset() == *other_base_t->get_bit_offset());
			if (!bit_offset_equal) return false;
			
			return true;
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
			return canonical_name_for(find_self().enclosing_cu().spec_here(),
				get_encoding(), *get_byte_size(),
				bit_size_and_offset().first, bit_size_and_offset().second);
		}
/* from ptr_to_member_type_die */
		bool ptr_to_member_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			
			debug(2) << "Testing ptr_to_member_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			auto other_t = t->get_concrete_type();
			auto other_t_as_pmem = other_t.as_a<ptr_to_member_type_die>();
			return other_t.tag_here() == DW_TAG_ptr_to_member_type
				&& other_t_as_pmem->get_type()->may_equal(get_type(), assuming_equal)
				&& other_t_as_pmem->get_containing_type()->may_equal(get_containing_type(),
					assuming_equal);
		}
/* from array_type_die */
		iterator_df<type_die> array_type_die::get_concrete_type() const
		{
			return find_self();
		}
		bool array_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			
			debug(2) << "Testing array_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			if (get_name() != t.name_here()) return false;

			// our subrange type(s) should be equal, if we have them
			auto our_subr_children = children().subseq_of<subrange_type_die>();
			auto their_subr_children = t->children().subseq_of<subrange_type_die>();
			auto i_theirs = their_subr_children.first;
			for (auto i_subr = our_subr_children.first; i_subr != our_subr_children.second;
				++i_subr, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_subr_children.second) return false;
				
				bool types_equal = 
				// presence equal
					(!i_subr->get_type() == !i_subr->get_type())
				// and if we have one, it's equal to theirs
				&& (!i_subr->get_type() || i_subr->get_type()->equal(i_theirs->get_type(), assuming_equal));
				
				if (!types_equal) return false;
			}
			// if they had more, we're unequal
			if (i_theirs != their_subr_children.second) return false;
			
			// our element type(s) should be equal
			bool types_equal = get_type()->equal(t.as_a<array_type_die>()->get_type(), assuming_equal);
			if (!types_equal) return false;
			
			return true;
		}
/* from string_type_die */
		bool string_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			
			debug(2) << "Testing string_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			if (get_name() != t.name_here()) return false;

			// our has-dynamic-lengthness should be equal
			bool dynamic_lengthness_equal = (t.as_a<string_type_die>()->get_string_length()
				== get_string_length());
			if (!dynamic_lengthness_equal) return false;
			// if we don't have dynamic length, any static length should be equal
			if (!get_string_length())
			{
				auto our_opt_byte_size = get_byte_size();
				auto other_opt_byte_size = t.as_a<string_type_die>()->get_byte_size();
				if (our_opt_byte_size != other_opt_byte_size) return false;
			}
			return true;
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
		bool subrange_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			debug(2) << "Testing subrange_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			
			if (get_name() != t.name_here()) return false;

			auto subr_t = t.as_a<subrange_type_die>();
			
			// our base type(s) should be equal
			bool types_equal = 
			// presence equal
				(!get_type() == !subr_t->get_type())
			// if we have one, it should equal theirs
			&& (!get_type() || get_type()->equal(subr_t->get_type(), assuming_equal));
			if (!types_equal) return false;
			
			// our upper bound and lower bound should be equal
			bool lower_bound_equal = get_lower_bound() == subr_t->get_lower_bound();
			if (!lower_bound_equal) return false;
			
			bool upper_bound_equal = get_upper_bound() == subr_t->get_upper_bound();
			if (!upper_bound_equal) return false;
			
			bool count_equal = get_count() == subr_t->get_count();
			if (!count_equal) return false;
			
			return true;
		}
/* from enumeration_type_die */
		bool enumeration_type_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			debug(2) << "Testing enumeration_type_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			
			if (get_name() != t.name_here()) return false;
		
			auto enum_t = t.as_a<enumeration_type_die>();
			
			// our base type(s) should be equal
			bool types_equal = 
			// presence equal
				(!get_type() == !enum_t->get_type())
			// if we have one, it should equal theirs
			&& (!get_type() || get_type()->equal(enum_t->get_type(), assuming_equal));
			if (!types_equal) return false;

			/* We need like-named, like-valued enumerators. */
			auto our_enumerator_children = children().subseq_of<enumerator_die>();
			auto their_enumerator_children = t->children().subseq_of<enumerator_die>();
			auto i_theirs = their_enumerator_children.first;
			for (auto i_memb = our_enumerator_children.first; i_memb != our_enumerator_children.second;
				++i_memb, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_enumerator_children.second) return false;

				if (i_memb->get_name() != i_theirs->get_name()) return false;
				
				if (i_memb->get_const_value() != i_theirs->get_const_value()) return false;
			}
			if (i_theirs != their_enumerator_children.second) return false;
			
			return true;
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
		bool type_chain_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			debug(2) << "Testing type_chain_die::may_equal() (default case)" << endl;
			
			return get_tag() == t.tag_here() && 
				(
					(!get_type() && !t.as_a<type_chain_die>()->get_type())
				||  (get_type() && t.as_a<type_chain_die>()->get_type() && 
					get_type()->equal(t.as_a<type_chain_die>()->get_type(), assuming_equal)));
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
			// HACK: for now, do nothing
			// We should make this more reliable,
			// but it's difficult because overall size of a struct is
			// language- and implementation-dependent.
			return this->type_die::calculate_byte_size();
		}
/* from spec::with_data_members_die */
		bool with_data_members_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			debug(2) << "Testing with_data_members_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			
			if (get_name() != t.name_here()) return false;
			
			/* We need like-named, like-located members. 
			 * GAH. We really need to canonicalise location lists to do this properly. 
			 * That sounds difficult (impossible in general). Nevertheless for most
			 * structs, it's likely to be that they are identical. */
			
			/* Another GAH: recursive structures. What to do about them? */
			
			auto our_member_children = children().subseq_of<member_die>();
			auto their_member_children = t->children().subseq_of<member_die>();
			auto i_theirs = their_member_children.first;
			for (auto i_memb = our_member_children.first; i_memb != our_member_children.second;
				++i_memb, ++i_theirs)
			{
				// if they have fewer, we're unequal
				if (i_theirs == their_member_children.second) return false;
				
				auto this_test_pair = make_pair(
					find_self().as_a<type_die>(),
					t
				);
				auto recursive_test_set = assuming_equal; recursive_test_set.insert(this_test_pair);
				
				bool types_equal = 
				// presence equal
					(!i_memb->get_type() == !i_theirs->get_type())
				// and if we have one, it's equal to theirs
				&& (!i_memb->get_type() || 
				/* RECURSION: here we may get into an infinite loop 
				 * if equality of get_type() depends on our own equality. 
				 * So we use equal_modulo_assumptions()
				 * which is like operator== but doesn't recurse down 
				 * grey (partially opened)  */
					i_memb->get_type()->equal(i_theirs->get_type(), 
						/* Don't recursively begin the test we're already doing */
						recursive_test_set));
				if (!types_equal) return false;
				
				auto loc1 = i_memb->get_data_member_location();
				auto loc2 = i_theirs->get_data_member_location();
				bool locations_equal = 
					loc1 == loc2;
				if (!locations_equal) return false;
				
				// FIXME: test names too? not for now
			}
			// if they had more, we're unequal
			if (i_theirs != their_member_children.second) return false;
			
			return true;
		}
		iterator_base with_data_members_die::find_definition() const
		{
			root_die& r = get_root();
			if (!get_declaration() || !*get_declaration()) 
			{
				/* we are a definition already */
				return find_self();
			}
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
						return i_sib;
					}
				}
			}
		return_no_result:
			debug(2) << "Failed to find definition of declaration " << summary() << endl;
			return iterator_base::END;
		}

		bool variable_die::has_static_storage() const
		{
			// don't bother testing whether we have an enclosing subprogram -- too expensive
			//if (nonconst_this->nearest_enclosing(DW_TAG_subprogram))
			//{
				// we're either a local or a static -- skip if local
				root_die& r = get_root();
				auto attrs = copy_attrs();
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
						std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, 0UL))).tos();
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
		boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned> 
		with_static_location_die::file_relative_intervals(
		
			root_die& r, 
		
			sym_resolver_t sym_resolve,
			void *arg /* = 0 */) const
		{
			encap::attribute_map attrs = d.copy_attrs();
			
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
							assert(calculated_byte_size);
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
						expr_pieces = loclist.loc_for_vaddr(0).pieces();
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
						//	expr_pieces = loclist.begin()->pieces();
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
								this->get_spec(r)).tos();

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
        	auto attrs = copy_attrs();
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
		bool type_describing_subprogram_die::may_equal(iterator_df<type_die> t, const set< pair< iterator_df<type_die>, iterator_df<type_die> > >& assuming_equal) const
		{
			if (!t) return false;
			debug(2) << "Testing type_describing_subprogram_die::may_equal(" << this->summary() << ", " << t->summary() << ")"
				<< " assuming " << assuming_equal.size() << " pairs equal" << endl;
			
			if (get_tag() != t.tag_here()) return false;
			
			if (get_name() != t.name_here()) return false;
			
			auto other_sub_t = t.as_a<type_describing_subprogram_die>();
			
			bool return_types_equal = 
				// presence equal
					(!get_return_type() == !other_sub_t->get_return_type())
				// and if we have one, it's equal to theirs
				&& (!get_return_type() || get_return_type()->equal(other_sub_t->get_return_type(), assuming_equal));
			if (!return_types_equal) return false;
			
			bool variadicness_equal
			 = is_variadic() == other_sub_t->is_variadic();
			if (!variadicness_equal) return false;
			
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
				if (i_theirs == their_fps.second) return false;
				
				bool types_equal = 
				// presence equal
					(!i_fp->get_type() == !i_theirs->get_type())
				// and if we have one, it's equal to theirs
				&& (!i_fp->get_type() || i_fp->get_type()->equal(i_theirs->get_type(), assuming_equal));
				
				if (!types_equal) return false;
				
				bool locations_equal = 
					(i_fp->get_location() == i_theirs->get_location());
				if (!locations_equal) return false;
				
				// FIXME: test names too? not for now
			}
			// if they had more, we're unequal
			if (i_theirs != their_fps.second) return false;
			
			return true;
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
			auto attrs = copy_attrs();
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
			auto attrs = copy_attrs();
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
			auto attrs = copy_attrs();
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
			auto attrs = copy_attrs();
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
				std::stack<Dwarf_Unsigned>(std::deque<Dwarf_Unsigned>(1, object_base_addr))).tos();
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
