#!/usr/bin/env python

f = file("test-object.o")
import dwarfpp
df = dwarfpp.file(f.fileno())
ai = dwarfpp.abi_information(df)
