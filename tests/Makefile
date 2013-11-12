cases := $(wildcard [a-z]*)

run-%: # run the test case with itself as input
	$(MAKE) -C "$*" "$*" && ( cd "$*" && ./$* ./$* )

default:
	for case in $(cases); do \
            $(MAKE) run-$$case; \
        done
