#include <boost/graph/graph_concepts.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/encap_adt.hpp>
//#include <dwarfpp/encap_graph.hpp>
#include <cstdio>


int
main(int argc,char*argv[])
{
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::encap::file df(fileno(f));
    
    // first test the reference-attribute iterator on a single DIE having one reference
    std::vector<std::string> name_td(1, std::string("FILE"));
    auto td_opt = df.get_ds().all_compile_units().resolve(name_td.begin(), name_td.end());
    assert(td_opt);
    auto td = dynamic_cast<dwarf::encap::Die_encap_base&>(*td_opt);
    for (auto i = td.ref_attrs_begin(); i != td.ref_attrs_end(); i++)
    {
    	std::cout << "Found a reference from DIE at " 
        	<< std::hex << i->second.get_ref().referencing_off
        	<< " (tag " 
            << df.get_ds().get_spec().tag_lookup(df.get_ds()[i->second.get_ref().referencing_off]->get_tag())
        	<< ") to DIE at " 
            << std::hex << i->second.get_ref().referencing_off
        	<< " (tag " 
            << df.get_ds().get_spec().tag_lookup(df.get_ds()[i->second.get_ref().off]->get_tag())
            << ") by attribute " 
            << df.get_ds().get_spec().attr_lookup(i->second.get_ref().referencing_attr)
            << std::endl;
    }
    // first test the reference-attribute iterator on a single DIE having no references
    std::vector<std::string> name_base(1, std::string("int"));
    auto base_opt = df.get_ds().all_compile_units().resolve(name_base.begin(), name_base.end());
    assert(base_opt);
    auto base = dynamic_cast<dwarf::encap::Die_encap_base&>(*base_opt);
    for (auto i = base.ref_attrs_begin(); i != base.ref_attrs_end(); i++)
    {
    	assert(false);
    }
    
    unsigned ref_count = 0;
    
    for (auto i = (*(df.get_ds().all_compile_units().compile_units_begin()))->all_refs_dfs_begin();
    	i != (*(df.get_ds().all_compile_units().compile_units_begin()))->all_refs_dfs_end();
        i++)
    {
    	std::cout << "Found a reference from DIE at " 
        	<< std::hex << i->second.get_ref().referencing_off
        	<< " (tag " 
            << df.get_ds().get_spec().tag_lookup(df.get_ds()[i->second.get_ref().referencing_off]->get_tag())
        	<< ") to DIE at " 
            << std::hex << i->second.get_ref().referencing_off
        	<< " (tag " 
            << df.get_ds().get_spec().tag_lookup(df.get_ds()[i->second.get_ref().off]->get_tag())
            << ") by attribute " 
            << df.get_ds().get_spec().attr_lookup(i->second.get_ref().referencing_attr)
            << std::endl;
        ref_count++;
    }
    std::cout << "Visited " << ref_count << " references." << std::endl;
    return 0;
}

