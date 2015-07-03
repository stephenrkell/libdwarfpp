import sys

sys.path.append('./spec')

from dwarf3 import *

def main(argv):
    for base in set(sum([bases for (tag, (attr, children, bases)) in tags], [])):
        print "begin_pred(%s)" % base
        for tag_in_pred in [tag for (tag, ( attr, children, this_tag_bases) ) in tags \
                if base in this_tag_bases]:
            print "\tdisjunct(%s)" % tag_in_pred
        print "end_pred(%s)" % base

# main script
if __name__ == "__main__":
    main(sys.argv[1:])
