type-equality: test.so

# test.so is a .so with exactly two CUs, which contain
# identical DWARF types. We make it from two copies
# of the same .o file

test.so: hello.o hello-renamed.o
	$(CC) -shared -o $@ $+

hello.o: CFLAGS += -g -fPIC

hello-renamed.o: hello.o
	objcopy --redefine-sym main=another_main $< $@
