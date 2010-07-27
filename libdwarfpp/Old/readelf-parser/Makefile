.PHONY: default
default: readelfLexer.py readelfParser.py

.PHONY: test
test: default
	xterm -hold -e readelf --debug-dump=info ./test-object &
	./readelf-parser-test

#%.pyc: %.py

%Lexer.py %Parser.py: %.g
	antlr "$<"
