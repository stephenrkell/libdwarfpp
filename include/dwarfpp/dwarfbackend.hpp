/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 *
 * dwarfbackend.hpp: backend selector for the C-API / type layer.

 * Copyright (c) 2008--26, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_DWARFBACKEND_HPP_
#define DWARFPP_DWARFBACKEND_HPP_

#include "config.h" 

#if defined(DWARFPP_USE_LIBDW) && DWARFPP_USE_LIBDW
#include "dwarfpp/libdw.hpp"
#else
#include "dwarfpp/libdwarf.hpp"
#endif

#endif /* DWARFPP_DWARFBACKEND_HPP_ */
