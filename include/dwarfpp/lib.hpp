/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * lib.hpp: basic C++ wrapping of libdwarf C API (info section).
 *
 * Copyright (c) 2008--17, Stephen Kell.
 */

#ifndef DWARFPP_LIB_HPP_
#define DWARFPP_LIB_HPP_

#include <iostream>
#include <utility>
#include <functional>
#include <memory>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <queue>
#include <cassert>
#include <boost/optional.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/icl/interval_map.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/iterator/filter_iterator.hpp>
#include <srk31/selective_iterator.hpp>
#include <srk31/concatenating_iterator.hpp>
#include <srk31/rotate.hpp>
#include <srk31/transform_iterator.hpp>
#include <libgen.h> /* FIXME: use a C++-y way to do dirname() */

#include "util.hpp"
#include "spec.hpp"
#include "opt.hpp"

#include "attr.hpp" // includes forward decls for iterator_df!
#include "expr.hpp"

#include "abstract.hpp"
#include "libdwarf-handles.hpp" /* we use only libdwarf for backing, for now */

#include "root.hpp"
#include "iter.hpp"
#include "dies.hpp"

#include "root-inl.hpp"
#include "abstract-inl.hpp"
#include "iter-inl.hpp"
#include "dies-inl.hpp"

#endif
