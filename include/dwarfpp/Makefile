incs := $(wildcard *.inc)
m4_hdrs := $(wildcard *.hpp.m4)
hdrs := $(patsubst %.m4,%,$(m4_hdrs))

.PHONY: default
default: $(patsubst %.m4,%,$(m4_hdrs)) dwarf-onlystd.h dwarf-onlystd-v2.h dwarf-ext-GNU.h

%: %.m4 $(incs)
	rm -f "$@" && m4 -I. < "$<" > "$@" && chmod a-w "$@"

.PHONY: clean
clean:
	rm -f $(hdrs)

dwarf-onlystd.h: /usr/include/dwarf.h
	cat "$<" | egrep -v 'DW_[A-Z]+_(GNU|SUN|HP|APPLE|INTEL|ARM|upc|PGI|ALTIUM|MIPS|CPQ|VMS|GNAT)' | \
		egrep -v '/\* (SGI|GNU)( \*/|\. )' | egrep -v 'LANG_Mips|LANG_Upc' | egrep -v '_use_GNAT' | egrep -v 'ATCF entries start at|DW_LANG_UPC instead.' > "$@"

dwarf-onlystd-v2.h: dwarf-onlystd.h
	cat "$<" | grep -v 'DWARF[^2]' > "$@"

dwarf-ext-GNU.h: /usr/include/dwarf.h
	cat "$<" | egrep '(_|/\* |, )GNU' | egrep -vi conflict | egrep -vi '^[[:blank:]]*/\*' > "$@"
