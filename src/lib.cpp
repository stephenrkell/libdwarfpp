/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * lib.cpp: basic C++ wrapping of libdwarf C API.
 *
 * Copyright (c) 2008--17, Stephen Kell.
 */

#include "dwarfpp/lib.hpp"
#include "dwarfpp/expr.hpp" /* for absolute_loclist_to_additive_loclist */
#include "dwarfpp/frame.hpp"

#include <srk31/indenting_ostream.hpp>
#include <srk31/algorithm.hpp>
#include <sstream>
#include <libelf.h>

