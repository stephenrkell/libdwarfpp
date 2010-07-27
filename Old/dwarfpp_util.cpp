#include "dwarfpp_util.hpp"
#include "dwarfpp_simple.hpp"
#include "indenting_ostream.hpp"
#include <cstdio>
#include <cassert>
#include <vector>
#include <iostream>

namespace dwarf
{
	//func_true<void*> true_f;
	//func_true<encap::die&, 
	//depthfirst_walker default_depthfirst_walker;
	//siblings_upward_walker default_siblings_upward_walker;
	namespace walker
	{		
// 		boost::optional<Dwarf_Off> find_first_match(dwarf::dieset& dies, Dwarf_Off off,
// 			walker& walk)
// 		{
// 			capture_func<Dwarf_Off> capture;
// 			//selector<
// 			//func2_true<encap::die&, W> true_f;
// 			walk/*er*/(dies, off/*, match, capture, true_f*/);
// 			//walk_dwarf_tree_up_siblings(dies, off, match, capture, true_f);
// 			//func_true<encap::die&> my_true_f;
// 			//walk_dwarf_tree_up_siblings<T, match_capture, func_true>(dies, off, match, capture, my_true_f);
// 			//test_func(dies, off, match, capture);
// 			return capture.captured;
// 		}
				
		//tag_matcher<std::binder2nd<std::equal_to<Dwarf_Half>, Dwarf_Half> > 
		tag_equal_to_matcher_t matcher_for_tag_equal_to(Dwarf_Half tag)
		{
			return tag_matcher<std::binder2nd<std::equal_to<Dwarf_Half> > > 
				(std::bind2nd(std::equal_to<Dwarf_Half>(), tag));
		}

		name_equal_to_matcher_t matcher_for_name_equal_to(std::string name)
		{
			return name_matcher<std::binder2nd<std::equal_to<std::string> > > 
				(std::bind2nd(std::equal_to<std::string>(), name));
		}		
		//tag_matcher<std::pointer_to_unary_function<Dwarf_Half, bool> >
		tag_satisfying_func_matcher_t matcher_for_tag_satisfying_func(bool (*func)(Dwarf_Half))
		{
			return tag_matcher<std::pointer_to_unary_function<Dwarf_Half, bool> >(
				std::ptr_fun(func));
		}
		
		offset_greater_equal_matcher_t matcher_for_offset_greater_equal(Dwarf_Off off)
		{
			return offset_matcher<std::binder2nd<std::greater_equal<Dwarf_Off> > >
				(std::bind2nd(std::greater_equal<Dwarf_Off>(), off));
		}
	}
	
	boost::optional<Dwarf_Off> resolve_die_path(dieset& dies, const Dwarf_Off start, 
		const pathname& path, pathname::const_iterator pos)
	{
		boost::optional<Dwarf_Off> retval;
		if (pos == path.end()) { retval = boost::optional<Dwarf_Off>(start); return retval; }
		else
		{
			//std::cerr << "Looking for a DWARF element named " << *pos 
			//	<< " starting from offset 0x" << std::hex << start << std::dec
			//	<< ", " << dies[start] << std::endl;
			if (tag_is_type(dies[start].tag()) && !tag_has_named_children(dies[start].tag()))
			{
				// this is a chained type qualifier -- recurse
				assert(dies[start].has_attr(DW_AT_type));
				retval = boost::optional<Dwarf_Off>(
					resolve_die_path(dies, dies[start][DW_AT_type].get_ref().off,
						path, pos));
				return retval;
			}
			else if (tag_has_named_children(dies[start].tag()) || start == 0UL)
			{
				// this is a compilation unit or type or variable or subprogram with named children
				//std::cerr << "Current die has " << dies[start].children().size() << " children" << std::endl;
				for (die_off_list::iterator iter = dies[start].children().begin();
					iter != dies[start].children().end();
					iter++)
				{
					if (dies[*iter].has_attr(DW_AT_name) && 
						dies[*iter][DW_AT_name].get_string() == *pos)
					{
						//std::cerr << __func__ << ": found a DWARF element named " << dies[*iter][DW_AT_name].get_string() << ", recursing..." << std::endl;
						retval = boost::optional<Dwarf_Off>(
							resolve_die_path(dies, *iter, path, pos + 1));
						return retval;
					}
					// else found an anonymous child -- continue
				} // end for
				// failed
				return retval; // == none
			}
			else 
			{	
				//std::cerr << "Error: current die does not have named children and is not a type! "
					//<< "Tag is " << tag_lookup(dies[start].tag()) << std::endl;
				return retval; // none
			}
		}
	}

	print_action::print_action(abi_information& info) : info(info), 
/*		indent_level(0),
		stream_filter(&indent_level),*/
		created_stream(0),
		wrapped_stream(srk31::indenting_cout)	/*stream(std::cout)*/ 
	{
		/*wrapped_streambuf.push(stream_filter);
		wrapped_streambuf.push(std::cerr);*/
	}
	print_action::print_action(abi_information& info, std::ostream& stream) : info(info),  
		/*indent_level(0),
		stream_filter(&indent_level),*/
		created_stream(new srk31::indenting_ostream(stream)),
		wrapped_stream(*created_stream)
		/*wrapped_stream(&wrapped_streambuf)*/	/*stream(std::cout)*/ 
	{
		/*wrapped_streambuf.push(stream_filter);
		wrapped_streambuf.push(stream);*/
	}
	print_action::~print_action() { if (created_stream) delete created_stream; }
	void print_action::operator()(Dwarf_Off off)
	{
		// calculate the appropriate indentation level
		int indent_level = 0;
		for (Dwarf_Off parent = off; 
			parent != 0UL; 
			parent = info.get_dies()[parent].parent()) indent_level++;

		// set the output stream to this level
		if (indent_level > wrapped_stream.level()) 
			while (wrapped_stream.level() < indent_level) wrapped_stream.inc_level();
		else while (wrapped_stream.level() > indent_level) wrapped_stream.dec_level();

		// problem: the walker doesn't do inc_ or dec_
		// when ascending/descending
		// so we're not making use of the stream filtering thing at all!
		// (except when printing out attributes
		// -- can hack around this by tracking last_indent_level across calls
		// and detecting edges

		encap::die& d = info.get_dies()[off];

		//for (int i = 0; i < indent; i++) stream << '\t'; //printf("\t");

		wrapped_stream  << d;

// 			printf("Read a DIE: tag %s, offset %llx, name %s\n", 
// 				d.tag() <= 64 ? dwarf::tag_lookup(d.tag()) : "(none)", 
// 				off, 
// 				d[DW_AT_name] != encap::attribute_value::DOES_NOT_EXIST() ?
// 					d[DW_AT_name].get_string().c_str() : "(not present)");

		// print attributes
		//print_attributes(current, indent + 1);
	}
}
