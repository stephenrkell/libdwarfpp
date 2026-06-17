%: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

type-fp-abstract-origin: type-fp-abstract-origin.cpp
type-fp-abstract-origin: deep.os

deep.o: deep.c
	$(CC) $(CFLAGS) -O2 -gdwarf-4 -c $< -o $@

deep-renamed.o: deep.o
	objcopy --prefix-symbols=another_ $< $@

deep.os: deep.o deep-renamed.o
	ld -r -o $@ $+
