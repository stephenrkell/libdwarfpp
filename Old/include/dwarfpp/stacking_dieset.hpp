/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * stacking_dieset.hpp: combining many diesets in an overlaid fashion.
 *
 * Copyright (c) 2010, Stephen Kell.
 */

#ifndef DWARFPP_STACKING_DIESET_HPP_
#define DWARFPP_STACKING_DIESET_HPP_

#include <vector>
#include "spec_adt.hpp"

class stacking_dieset
	: public abstract_dieset
{
	// our array of stacked diesets
	std::vector<abstract_dieset *> ds_vec;
	
public:
	// constructors
	template <typename In>
	stacking_dieset(In first, In past_last)
	{ 
		for (In i = first; i != past_last; i++)
		{
			// specs must match! (for now... define compatibility relation on specs later)
			if (i != first) assert(i->get_spec() == ds_vec.at(0)->get_spec());
			ds_vec.push_back(&*i); 
		}
		assert(ds_vec.size() > 0);
	}

	/* virtual */ iterator find(Dwarf_Off off);
	/* virtual */ iterator begin();
	/* virtual */ iterator end();

	/* virtual */ std::deque< position > path_from_root(Dwarf_Off off);

	/* virtual */ boost::shared_ptr<spec::basic_die> operator[](Dwarf_Off off) const;
    /* virtual */ boost::shared_ptr<spec::file_toplevel_die> toplevel(); /* NOT const */
    /* virtual */ const spec::abstract_def& get_spec() const;
};

#endif
