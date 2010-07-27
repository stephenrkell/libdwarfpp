incs := $(wildcard *.inc)
m4_hdrs := $(wildcard *.hpp.m4)
hdrs := $(patsubst %.m4,%,$(m4_hdrs))

.PHONY: default
default: $(patsubst %.m4,%,$(m4_hdrs))

%: %.m4 $(incs)
	rm -f "$@" && m4 -I. < "$<" > "$@" && chmod a-w "$@"

.PHONY: clean
clean:
	rm -f $(hdrs)
