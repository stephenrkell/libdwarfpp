import sys

sys.path.append('./spec')

from dwarf_current import *

def main(argv):
    for (tag, (attr_list, children, bases) ) in tags:
        print("factory_case(%s, %s)" % (tag, ', '.join(bases)))

# main script
if __name__ == "__main__":
    main(sys.argv[1:])
