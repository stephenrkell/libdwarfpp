TESTS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

CXXFLAGS += -std=gnu++11
CXXFLAGS += -I$(HOME)/opt/include 
CXXFLAGS += -I$(HOME)/scratch/opt/include 
CXXFLAGS += -I$(realpath $(TESTS_DIR)/..)/include 
CXXFLAGS += -I$(realpath $(TESTS_DIR)/../..)/libsrk31c++/include 
CXXFLAGS += -I.
CXXFLAGS += -I$(HOME)/opt/include 
CXXFLAGS += -I$(realpath $(TESTS_DIR)/../..)/libc++fileno/include

LDFLAGS += -L$(realpath $(TESTS_DIR)/..)/src 
LDFLAGS += -L$(realpath $(TESTS_DIR)/../..)/libsrk31c++/lib 
LDFLAGS += -L$(HOME)/opt/lib  
LDFLAGS += -L$(realpath $(TESTS_DIR)/../..)/libc++fileno/lib

include $(TESTS_DIR)/Makerules
