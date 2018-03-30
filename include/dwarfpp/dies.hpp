/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * dies.hpp: classes and methods specific to each DIE tag
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_DIES_HPP_
#define DWARFPP_DIES_HPP_

#include <iostream>
#include <utility>
#include <memory>
#include <boost/icl/interval_map.hpp>
#include <boost/algorithm/string.hpp>
#include <unordered_set>
#include <stack>
#include <set>
#include <boost/optional/optional.hpp> // until we are using C++17
#include <srk31/rotate.hpp>

#include "dwarfpp/root.hpp"
#include "dwarfpp/expr.hpp"
#include "dwarfpp/iter.hpp"

namespace dwarf
{
	using std::string;
	using std::vector;
	using std::stack;
	using std::set;
	using std::unordered_set;
	using std::unordered_map;
	using std::endl;
	using boost::optional;
	using std::shared_ptr;
	
	namespace core
	{
/****************************************************************/
/* begin generated ADT includes								 */
/****************************************************************/
#define forward_decl(t) class t ## _die;
#define declare_base(base) base ## _die
#define initialize_base(fragment) /*fragment ## _die(s, std::move(h))*/
#define constructor(fragment, ...) /* "..." is base inits */ \
		fragment ## _die(spec& s, Die&& h) : basic_die(s, std::move(h)) {}
/* #define declare_bases(first_base, ...) first_base , ##__VA_ARGS__ */
#define begin_class(fragment, base_inits, ...) \
	struct fragment ## _die : virtual __VA_ARGS__ { \
	friend struct dwarf_current_factory_t; \
	protected: /* dummy constructor used only to construct dummy instances */\
		fragment ## _die(spec& s) : basic_die(s) {} \
		/* constructor used only to construct in-memory instances */\
		fragment ## _die(spec& s, root_die& r) : basic_die(s, r) {} \
	public: /* main constructor */ \
		constructor(fragment, base_inits /* NOTE: base_inits is expanded via ',' into varargs list */) \
	protected: /* protected constructor that doesn't touch basic_die */ \
		fragment ## _die() {} \
	public: /* extra decls should be public */
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
#define stored_type_address dwarf::encap::attribute_value::address
#define stored_type_refiter iterator_df<basic_die>
#define stored_type_refiter_is_type iterator_df<type_die>
#define stored_type_rangelist dwarf::encap::rangelist

/* This is libdwarf-specific. This might be okay -- 
 * depends if we want a separate class hierarchy for the 
 * encap-style ones (like in encap::). Yes, we probably do.
 * BUT things like type_die should still be the supertype, right?
 * ARGH. 
 * I think we can use virtuality to deal with this in one go. 
 * Define a new encapsulated_die base class (off of basic_die) 
 * which redefines all the get_() stuff in one go. 
 * and then an encapsulated version of any type_die can use
 * virtual inheritance to wire its getters up to those versions. 
 * ARGH: no, we need another round of these macros to enumerate all the getters. 
 */
#define attr_optional(name, stored_t) \
	opt<stored_type_ ## stored_t> get_ ## name() const \
	{ if (has_attr(DW_AT_ ## name)) \
	  {  /* we have to check the form matches our expectations */ \
		 encap::attribute_value a = attr(DW_AT_ ## name); \
		 if (!a.is_ ## stored_t ()) { \
			debug() << "Warning: attribute " #name " of DIE at 0x" << std::hex << get_offset() << std::dec << " not a " #stored_t << endl; \
			return opt<stored_type_ ## stored_t>(); \
		 } else return a.get_ ## stored_t (); \
	  } \
	  else return opt<stored_type_ ## stored_t>(); } \
	opt<stored_type_ ## stored_t> find_ ## name() const \
	{ encap::attribute_value found = find_attr(DW_AT_ ## name); \
	  if (found.get_form() != encap::attribute_value::NO_ATTR) { \
		 if (!found.is_ ## stored_t ()) { \
			debug() << "Warning: attribute " #name " of DIE at 0x" << std::hex << get_offset() << std::dec << " not a " #stored_t << endl; \
			return opt<stored_type_ ## stored_t>(); \
		 } else return found.get_ ## stored_t (); \
	  } else return opt<stored_type_ ## stored_t>(); }

#define super_attr_optional(name, stored_t) attr_optional(name, stored_t)

#define attr_mandatory(name, stored_t) \
	stored_type_ ## stored_t get_ ## name() const \
	{ assert(has_attr(DW_AT_ ## name)); \
	  return attr(DW_AT_ ## name).get_ ## stored_t (); } \
	stored_type_ ## stored_t find_ ## name() const \
	{ encap::attribute_value found = find_attr(DW_AT_ ## name); \
	  assert(found.get_form() != encap::attribute_value::NO_ATTR); \
	  return found.get_ ## stored_t (); }


#define super_attr_mandatory(name, stored_t) attr_mandatory(name, stored_t)
#define child_tag(arg)

	struct type_die; 
	struct with_static_location_die : public virtual basic_die
	{
		struct sym_binding_t 
		{ 
			Dwarf_Off file_relative_start_addr; 
			Dwarf_Unsigned size;
		};
		typedef std::function<sym_binding_t(const std::string&, void *)> sym_resolver_t;
		virtual encap::loclist get_static_location() const;
		opt<Dwarf_Off> spans_addr(
			Dwarf_Addr file_relative_addr,
			root_die& r,
			sym_resolver_t sym_resolve = sym_resolver_t(),
			void *arg = 0) const;
		virtual boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned> 
		file_relative_intervals(
			root_die& r, 
			sym_resolver_t sym_resolve /* = sym_resolver_t() */,
			void *arg /* = 0 */) const;
	};

	struct with_named_children_die : public virtual basic_die
	{
		/* We most most of the resolution stuff into iterator_base, 
		 * but leave this here so that payload implementations can
		 * provide a faster-than-default (i.e. faster than linear search)
		 * way to look up named children (e.g. hash table). */
		virtual 
		iterator_base
		named_child(const std::string& name) const;
	};

/* program_element_die */
begin_class(program_element, base_initializations(basic), declare_base(basic))
		attr_optional(decl_file, unsigned)
		attr_optional(decl_line, unsigned)
		attr_optional(decl_column, unsigned)
		attr_optional(prototyped, flag)
		attr_optional(declaration, flag)
		attr_optional(external, flag)
		attr_optional(visibility, unsigned)
		attr_optional(artificial, flag)
end_class(program_element)
/* type_die */
struct summary_code_word_t
{
	opt<uint32_t> val;

	void invalidate() { val = opt<uint32_t>(); }
	void zero_check()
	{
		if (val && *val == 0)
		{
			debug() << "Warning: output_word value hit zero again." << endl;
			*val = (uint32_t) -1;
		}
	}
	summary_code_word_t& operator<<(uint32_t arg) 
	{
		if (val)
		{
			*val = rotate_left(*val, 8) ^ arg;
			zero_check();
		}
		return *this;
	}
	summary_code_word_t& operator<<(const string& s) 
	{
		if (val)
		{
			for (auto i = s.begin(); i != s.end(); ++i)
			{
				*this << static_cast<uint32_t>(*i);
			}
		}
		zero_check();
		return *this;
	}
	summary_code_word_t& operator<<(opt<uint32_t> o)
	{
		if (!o) 
		{
			val = opt<uint32_t>(); // FIXME: do I want this to "lock" at the invalid state? for incomplete types etc.
			return *this;
		}
		else return this->operator<<(*o);
	}
	summary_code_word_t() : val(0) {}
};
/* To do summary codes efficiently, we need to make them compositional. 
 * This means that given the summary codes for the types we depend on,
 * we should be able to re-use those to compute the code our own type.
 * Unfortunately, type graphs are cyclic, and so the naive approach
 * (which we used to use) of doing a depth-first search and skipping
 * back-edges, is non-compositional: depending on where you start the
 * exploration, different edges are back-edges, and so a depended-on type's
 * code can include/exclude different edges than the depending type does.
 * To avoid this problem, we need to identify the strongly-connected
 * components in the type graph. Then, any type that participates in a
 * cycle can summarised by a pair: its strongly-connected component, and
 * its own identity.
 *
 * Two problems: how do we represent strongly-connected components (SCCs)
 * and how do we identify nodes? To answer the first: an SCC is just a set
 * of edges -- including the back-edges.
 
 * Nodes themselves are tricky. There is no obvious good way to identify
 * nodes in the graph, since we don't want to be sensitive to their DWARF
 * offsets, or even their relative ordering in the DWARF info section.
 * We define an "abstract name". It is definitely not unique -- we are
 * still building up to that, by defining summary codes -- but it should
 * satisfy the property that
 * 
 * - no DIE cycle includes two distinct types with the same abstract name;
 * 
 * - distinct types with the same abstract name will summarise differently,
 *   either because they are not cyclic (and their tree-structures are 
 *   different in the obvious way) or because they participate in cycles
 *   that are distinct *even* after names are abstracted.
 *
 * Whether the last one is true depends a lot on how an SCC, as a set of 
 * edges, gets crumpled down into a summary code.
 */
struct type_edge;
struct type_edge_compare : std::function<bool(const type_edge&, const type_edge&)>
{
	type_edge_compare(); // initializes the std::function with a lambda
};
struct type_scc_t : public set<type_edge, type_edge_compare>
{
	using set::set;
	// FIXME: we could try to maintain this on inserts.
	// if we remove an edge, we have to invalidate it and recompute.
	summary_code_word_t edges_summary;
};
bool types_abstractly_equal(iterator_df<type_die> t1, iterator_df<type_die> t2);
std::ostream& print_type_abstract_name(std::ostream& s, iterator_df<type_die> t);
string abstract_name_for_type(iterator_df<type_die> t);
begin_class(type, base_initializations(initialize_base(program_element)), declare_base(program_element))
		attr_optional(byte_size, unsigned)
		mutable opt<uint32_t> cached_summary_code;
	protected:
		string arbitrary_name() const;
	public:
		mutable optional<shared_ptr<type_scc_t> > opt_cached_scc; // HACK: should be private, but test-scc needs it
		virtual opt<Dwarf_Unsigned> calculate_byte_size() const;
		// virtual bool is_rep_compatible(iterator_df<type_die> arg) const;
		virtual iterator_df<type_die> get_concrete_type() const;
		virtual iterator_df<type_die> get_unqualified_type() const;
		virtual bool abstractly_equals(core::iterator_df<core::type_die> t) const;
		virtual std::ostream& print_abstract_name(std::ostream& s) const ;
		virtual opt<type_scc_t> get_scc() const;
		virtual opt<uint32_t>		 summary_code() const;
		/* FIXME: temporary side-by-side impls while we compare / bug-fix. */
		virtual opt<uint32_t>		 summary_code_using_iterators() const;
		virtual opt<uint32_t>		 summary_code_using_walk_type() const;
		virtual bool may_equal(core::iterator_df<core::type_die> t, 
			const std::set< std::pair<core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const;
		bool equal(core::iterator_df<core::type_die> t, 
			const std::set< std::pair<core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const;
		bool operator==(const dwarf::core::type_die& t) const;
end_class(type)
struct type_edge : public pair< pair<iterator_df<type_die>, iterator_df<program_element_die> >, iterator_df<type_die> >
{
	using pair::pair;
	using pair::operator=;
	const iterator_df<type_die>& source() const { return first.first; }
	iterator_df<type_die>& source() { return first.first; }
	const iterator_df<program_element_die>& label() const { return first.second; }
	iterator_df<program_element_die>& label() { return first.second; }
	const iterator_df<program_element_die>& reason() const { return first.second; }
	iterator_df<program_element_die>& reason() { return first.second; }
	const iterator_df<type_die>& target() const { return second; }
	iterator_df<type_die>& target() { return second; }
};
/* "Type iterators" actually walk *edges* in the type DIE graph, 
 * not types per se: the target DIE of an edge is the iterator's
 * "position" if you dereference it, and the DIE which models the
 * edge itself is the "reason". The source edge is implied by the
 * reason, e.g. for a member_die it's the containing structure;
 * for a formal_parameter_die it's the containing subprogram type,
 * etc.. 
 * If you walk a type, you get the edges in the depth-first traversal,
 * without the back-edges. What if you want to know about the back-edges?
 * Bit of a HACK: I have added the "back_edges_here()" method which
 * gives you a vector of (reason, target) pairs. We use this when
 * computing the SCC of a type DIE.
 *
 * Another wart: we have type_iterator_df which avoids repeat visits
 * to any node; and we have type_iterator_edge_df which will visit
 * the same node as many times as there are distinct-labelled incoming
 * edges to it. */
struct type_iterator_df_base : public iterator_base
{
	typedef type_iterator_df_base self;
	friend class boost::iterator_core_access;

	/* The stack records the grey nodes. The back of the stack
	 * may or may not be our current position.  */
	deque< pair<iterator_df<type_die>, iterator_df<program_element_die> > > m_stack;
	struct black_offsets_set_t : std::unordered_set<Dwarf_Off>
	{
		bool contains(const iterator_base& i) const
		{
			return this->find(i.is_end_position() ? (Dwarf_Off)-1 : i.offset_here())
				!= this->end();
		}
		void insert(const iterator_base& i)
		{
			this->unordered_set::insert(i.is_end_position() ? (Dwarf_Off)-1 : i.offset_here());
		}
		using unordered_set::unordered_set;
	} black_offsets;

	iterator_df<program_element_die> m_reason;
	iterator_base& base_reference()
	{ return static_cast<iterator_base&>(*this); }
	const iterator_base& base() const
	{ return static_cast<const iterator_base&>(*this); }

	type_iterator_df_base() : iterator_base() {}
	type_iterator_df_base(const iterator_base& arg)
	 : iterator_base(arg)   {}// this COPIES so avoid
	type_iterator_df_base(iterator_base&& arg)
	 : iterator_base(std::move(arg)) {} // FIXME: use std::move here, and test
	type_iterator_df_base(const self& arg)
	 : iterator_base(arg), m_stack(arg.m_stack),
	   black_offsets(arg.black_offsets), m_reason(arg.m_reason) {}
	type_iterator_df_base(self&& arg)
	 : iterator_base(arg), m_stack(std::move(arg.m_stack)),
	   black_offsets(std::move(arg.black_offsets)), m_reason(std::move(arg.m_reason)) {}

	bool is_grey(const iterator_base& i) const 
	{
		return !black_offsets.contains(i)
		&& std::find_if(m_stack.begin(), m_stack.end(),
			[i](const pair<iterator_df<type_die>, iterator_df<program_element_die> >& pair)
			{ return pair.first == i; }
		) != m_stack.end();
	}
	bool pos_is_grey() const { return is_grey(base()); }
	
	//bool is_latest_grey(const iterator_base& i) const
	//{ return i == m_stack.back().first; }
	
	//bool pos_is_latest_grey() const { return is_latest_grey(base()); }
	
	// for if we moved (for edge-reflecting purposes) to a non-latest-grey position...
	bool is_black(const iterator_base& i) const
	{ return black_offsets.contains(i); }
	bool pos_is_black() const
	{ return is_black(base()); }
	
	bool pos_is_white() const { return !pos_is_black() && !pos_is_grey(); }
	
	enum colour { WHITE, GREY, BLACK };
	colour colour_of(const iterator_base& i) const
	{ return is_black(i) ? BLACK : is_grey(i) ? GREY : WHITE; }
	colour pos_colour() const { return colour_of(base()); }

	self& operator=(const iterator_base& arg) // assign fresh from an iterator; starts white
	{ this->base_reference() = arg; 
	  m_stack.clear();
	  this->black_offsets.clear(); // = std::move(std::set<Dwarf_Off>());
	  this->m_reason = END;
	  return *this; }
	self& operator=(iterator_base&& arg)
	{ this->base_reference() = std::move(arg); 
	  m_stack.clear();
	  black_offsets.clear(); // = std::move(std::set<Dwarf_Off>());
	  this->m_reason = END;
	  return *this; }
	self& operator=(const self& arg)
	{ self tmp(arg); 
	  std::swap(this->base_reference(), tmp.base_reference());
	  std::swap(this->m_stack, tmp.m_stack);
	  std::swap(this->black_offsets, tmp.black_offsets);
	  std::swap(this->m_reason, tmp.m_reason);
	  return *this; }
	self& operator=(self&& arg)
	{ this->base_reference() = std::move(arg);
	  this->m_stack = std::move(arg.m_stack);
	  this->black_offsets = std::move(arg.black_offsets);
	  this->m_reason = std::move(arg.m_reason);
	  return *this; }

	iterator_df<program_element_die> reason() const
	{ return m_reason; }
protected: /* helpers */
	iterator_df<type_die> source_vertex_for(const iterator_df<type_die>&, const iterator_df<program_element_die>& p) const;
	friend class type_die;
public:
	iterator_df<type_die> source_vertex() const
	{ return source_vertex_for(base(), reason()); }
	iterator_df<program_element_die> edge_label() const { return reason(); }
	pair< pair<iterator_df<type_die>, iterator_df<program_element_die> >, iterator_df<type_die> > as_incoming_edge() const
	{
		return make_pair(
			make_pair(
				source_vertex(),
				edge_label()
			),
			*this
		);
	}
	enum edge_kind { TREE, BACK, CROSS };
	edge_kind incoming_edge_kind() const
	{
		if (pos_is_white()) return TREE;
		if (pos_is_grey()) return BACK;
		assert(pos_is_black()); return CROSS;
		// if we're walking an incoming edge to any node, that node is not white
	}

	/* Since we want to walk "void", which has no representation,
	 * we're only at the end if we're both "no DIE" and "no reason". */
	operator bool() const { return !(!this->base() && !this->reason()); }
	
	/* Primitive operations that our subclasses will use. */
	type_die& dereference() const
	{ return dynamic_cast<type_die&>(this->iterator_base::dereference()); }
	
	/* Nobody implements this. */
	void decrement();

	/* "go deeper" means find a white or black successor of the current node.
	 -- if it's black, we may or may not want to reexplore */
	pair<iterator_df<type_die>, iterator_df<program_element_die> >
	first_outgoing_edge_target() const;
	
	/* "go sideways" means find a white or black successor of the grey 
	 * predecessor of the current node.
	 * -- there is only one grey predecessor, by construction (depth-first)
	 * -- the current node become black when we do this.
	 */
	pair<iterator_df<type_die>, iterator_df<program_element_die> >
	predecessor_node_next_outgoing_edge_target() const;
		/* "backtrack" means find a white or black successor of the next grey predecessor,
	 *  i.e. iterate the go-sideways thing one grey node up the stack. 
	 * The client can open-code this. */
	
	 /* What about a nice interface to back edges and cross edges?
	  * Since the edge iterator will follow these -- it just won't
	  * push them on the stack -- we don't need a separate interface. */
	
	void increment_not_back(bool skip_dependencies = false);
	
	void increment_to_unseen_edge();
	void increment_to_unseen_node();
};

/* The idea of this one is simply to do the same thing as walk_type,
 * for better or worse. */
struct type_iterator_df_walk :  public type_iterator_df_base,
								public boost::iterator_facade<
								   type_iterator_df_walk
								 , type_die
								 , boost::forward_traversal_tag
								 , type_die& /* Reference */
								 , Dwarf_Signed /* difference */
								>
{
	typedef type_iterator_df_walk self;
	friend class boost::iterator_core_access;

	using type_iterator_df_base::type_iterator_df_base;
	
	void increment(bool skip_dependencies = false) { increment_not_back(skip_dependencies); }
	void increment_skipping_dependencies() { increment(true); }
};

struct type_iterator_df_edges : public type_iterator_df_base,
						  public boost::iterator_facade<
							type_iterator_df_edges
						  , type_die
						  , boost::forward_traversal_tag
						  , type_die& /* Reference */
						  , Dwarf_Signed /* difference */
						  >
{
	typedef type_iterator_df_edges self;
	friend class boost::iterator_core_access;
	
	type_iterator_df_edges(const iterator_base& arg)
	{ base_reference() = arg; m_reason = END; }
	type_iterator_df_edges(iterator_base&& arg)
	{ base_reference() = std::move(arg); m_reason = END; }
	type_iterator_df_edges(const self& arg)
	 : type_iterator_df_base(arg) {}
	type_iterator_df_edges(self&& arg) : type_iterator_df_base(std::move(arg)) {}

	void increment();
};

struct type_iterator_outgoing_edges : public type_iterator_df_base,
				  public boost::iterator_facade<
					type_iterator_outgoing_edges
				  , type_die
				  , boost::forward_traversal_tag
				  , type_die& /* Reference */
				  , Dwarf_Signed /* difference */
				  >
{
	typedef type_iterator_outgoing_edges self;
	typedef type_iterator_df_edges super;
private:
	void init()
	{
		m_stack.clear();
		black_offsets.clear();
		// we have nowhere to pop to, so if no outgoing edges, will hit END
		if (this->operator bool()) this->increment_to_unseen_edge();
	}
public:
	type_iterator_outgoing_edges()
	: type_iterator_df_base() {}
	explicit type_iterator_outgoing_edges(const type_iterator_df_base& arg)
	: type_iterator_df_base(arg) { init(); }
	explicit type_iterator_outgoing_edges(type_iterator_df_base&& arg)
	: type_iterator_df_base(std::move(arg)) { init(); }
	type_iterator_outgoing_edges(const self& arg)
	 : type_iterator_df_base(arg) { /* don't init */ }
	type_iterator_outgoing_edges(self&& arg) : type_iterator_df_base(std::move(arg))
	{ /* don't init */ }
	explicit type_iterator_outgoing_edges(
		const deque< pair<iterator_df<type_die>, iterator_df<program_element_die> > >& stack)
	{ /* This constructor means 
	   * "the stack position is black; start from the predecessor's next outgoing edge". */
		this->base_reference() = stack.back().first;
		this->m_reason = stack.back().second;
		this->m_stack.clear();
		// don't need stack for next outgoing edge
		auto found_next = predecessor_node_next_outgoing_edge_target();
		if (found_next.first || found_next.second)
		{
			this->base_reference() = std::move(found_next.first);
			this->m_reason = std::move(found_next.second);
		}
		else
		{
			this->base_reference() = END;
			this->m_reason = END;
		}
	}
	
	// assigning from any DF iterator will walk its children?
	// NO; this would be confusing (changes position)
	// we made the constructors above "explicit" for the same reason
// 	self& operator=(const type_iterator_df_base& arg)
// 	{ static_cast<type_iterator_df_base&>(*this)
// 	  = static_cast<const type_iterator_df_base&>(arg);
// 	  init();
// 	  return *this; }
// 	self& operator=(type_iterator_df_base&& arg)
// 	{ static_cast<type_iterator_df_base&&>(*this)
// 	  = static_cast<type_iterator_df_base&&>(arg);
// 	  init();
// 	  return *this; }
	// assigning from ourselves will just copy-assign -- OK
	self& operator=(const self& arg)
	{ static_cast<type_iterator_df_base&>(*this)
	  = static_cast<const type_iterator_df_base&>(arg);
	  return *this; }
	self& operator=(self&& arg)
	{ static_cast<type_iterator_df_base&&>(*this)
	  = static_cast<type_iterator_df_base&&>(arg);
	  return *this; }


	//type_iterator_outgoing_edges() : type_iterator_df_edges() { init(); }
	//type_iterator_outgoing_edges(const type_iterator_df_base& arg)
	// : type_iterator_df_edges(arg)  { init(); }// this COPIES so avoid
	//type_iterator_outgoing_edges(type_iterator_df_edges&& arg)
	// : type_iterator_df_edges(std::move(arg)) { init(); }
	//type_iterator_outgoing_edges(const self& arg)
	// : type_iterator_df_edges(arg) { /* don't init */ }
	//type_iterator_outgoing_edges(self&& arg)
	// : type_iterator_df_edges(std::move(arg)) { /* don't init */ }

	/* Basic idea: we're an iterator that yields each immediate successor
	 * of the start type_die. Our initial stack consists of that DIE
	 * and the null reason. The first thing we do is increment our position,
	 * moving us to the first outgoing edge's target, if any. */

	void increment()
	{
		auto found_next = predecessor_node_next_outgoing_edge_target();
		this->base_reference() = std::move(found_next.first);
		this->m_reason = std::move(found_next.second);
	}
};

/* type_set and related utilities. */
size_t type_hash_fn(iterator_df<type_die> t);
bool type_eq_fn(iterator_df<type_die> t1, iterator_df<type_die> t2);
struct type_set : public unordered_set< 
	/* Key */   iterator_df<type_die>,
	/* Hash */  std::function<size_t(iterator_df<type_die>)>,
	/* Equal */ std::function<bool(iterator_df<type_die> t1, iterator_df<type_die> t2)>
>
{
	type_set() : unordered_set({}, 0, type_hash_fn, type_eq_fn) {}
};
typedef set< opt<Dwarf_Off> > dieloc_set;
template <typename Value>
struct type_map : public unordered_map< 
	/* Key */   iterator_df<type_die>,
	Value,
	/* Hash */  std::function<size_t(iterator_df<type_die>)>,
	/* Equal */ std::function<bool(iterator_df<type_die> t1, iterator_df<type_die> t2)>
>
{
	typedef unordered_map< 
		/* Key */   iterator_df<type_die>,
		Value,
		/* Hash */  std::function<size_t(iterator_df<type_die>)>,
		/* Equal */ std::function<bool(iterator_df<type_die> t1, iterator_df<type_die> t2)>
	> super;
	type_map() : super({}, 0, type_hash_fn, type_eq_fn) {}
};
void walk_type(core::iterator_df<core::type_die> t, 
	core::iterator_df<core::program_element_die> origin, 
	const std::function<bool(core::iterator_df<core::type_die>, core::iterator_df<core::program_element_die>)>& pre_f,
	const std::function<void(core::iterator_df<core::type_die>, core::iterator_df<core::program_element_die>)>& post_f
	 = std::function<void(core::iterator_df<core::type_die>, core::iterator_df<core::program_element_die>)>(),
	const dieloc_set& currently_walking = dieloc_set());
/* with_type_describing_layout_die */
	struct with_type_describing_layout_die : public virtual program_element_die
	{
		virtual opt<iterator_df<type_die> > get_type() const = 0;
		virtual opt<iterator_df<type_die> > find_type() const = 0;
	};
/* with_dynamic_location_die */
	struct with_dynamic_location_die : public virtual with_type_describing_layout_die
	{
		virtual opt<Dwarf_Off> spans_addr(
				Dwarf_Addr absolute_addr,
				Dwarf_Signed instantiating_instance_addr, 
				root_die& r, 
				Dwarf_Off dieset_relative_ip,
				dwarf::expr::regs *p_regs = 0) const = 0;
		/* We define two variants of the spans_addr logic: 
		 * one suitable for stack-based locations (fp/variable)
		 * and another for object-based locations (member/inheritance)
		 * and each derived class should pick one! */
	protected:
		opt<Dwarf_Off> spans_stack_addr(
				Dwarf_Addr absolute_addr,
				Dwarf_Signed instantiating_instance_addr,
				root_die& r,
				Dwarf_Off dieset_relative_ip,
				dwarf::expr::regs *p_regs = 0) const;
		opt<Dwarf_Off> spans_addr_in_object(
				Dwarf_Addr absolute_addr,
				Dwarf_Signed instantiating_instance_addr,
				root_die& r, 
				Dwarf_Off dieset_relative_ip,
				dwarf::expr::regs *p_regs = 0) const;
	public:
		virtual iterator_df<program_element_die> 
		get_instantiating_definition() const;

		virtual Dwarf_Addr calculate_addr(
			Dwarf_Addr instantiating_instance_location,
			root_die& r,
			Dwarf_Off dieset_relative_ip,
			dwarf::expr::regs *p_regs = 0) const = 0;

		/** This gets a location list describing the location of the thing, 
			assuming that the instantiating_instance_location has been pushed
			onto the operand stack. */
		virtual encap::loclist get_dynamic_location() const = 0;
	protected:
		/* ditto */
		virtual Dwarf_Addr calculate_addr_on_stack(
			Dwarf_Addr instantiating_instance_location,
			root_die& r,
			Dwarf_Off dieset_relative_ip,
			dwarf::expr::regs *p_regs = 0) const;
		virtual Dwarf_Addr calculate_addr_in_object(
			Dwarf_Addr instantiating_instance_location,
			root_die& r, 
			Dwarf_Off dieset_relative_ip,
			dwarf::expr::regs *p_regs = 0) const;
	public:
		virtual bool location_requires_object_base() const = 0; 

		/* virtual Dwarf_Addr calculate_addr(
			Dwarf_Signed frame_base_addr,
			Dwarf_Off dieset_relative_ip,
			dwarf::expr::regs *p_regs = 0) const;*/
	};
#define has_stack_based_location \
	bool location_requires_object_base() const { return false; } \
	opt<Dwarf_Off> spans_addr( \
					Dwarf_Addr aa, \
					Dwarf_Signed fb, \
					root_die& r, \
					Dwarf_Off dr_ip, \
					dwarf::expr::regs *p_regs) const \
					{ return spans_stack_addr(aa, fb, r, dr_ip, p_regs); } \
	Dwarf_Addr calculate_addr( \
				Dwarf_Addr fb, \
				root_die& r, \
				Dwarf_Off dr_ip, \
				dwarf::expr::regs *p_regs = 0) const \
				{ return calculate_addr_on_stack(fb, r, dr_ip, p_regs); } \
	encap::loclist get_dynamic_location() const;
#define has_object_based_location \
	bool location_requires_object_base() const { return true; } \
	opt<Dwarf_Off> spans_addr( \
					Dwarf_Addr aa, \
					Dwarf_Signed io, \
					root_die& r, \
					Dwarf_Off dr_ip, \
					dwarf::expr::regs *p_regs) const \
					{ return spans_addr_in_object(aa, io, r, dr_ip, p_regs); } \
	Dwarf_Addr calculate_addr( \
				Dwarf_Addr io, \
				root_die& r, \
				Dwarf_Off dr_ip, \
				dwarf::expr::regs *p_regs = 0) const \
				{ return calculate_addr_in_object(io, r, dr_ip, p_regs); } \
	encap::loclist get_dynamic_location() const;
/* data_member_die */
begin_class(data_member, base_initializations(initialize_base(with_dynamic_location)), declare_base(with_dynamic_location))
	has_object_based_location
	attr_optional(data_member_location, loclist)
	virtual iterator_df<type_die> find_or_create_type_handling_bitfields() const;
	/** This gets an offset in an enclosing object. */
	virtual opt<Dwarf_Unsigned> byte_offset_in_enclosing_type(bool assume_packed_if_no_location = false) const;
end_class(data_member)
/* type_chain_die */
begin_class(type_chain, base_initializations(initialize_base(type)), declare_base(type))
		attr_optional(type, refiter_is_type)
		opt<Dwarf_Unsigned> calculate_byte_size() const;
		iterator_df<type_die> get_concrete_type() const;
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const;
end_class(type_chain)
/* type_describing_subprogram_die */
begin_class(type_describing_subprogram, base_initializations(initialize_base(type)), declare_base(type))
		attr_optional(type, refiter_is_type)
		virtual iterator_df<type_die> get_return_type() const = 0;
		virtual bool is_variadic() const;
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const;
		bool abstractly_equals(iterator_df<type_die> t) const;
		std::ostream& print_abstract_name(std::ostream& s) const;
end_class(type_describing_subprogram)
/* address_holding_type_die */
begin_class(address_holding_type, base_initializations(initialize_base(type_chain)), declare_base(type_chain))
		attr_optional(address_class, unsigned)
		iterator_df<type_die> get_concrete_type() const;
		opt<Dwarf_Unsigned> calculate_byte_size() const;
		bool abstractly_equals(iterator_df<type_die> t) const;
		std::ostream& print_abstract_name(std::ostream& s) const;
end_class(type_chain)
/* qualified_type_die */
begin_class(qualified_type, base_initializations(initialize_base(type_chain)), declare_base(type_chain))
		iterator_df<type_die> get_unqualified_type() const;
end_class(qualified_type)
/* with_data_members_die */
begin_class(with_data_members, base_initializations(initialize_base(type)), declare_base(type))
		child_tag(member)
		iterator_base find_definition() const; // for turning declarations into defns
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const; 
		bool abstractly_equals(iterator_df<type_die> t) const;
		std::ostream& print_abstract_name(std::ostream& s) const;
end_class(with_data_members)

#define extra_decls_subprogram \
		opt< std::pair<Dwarf_Off, iterator_df<with_dynamic_location_die> > > \
		spans_addr_in_frame_locals_or_args( \
					Dwarf_Addr absolute_addr, \
					root_die& r, \
					Dwarf_Off dieset_relative_ip, \
					Dwarf_Signed *out_frame_base, \
					dwarf::expr::regs *p_regs = 0) const; \
		iterator_df<type_die> get_return_type() const;
#define extra_decls_variable \
		bool has_static_storage() const; \
		has_stack_based_location
#define extra_decls_formal_parameter \
		has_stack_based_location
#define extra_decls_array_type \
		opt<Dwarf_Unsigned> element_count() const; \
		vector<opt<Dwarf_Unsigned> > dimension_element_counts() const; \
		opt<Dwarf_Unsigned> calculate_byte_size() const; \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */ \
		iterator_df<type_die> ultimate_element_type() const; \
		opt<Dwarf_Unsigned> ultimate_element_count() const; \
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const; \
		iterator_df<type_die> get_concrete_type() const; \
		bool abstractly_equals(iterator_df<type_die> t) const; \
		std::ostream& print_abstract_name(std::ostream& s) const;
#define extra_decls_string_type \
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const; \
		opt<Dwarf_Unsigned> fixed_length_in_bytes() const; \
		opt<encap::loclist> dynamic_length_in_bytes() const; \
		bool abstractly_equals(iterator_df<type_die> t) const; \
		std::ostream& print_abstract_name(std::ostream& s) const; \
		opt<Dwarf_Unsigned> calculate_byte_size() const;
#define extra_decls_pointer_type \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_reference_type \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_base_type \
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const; \
		opt<Dwarf_Unsigned> calculate_byte_size() const; \
		pair<Dwarf_Unsigned, Dwarf_Unsigned> bit_size_and_offset() const; \
		bool is_bitfield_type() const; \
		bool abstractly_equals(iterator_df<type_die> t) const; \
		std::ostream& print_abstract_name(std::ostream& s) const; \
		string get_canonical_name() const; \
		static string canonical_name_for(spec& spec, unsigned encoding, \
			unsigned byte_size, unsigned bit_size, unsigned bit_offset);
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_structure_type \
		opt<Dwarf_Unsigned> calculate_byte_size() const; \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_union_type \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_class_type \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_enumeration_type \
		bool abstractly_equals(iterator_df<type_die> t) const; \
		std::ostream& print_abstract_name(std::ostream& s) const; \
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const; \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_subrange_type \
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const; \
		bool abstractly_equals(iterator_df<type_die> t) const; \
		std::ostream& print_abstract_name(std::ostream& s) const;
#define extra_decls_set_type \
		bool abstractly_equals(iterator_df<type_die> t) const; \
		std::ostream& print_abstract_name(std::ostream& s) const;
#define extra_decls_file_type \
		bool abstractly_equals(iterator_df<type_die> t) const; \
		std::ostream& print_abstract_name(std::ostream& s) const;
#define extra_decls_ptr_to_member_type \
		bool abstractly_equals(iterator_df<type_die> t) const; \
		std::ostream& print_abstract_name(std::ostream& s) const; \
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const;
#define extra_decls_member \
		iterator_df<type_die> find_or_create_type_handling_bitfields() const;
#define extra_decls_subroutine_type \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */ \
		core::iterator_df<core::type_die> get_return_type() const; 

#define extra_decls_compile_unit \
/* We define fields and getters for the per-CU source file info */ \
/* (which is *separate* from decl_file and decl_line attributes). */ \
public: \
inline std::string source_file_name(unsigned o) const; \
opt<std::string> source_file_fq_pathname(unsigned o) const; \
inline unsigned source_file_count() const; \
/* We define fields and getters for the per-CU info (NOT attributes) */ \
/* available from libdwarf. These will be filled in by root_die::make_payload(). */ \
protected: \
Dwarf_Unsigned cu_header_length; \
Dwarf_Half version_stamp; \
Dwarf_Unsigned abbrev_offset; \
Dwarf_Half address_size; \
Dwarf_Half offset_size; \
Dwarf_Half extension_size; \
Dwarf_Unsigned next_cu_header; \
public: \
Dwarf_Unsigned get_cu_header_length() const { return cu_header_length; } \
Dwarf_Half get_version_stamp() const { return version_stamp; } \
Dwarf_Unsigned get_abbrev_offset() const { return abbrev_offset; } \
Dwarf_Half get_address_size() const { return address_size; } \
Dwarf_Half get_offset_size() const { return offset_size; } \
Dwarf_Half get_extension_size() const { return extension_size; } \
Dwarf_Unsigned get_next_cu_header() const { return next_cu_header; } \
opt<Dwarf_Unsigned> implicit_array_base() const; \
mutable iterator_df<type_die> cached_implicit_enum_base_type; \
iterator_df<type_die> implicit_enum_base_type() const; \
mutable iterator_df<type_die> cached_implicit_subrange_base_type; \
iterator_df<type_die> implicit_subrange_base_type() const; \
encap::rangelist normalize_rangelist(const encap::rangelist& rangelist) const; \
bool is_generic_pointee_type(iterator_df<type_die> t) const; \
friend class iterator_base; \
friend class factory; 

#include "dwarf-current-adt.h"

#undef extra_decls_subprogram
#undef extra_decls_compile_unit
#undef extra_decls_array_type
#undef extra_decls_string_type
#undef extra_decls_variable
#undef extra_decls_structure_type
#undef extra_decls_union_type
#undef extra_decls_class_type
#undef extra_decls_enumeration_type
#undef extra_decls_subroutine_type
#undef extra_decls_member
#undef extra_decls_inheritance
#undef extra_decls_pointer_type
#undef extra_decls_reference_type
#undef extra_decls_base_type
#undef extra_decls_formal_parameter
#undef extra_decls_set_type
#undef extra_decls_file_type
#undef extra_decls_ptr_to_member_type
#undef extra_decls_unspecified_type
#undef extra_decls_subrange_type

#undef has_stack_based_location
#undef has_object_based_location

#undef forward_decl
#undef declare_base
#undef declare_bases
#undef base_fragment
#undef initialize_base
#undef constructor
#undef begin_class
#undef base_initializations
#undef declare_base
#undef end_class
#undef stored_type
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
#undef stored_type_refiter
#undef stored_type_refiter_is_type
#undef stored_type_rangelist
#undef attr_optional
#undef attr_mandatory
#undef super_attr_optional
#undef super_attr_mandatory
#undef child_tag

/****************************************************************/
/* end generated ADT includes								   */
/****************************************************************/
	}
}
#endif
