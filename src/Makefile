CXXFLAGS += -std=gnu++0x -Wall -O0 -g3 -fno-eliminate-unused-debug-types -pg
CXXFLAGS += -I../include

SRC := $(wildcard *.cpp)
DEPS := $(patsubst %.cpp,.%.d,$(SRC))
OBJS := $(patsubst %.cpp,%.o,$(SRC))
#HDRS := $(wildcard *.hpp)

default: libdwarfpp.so 

libdwarfpp.a: $(OBJS)
	ar r "$@" $^

# use this version in distribution (avoid writing huge .o files)
#libdwarfpp.so: $(SRC)
#	g++ $(CXXFLAGS) -shared -o "$@" $^

# use this version when developing (avoid recompiling whole of huge .so file)
libdwarfpp.so: $(OBJS)
	g++ $(CXXFLAGS) -shared -o "$@" $^

.PHONY: clean
clean:
	rm -f *.o *.a *.inc .*.d

$(DEPS): .%.d : %.cpp
	g++ -MM $(CXXFLAGS) "$<"  > "$@"

include $(DEPS)

%.o: %.cpp #$(HDRS)
	g++ $(CXXFLAGS) -c -o "$@" "$<"

