/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 *
 * handles.hpp: backend selector for the C++ handle layer.
 *
 * libdwarfpp can be backed either by David Anderson's libdwarf or, directly, by
 * elfutils' libdw. The two backends provide the same
 * dwarf::core::{Debug,Die,Attribute,...} handle interface but implement it
 * differently (the libdw layer stores libdw's by-value die/attribute structs
 * inline, with no per-node allocation). This header picks the right handle
 * header for the configured backend, so the rest of libdwarfpp can just
 * #include "dwarfpp/handles.hpp" and stay backend-agnostic.
 *
 * Copyright (c) 2008--26, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_HANDLES_HPP_
#define DWARFPP_HANDLES_HPP_

#include "dwarfpp/dwarfbackend.hpp"

#if defined(DWARFPP_USE_LIBDW) && DWARFPP_USE_LIBDW
/* The libdw backend uses elfutils' libdw directly, with handle objects that
 * store libdw's by-value die/attribute structs inline (no per-node allocation). */
#include "dwarfpp/libdw-handles.hpp"
#else
/* The original David Anderson libdwarf backend. */
#include "dwarfpp/libdwarf-handles.hpp"
#endif

#endif /* DWARFPP_HANDLES_HPP_ */
