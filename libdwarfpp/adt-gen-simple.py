#!/usr/bin/env python

# What needs to be generated?
# classes
# predicates
# factory functions

# the complete set of DWARF 3 (rev. f onwards) tags,
# with our extra schematic info as follows.
# what attributes each has: name, optional or mandatory
# what of children it (typically) has -- we have to supply this
# *** a separate table has the encap libdwarf rep of each attribute

import sys

sys.path.append('./spec')

from dwarf3 import *

def all_attrs(tag):
    if tag_map[tag][1] = None:
        base_attrs = []
    else:
        base_attrs = all_attrs(tag_map[tag][1])
    return base_attrs + tag_map[tag][0]

def main(argv):
    for (tag, (attr_list, base) ) in tags:
        print "begin_class(%s, %s)" % (tag, base)
        for (attr, optional) in attr_list:
            print "attr_%s(%s)" % (
        print "end_class(%s)" % tag

# main script
if __name__ == "__main__":
    main(sys.argv[1:])
