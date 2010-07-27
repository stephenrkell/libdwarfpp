#include <dwarfpp/encap.hpp>
#include <dwarfpp/encap_adt.hpp>
//#include <dwarfpp/wrap.hpp>
//#include <dwarfpp/util.hpp>
#include <functional>
#include <iostream>

// test die assignment

int main(int argc, char **argv)
{
	using namespace dwarf;
	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	encap::file df(fileno(f));

	encap::Die_encap_all_compile_units& top_die = df.get_ds().all_compile_units();

	// test the iterator_with_lens stuff
    for (encap::Die_encap_base::children_iterator i = top_die.children_begin(); 
	    	i != top_die.children_end();
            i++)
	{
    	std::cerr << "Found a child encap::die at address " << *i 
        	<< ", name: " << ((*i)->has_attr(DW_AT_name) ? (**i)[DW_AT_name].get_string() : "(none)")
            << std::endl;
        
        encap::Die_encap_compile_unit *cu = 
        	dynamic_cast<encap::Die_encap_compile_unit *>(*i);
            
        //for (encap::die::subprograms_iterator j = cu->subprograms_begin();
    	//    j != cu->subprograms_end(); j++)
        //{
 		//    std::cerr << "Found a subprogram, name " << *((*j)->name()) << std::endl;       
        //}
    }

    for (encap::subprograms_iterator i = top_die.subprograms_begin();
    	i != top_die.subprograms_end(); i++)
    {
 		std::cerr << "Found a subprogram, name " << *((*i)->name()) << std::endl;       
    }
    


	// add the imported functions
//    dwarf::wrap::All_compilation_units cus(df.get_ds()[0UL]);
    
//    for (dwarf::wrap::All_compilation_units::compile_unit_iterator iter = cus.compile_units_begin();
//    	false; /*iter != cus.compile_units_end();
//        iter++*/)
//    {
//        
//    }
    
    //std::cout << df.get_ds();
}
