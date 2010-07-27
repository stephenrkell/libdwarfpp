#!/usr/bin/env python

import pygccxml
from pyplusplus import module_builder_t

mb = module_builder_t(["dwarfpp_simple.hpp"]) #module_builder_t is the main class that
                             #will help you with code generation process
#mb.free_functions( arg_types=[ 'int &', None ] )
#mb.calldefs( access_type_matcher_t( 'protected' ) ).exclude()
#mb.decls( lambda decl: 'impl' in decl.name ).exclude()

module_builder_t.build_code_creator("dwarfpp_simple")

mb.code_creator.license = "(c) 2008-09, Stephen Kell."

module_builder_t.write_module("dwarfpp_simple_py++.cxx")
