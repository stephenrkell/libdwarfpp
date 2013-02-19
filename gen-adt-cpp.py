#!/usr/bin/env python

import sys

sys.path.append('./spec')

# FIXME: make the spec file an argument to this script
from dwarf3 import *

def mandatory_fragment(mand):
    if mand: 
        return "mandatory"
    else:
        return "optional" 

def super_attrs(tag):
    #sys.stderr.write("Calculating super attrs for %s\n" % tag)
    # attrs of all bases, plus super_attrs of all bases
    immediate_base_attrs = sum([tag_map.get(base, ([], [], []))[0] \
        for base in tag_map.get(tag, ([], [], []))[2] + artificial_tag_map.get(tag, ([], [], []))[2]], []) \
        + sum([artificial_tag_map.get(base, ([], [], []))[0] \
        for base in tag_map.get(tag, ([], [], []))[2] + artificial_tag_map.get(tag, ([], [], []))[2]], [])
    base_attrs = sum(map(super_attrs, tag_map.get(tag, ([], [], []))[2]), immediate_base_attrs) + \
        sum(map(super_attrs, artificial_tag_map.get(tag, ([], [], []))[2]), [])
    #sys.stderr.write("Calculated super attrs for %s as %s\n" % (tag, str([x for x in set(base_attrs)])))
    return [x for x in set(base_attrs)] #+ tag_map[tag][0]

def main(argv):
    for (tag, (attr_list, children, bases) ) in tags:
        print "forward_decl(%s)" % tag
    for (tag, (attr_list, children, bases) ) in tags:
        print "begin_class(%s, %s, %s)" % (tag, \
        'base_initializations(' + ', '.join(["initialize_base(" + base + ")" for base in bases]) + ')', \
        ', '.join(["declare_base(%s)" % base for base in bases]))
        for (attr, mand) in attr_list:
            print "\tattr_%s(%s, %s)" % (mandatory_fragment(mand), attr, attr_type_map[attr])
        for (attr, mand) in super_attrs(tag):
            print "\tsuper_attr_%s(%s, %s)" % (mandatory_fragment(mand), attr, attr_type_map[attr])
        for child in children:
            print "\tchild_tag(%s)" % child
        print "#ifdef extra_decls_%s\n\textra_decls_%s\n#endif" % (tag, tag)
        print "end_class(%s)" % tag

# main script
if __name__ == "__main__":
    main(sys.argv[1:])
