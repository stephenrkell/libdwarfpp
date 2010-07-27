#ifndef __DWARFPP_UTIL_H
#define __DWARFPP_UTIL_H

#include <boost/optional.hpp>
#include "dwarfpp.hpp"
#include <map>
#include <string>
#include <iostream>
#include <functional>

namespace srk31 { class indenting_ostream; }
namespace dwarf
{
	class abi_information;
	namespace walker
	{
		// get these out of the way early
		template <typename Arg> struct func1_true : std::unary_function<Arg, bool> 
		{
		public:
			bool operator()(Arg a) { return true; }
		};

		template <typename Arg1, typename Arg2> struct func2_true : std::binary_function<Arg1, Arg2, bool> 
		{
		public:
			bool operator()(Arg1 a1, Arg2 a2) { return true; }
		};
		
		template <typename Arg> struct func1_do_nothing : std::unary_function<Arg, void>
		{
		public:
			void operator()(Arg a) {}
		};
		
		// forward declarations
		template <typename T> struct capture_func;
		class select_until_captured;
		
		struct walker : std::binary_function<dieset&, Dwarf_Off, void>
		{
			virtual void operator()(dieset& dies, Dwarf_Off off) = 0;
		};
		
		// the following typedefs are a convenience only -- you don't need to derive from them
		typedef std::unary_function<encap::die&, bool> matcher;
		typedef std::unary_function<Dwarf_Off, void> action;
		typedef std::binary_function<encap::die&, walker&, bool> selector;
		
		struct always_match : matcher
		{
			bool operator()(encap::die& d) { return true; }
		};
		struct do_nothing : action
		{
			void operator()(Dwarf_Off o) { return; }
		};
		struct always_select : selector
		{
			bool operator()(encap::die& d, walker& w) { return true; }
		};

		class walker_having_depth : public walker
		{
		protected:
			int m_depth;
		public:
			walker_having_depth() : m_depth(0) {}
			int depth() const { return m_depth; }
		};

		struct depth_limited_selector 
			: std::binary_function<encap::die&, walker_having_depth&, bool>
		{
			int m_max_depth;
		public:
			depth_limited_selector(int max_depth) 
			: m_max_depth(max_depth) {}
			bool operator()(encap::die& d, walker_having_depth& w)
			{
				if (w.depth() > m_max_depth) return false;
				else return true;
			}
		};
		
		template <	typename A = do_nothing, 
					typename M = always_match, 
					typename S = always_select >
		class depthfirst_walker : public walker_having_depth
		{
			int m_depth;
			M& m_matcher;
			A& m_action;
			S& m_selector;
		public:
			depthfirst_walker(
				const A& act = A(),
				const M& match = M(), 
				const S& select_children = S() )
			: 													// const_cast here is necessary
				 m_matcher(const_cast<M&>(match)), 				// in order that we can call
				 m_action(const_cast<A&>(act)), 				// definitions of operator()
				 m_selector(const_cast<S&>(select_children)) {} // which were not declared () const;
			
			void operator()(dwarf::dieset& dies, Dwarf_Off off)
			{
				if (m_matcher(dies[off])) m_action(off);

				++m_depth;
				for (die_off_list::iterator iter = dies[off].children().begin();
					iter != dies[off].children().end();
					iter++)
				{
					if (m_selector(dies[*iter], *this)) (*this)(dies, *iter);
				}
				--m_depth;
			}
		};

		class walker_having_height : public walker
		{
		protected:
			int m_height;
		public:
			walker_having_height() : m_height(0) {}
			int height() const { return m_height; }
		};


		template <	typename A = func1_do_nothing<Dwarf_Off>, 
					typename M = func1_true<encap::die&>, 
					typename S = func2_true<encap::die&, walker&> >
		class siblings_upward_walker : public walker_having_height
		{
			int m_height;
			M& m_matcher; // CARE: if these were created on the stack, 
			A& m_action;  // callers must ensure that the upward_walker object, or copies thereof,
			S& m_selector; // don't outlive these!
		public:
			typedef siblings_upward_walker<capture_func<Dwarf_Off>, M, select_until_captured> 
				find_first_match_walker_t;
			siblings_upward_walker(
				const A& act = A(),
				const M& match = M(), 
				const S& select_siblings = S() )
				 : m_height(0), 								// const_cast here is necessary
				 m_matcher(const_cast<M&>(match)), 				// in order that we can call
				 m_action(const_cast<A&>(act)), 				// definitions of operator()
				 m_selector(const_cast<S&>(select_siblings)) {} // which were not declared () const;
			int height() const { return m_height; }
		
			void operator() (dwarf::dieset& dies, Dwarf_Off off)
			{
				// try us first -- we're closer than our siblings
				//std::cerr << "siblings_upward_walker: walking offset 0x" << std::hex << off << std::dec << std::endl;
				//std::cerr << "matcher says " << m_matcher(dies[off]) << std::endl;
				//std::cerr << "matcher is at " << &m_matcher << std::endl;
				if (m_matcher(dies[off]))
				{
					//std::cerr << "siblings_upward_walker: firing action" << std::endl;
					m_action(off);
				}

				// if this is the termination case (root), we're finished
				if (off == 0UL)
				{
					 return;
				}
				else
				{
					// try siblings, skipping us
					for (die_off_list::iterator iter = dies[dies[off].parent()].children().begin();
						iter != dies[dies[off].parent()].children().end();
						iter++)
					{
						if (*iter == off) continue; // skip us -- we've already been tried
						//std::cerr << "siblings_upward_walker: trying sibling at 0x" << std::hex << *iter << std::dec << std::endl;
						if (m_selector(dies[*iter], *this) && m_matcher(dies[*iter])) m_action(off);
					}

					// recurse up to parent
					++m_height;
					(*this)(dies, dies[off].parent());
				}
			}
		};

		template <typename T> struct capture_func {
			boost::optional<T> captured;
			void operator()(T o) { captured = boost::optional<T>(o); }
		};

		//template <typename T/*, typename W*/>	
//		boost::optional<Dwarf_Off> find_first_match(
//			dwarf::dieset& dies, Dwarf_Off off, walker& walk);
		//template <typename M, typename Walker>
		//boost::optional<Dwarf_Off> find_first_match(dieset& dies, Dwarf_Off start,
		//	const M& matcher);

		template <typename Pred>
		class tag_matcher : public matcher
		{			
		private:
			const Pred m_pred; // we copy the pred because it may have been created on the stack
		public:
			tag_matcher(const Pred& pred) : m_pred(pred) {}
			bool operator()(const encap::die& d) const { return m_pred(d.tag()); }
		};

		typedef tag_matcher<std::binder2nd<std::equal_to<Dwarf_Half> > >  
			tag_equal_to_matcher_t;
		tag_equal_to_matcher_t matcher_for_tag_equal_to(Dwarf_Half tag);
						
		typedef tag_matcher<std::pointer_to_unary_function<Dwarf_Half, bool> >
			tag_satisfying_func_matcher_t;
		tag_satisfying_func_matcher_t matcher_for_tag_satisfying_func(bool (*func)(Dwarf_Half));					

		template <typename Pred>
		class offset_matcher : public matcher
		{			
		private:
			const Pred m_pred; // we copy the pred because it may have been created on the stack
		public:
			offset_matcher(const Pred& pred) : m_pred(pred) {}
			bool operator()(const encap::die& d) const { return m_pred(d.offset()); }
		};
		typedef offset_matcher<std::binder2nd<std::greater_equal<Dwarf_Off> > >  
			offset_greater_equal_matcher_t;
		offset_greater_equal_matcher_t matcher_for_offset_greater_equal(Dwarf_Off off);

		template <typename Pred>
		class name_matcher : public matcher
		{			
		private:
			const Pred m_pred; // we copy the pred because it may have been created on the stack
		public:
			name_matcher(const Pred& pred) : m_pred(pred) {}
			bool operator()(const encap::die& d) const { return d.has_attr(DW_AT_name) && 
				m_pred(d[DW_AT_name].get_string()); }
		};
		typedef name_matcher<std::binder2nd<std::equal_to<std::string> > >  
			name_equal_to_matcher_t;
		name_equal_to_matcher_t matcher_for_name_equal_to(std::string s);
		
		class has_attribute_matcher : public matcher
		{
			Dwarf_Half m_attr;
		public:
			has_attribute_matcher(Dwarf_Half attr) : m_attr(attr) {}
			bool operator()(encap::die& d) { return d.has_attr(m_attr); }
		};

		template <typename ValueMatcher>
		class attribute_entry_matcher : public matcher
		{
			Dwarf_Half m_attr;
			ValueMatcher m_val_matcher;
		public:
			attribute_entry_matcher(Dwarf_Half attr, ValueMatcher val_matcher) 
				: m_attr(attr), m_val_matcher(val_matcher) {}
			bool operator()(encap::die& d) 
			{ return d.has_attr(m_attr) && m_val_matcher(d.attrs()[m_attr]); }	
		};

		template <typename W> class select_limited_depth : public selector
		{
			int m_depth_limit;
		public:
			select_limited_depth(int depth_limit) : m_depth_limit(depth_limit) {}
			bool operator()(encap::die& candidate, W& walker)
			{
				return walker.depth() <= m_depth_limit;
			}
		};
		
		class select_until_captured : public selector
		{
			const capture_func<Dwarf_Off> m_capture;
		public:
			select_until_captured(const capture_func<Dwarf_Off> capture) : m_capture(capture) {}
			bool operator()(encap::die& candidate, walker& walker) 
			{ return !m_capture.captured.is_initialized(); }
		};
		
		template <typename M, typename Walker>
		boost::optional<Dwarf_Off> find_first_match(dieset& dies, Dwarf_Off start,
			const M& matcher)
		{
			capture_func<Dwarf_Off> capture;
			select_until_captured until_captured(capture);
			
			typename Walker::find_first_match_walker_t walker(capture, matcher, until_captured);

			walker(dies, start);
			return capture.captured;
		}
		
	} // end namespace walker
	
	typedef std::vector<std::string> pathname;
	boost::optional<Dwarf_Off> resolve_die_path(dieset& dies, const Dwarf_Off start, 
		const pathname& path, pathname::const_iterator pos);
		
	// print action
	class print_action : public walker::action {
		abi_information& info;
		
		/*int indent_level;
		newline_tabbing_filter stream_filter;
		boost::iostreams::filtering_ostreambuf wrapped_streambuf;*/
		srk31::indenting_ostream *created_stream;
		srk31::indenting_ostream& wrapped_stream;

	public:
		print_action(abi_information& info);
		print_action(abi_information& info, std::ostream& stream);
		virtual ~print_action();
		void operator()(Dwarf_Off off);
	};
}

#endif
