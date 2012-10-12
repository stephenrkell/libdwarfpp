#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>
#include <dwarfpp/adt.hpp>
#include <boost/icl/interval_map.hpp>

using std::cout; 
using std::cerr; 
using std::endl;
using namespace dwarf;
using namespace dwarf::lib;
using std::pair;
using std::make_pair;
using std::dynamic_pointer_cast;
using std::set;
using std::map;

/* instantiate the following template, for some reason. */
//dwarf::core::basic_die::basic_die<dwarf::core::iterator_df<dwarf::core::basic_die> >(dwarf::core::basic_root_die&, dwarf::core::iterator_df<dwarf::core::basic_die> const&);

int static_we_should_find;

int main(int argc, char **argv)
{
	assert(argc > 1);
	cerr << "Opening " << argv[1] << "..." << endl;
	std::ifstream in(argv[1]);
	assert(in);
	core::root_die root(fileno(in));
	dwarf::lib::file df(fileno(in));
	dwarf::lib::dieset ds(df);

	//cout << root;

	// set<lib::Dwarf_Off> static_vars;
	// map<lib::Dwarf_Addr, pair<lib::Dwarf_Off, size_t> > addr_lookup;
	
	cerr << "Searching for variables..." << endl;
	for (auto i = root.begin(); i != root.end(); ++i)
	{
		if (i.tag_here() == DW_TAG_variable
			&& i.has_attribute_here(DW_AT_location))
		{
			/* DWARF doesn't tell us whether a variable is static or not. 
			 * We want to rule out non-static variables. To do this, we
			 * rely on our existing lib:: infrastructure. */
			core::Attribute a(i.get_handle(), DW_AT_location);
			encap::attribute_value val(a, i.get_handle(), i.spec_here());
			auto loclist = val.get_loclist();
			bool reads_register = false;
			for (auto i_loc_expr = loclist.begin(); 
				i_loc_expr != loclist.end(); 
				++i_loc_expr)
			{
				for (auto i_instr = i_loc_expr->begin(); 
					i_instr != i_loc_expr->end();
					++i_instr)
				{
					if (spec::DEFAULT_DWARF_SPEC.op_reads_register(i_instr->lr_atom))
					{
						reads_register = true;
						break;
					}
				}
				if (reads_register) break;
			}
			if (!reads_register)
			{
				auto name = i.name_here();
				cerr << "Found a static or global variable named "
					<< (name.get() ? name.get() : "(no name)") << endl;
				Dwarf_Off off = i.offset_here();
				//static_vars.insert(off);
				
				/* To get the address range, we use the adt code for now. This is not so
				 * bad for performance because we only heap allocate for the DIEs we've
				 * already identified as static variables. */
				auto p_ds = &ds;
				assert(p_ds);
				auto found = (*p_ds)[off];
				assert(found);
				auto with_static_location
				 = dynamic_pointer_cast<spec::with_static_location_die>(found);
				if (!with_static_location)
				{
					cerr << "Warning: expected a with_static_location_die, got " 
						<< found->summary() << endl;
					continue;
				}
				try
				{
					boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned> out
					 = with_static_location->file_relative_intervals(nullptr, nullptr);
					cerr << out << endl;
					auto interval_count = boost::icl::interval_count(out);
					if (interval_count != 1)
					{
						cerr << "Warning: expected a single interval, got " << interval_count
							<< " of them, for " << with_static_location->summary() << endl;
						continue;
					}
// 					addr_lookup.insert(make_pair(
// 						out.begin()->first.lower(),
// 						make_pair(
// 							off,
// 							out.begin()->first.upper() - out.begin()->first.lower()
// 						)
// 					));
					/* We output to stderr ad-hoc text data in tab-separated fields as follows. 
					 * file-relative address; 
					 * CU offset;
					 * object size;
					 * name (free text)
					 */
					Dwarf_Addr file_relative_addr = out.begin()->first.lower();
					Dwarf_Off cu_offset =  i.enclosing_cu_offset_here();
					unsigned size = out.begin()->first.upper() - out.begin()->first.lower();
					cerr << std::hex 
						<< "0x" << file_relative_addr << '\t' // addr
						<< "0x" << cu_offset << '\t'  // CU offset
						<< "0x" << size << '\t' // size
						<< (name.get() ? name.get() : "") 
						<< std::dec << endl;
					/* We output to stdout binary data as follows. 
					 * file-relative address (width: native); 
					 * CU offset (64 bits);
					 * object size (32 bits).
					 */
					auto addr_size = (*ds.toplevel()->compile_unit_children_begin())->get_address_size();
					assert(addr_size == 4 || addr_size == 8);
					assert(sizeof cu_offset == 8);
					assert(sizeof size == 4);
					cout.write((char*) &file_relative_addr, addr_size);
					cout.write((char*) &cu_offset, 8);
					cout.write((char*) &size, 4);
					
				}
				catch (dwarf::lib::Not_supported)
				{
					cerr << "Warning: couldn't evaluate location of DIE at 0x"
						<< std::hex << off << std::dec << endl;
					continue;
				}
				
			}
			else
			{
				auto name = i.name_here();
				cerr << "Found a local variable named "
					<< (name.get() ? name.get() : "(no name)") << endl;
			}
		} 
	}

	
	return 0;
}
