/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * dies.hpp: classes and methods specific to each DIE tag
 *
 * Copyright (c) 2008--17, Stephen Kell.
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
		virtual encap::loclist get_static_location() const;
		opt<Dwarf_Off> spans_addr(
			Dwarf_Addr file_relative_addr,
			root_die& r,
			sym_binding_t (*sym_resolve)(const std::string& sym, void *arg) = 0, 
			void *arg = 0) const;
		virtual boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned> 
		file_relative_intervals(
			root_die& r, 
			sym_binding_t (*sym_resolve)(const std::string& sym, void *arg), 
			void *arg /* = 0 */) const;
	};

	struct with_named_children_die : public virtual basic_die
	{
		/* We most most of the resolution stuff into iterator_base, 
		 * but leave this here so that payload implementations can
		 * provide a faster-than-default (i.e. faster than linear search)
		 * way to look up named children (e.g. hash table). */
		virtual 
		inline iterator_base
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
			val = opt<uint32_t>();
			return *this;
		}
		else return this->operator<<(*o);
	}
	summary_code_word_t() : val(0) {}
};
begin_class(type, base_initializations(initialize_base(program_element)), declare_base(program_element))
		attr_optional(byte_size, unsigned)
		mutable opt<uint32_t> cached_summary_code;
		virtual opt<Dwarf_Unsigned> calculate_byte_size() const;
		// virtual bool is_rep_compatible(iterator_df<type_die> arg) const;
		virtual iterator_df<type_die> get_concrete_type() const;
		virtual iterator_df<type_die> get_unqualified_type() const;
		virtual opt<uint32_t>		 summary_code() const;
		virtual bool may_equal(core::iterator_df<core::type_die> t, 
			const std::set< std::pair<core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const;
		bool equal(core::iterator_df<core::type_die> t, 
			const std::set< std::pair<core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const;
		bool operator==(const dwarf::core::type_die& t) const;
end_class(type)
		struct type_iterator_df : public iterator_base,
								  public boost::iterator_facade<
								    type_iterator_df
								  , type_die
								  , boost::forward_traversal_tag
								  , type_die& /* Reference */
								  , Dwarf_Signed /* difference */
								  >
		{
			typedef type_iterator_df self;
			friend class boost::iterator_core_access;

			// extra state needed!
			// FIXME: we have to decide whether our current position should be 
			// on the stack or not.
			// "On" is slightly nicer for uniformity.
			// "Not on" means we can avoid copying the iterator in trivial cases,
			// but we have to store the present "reason" separately.
			// We go for "on" for now.
			deque< pair<iterator_base, iterator_base> > m_stack;
			
			iterator_base& base_reference()
			{ return static_cast<iterator_base&>(*this); }
			const iterator_base& base() const
			{ return static_cast<const iterator_base&>(*this); }
			
			type_iterator_df() : iterator_base() {}
			type_iterator_df(const iterator_base& arg)
			 : iterator_base(arg) { m_stack.push_back(make_pair(base(), END)); }// this COPIES so avoid
			type_iterator_df(iterator_base&& arg)
			 : iterator_base(arg) { m_stack.push_back(make_pair(base(), END)); } // FIXME: use std::move here, and test
			type_iterator_df(const self& arg)
			 : iterator_base(arg), m_stack(arg.m_stack) {}
			type_iterator_df(self&& arg)
			 : iterator_base(arg), m_stack(std::move(arg.m_stack)) {}
			
			self& operator=(const iterator_base& arg)
			{ this->base_reference() = arg; 
			  while (!this->m_stack.empty()) this->m_stack.pop_back();
			  this->m_stack.push_back(make_pair(base(), END));
			  return *this; }
			self& operator=(iterator_base&& arg)
			{ this->base_reference() = std::move(arg); 
			  while (!this->m_stack.empty()) this->m_stack.pop_back();
			  this->m_stack.push_back(make_pair(base(), END));
			  return *this; }
			self& operator=(const self& arg)
			{ self tmp(arg); 
			  std::swap(this->base_reference(), tmp.base_reference());
			  std::swap(this->m_stack, tmp.m_stack);
			  return *this; }
			self& operator=(self&& arg)
			{ this->base_reference() = std::move(arg);
			  this->m_stack = std::move(arg.m_stack);
			  return *this; }

			iterator_df<program_element_die> reason() const 
			{ if (!m_stack.empty()) return this->m_stack.back().second; else return END; }
			void increment(bool skip_dependencies = false);
			void increment_skipping_dependencies();
			void decrement();
			type_die& dereference() const
			{ return dynamic_cast<type_die&>(this->iterator_base::dereference()); }
			
			/* Since we want to walk "void", which has no representation,
			 * we're only at the end if we're both "no DIE" and "no reason". */
			operator bool() const { return !(!this->base() && !this->reason()); }
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
			
		/** This gets an offset in an enclosing object. NOTE that it's only 
			defined for members and inheritances (and not even all of those), 
			but it's here for convenience. */
		virtual opt<Dwarf_Unsigned> byte_offset_in_enclosing_type(bool assume_packed_if_no_location = false) const;

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
end_class(type_describing_subprogram)
/* address_holding_type_die */
begin_class(address_holding_type, base_initializations(initialize_base(type_chain)), declare_base(type_chain))
		attr_optional(address_class, unsigned)
		iterator_df<type_die> get_concrete_type() const;
		opt<Dwarf_Unsigned> calculate_byte_size() const;
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
		iterator_df<type_die> get_concrete_type() const;
#define extra_decls_string_type \
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const; \
		opt<Dwarf_Unsigned> fixed_length_in_bytes() const; \
		opt<encap::loclist> dynamic_length_in_bytes() const; \
		opt<Dwarf_Unsigned> calculate_byte_size() const;
#define extra_decls_pointer_type \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_reference_type \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_base_type \
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const; \
		opt<Dwarf_Unsigned> calculate_byte_size() const; \
		pair<Dwarf_Unsigned, Dwarf_Unsigned> bit_size_and_offset() const; \
		bool is_bitfield_type() const;
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_structure_type \
		opt<Dwarf_Unsigned> calculate_byte_size() const; \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_union_type \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_class_type \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_enumeration_type \
		bool may_equal(core::iterator_df<core::type_die> t, const std::set< std::pair< core::iterator_df<core::type_die>, core::iterator_df<core::type_die> > >& assuming_equal) const; \
		/* bool is_rep_compatible(iterator_df<type_die> arg) const; */
#define extra_decls_subrange_type \
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
inline opt<std::string> source_file_fq_pathname(unsigned o) const; \
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
