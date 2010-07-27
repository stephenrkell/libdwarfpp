grammar readelf;
options {
    output=AST;
    language=Python;
    ASTLabelType=CommonTree; // type of $statement.tree ref etc...
}

/* Parses a small subset of the output of readelf, namely that produced
 * when using (only) the --debug-dump=[=info,=abbrev,=pubnames,=macro,=frames,=str] options. */

/* The whole input */
toplevel:   debugSectionDump*
		;
        
debugSectionDump: infoSectionDump
//				| abbrevSectionDump
//				| pubnamesSectionDump
//                | macroSectionDump
//                | framesSectionDump
//                | strSectionDump
                ;

abbrevSectionDump : ABBREVHDR! NEWLINE! NEWLINE! 'Number'! 'TAG.'! NEWLINE! (INT IDENT NOTE? NEWLINE)*
				;
				
/*
objectExpr      :   atomicExpr
                |   'link'^ objectList linkBody?
                |   'mediate'^ '('! objectExpr ','! objectExpr ')'! mediateBody
                ;
objectList      :   '['! commaSeparatedObjects ']'!
                ;
commaSeparatedObjects	: objectExpr ( ','!? | ','! commaSeparatedObjects )
                		;
linkBody        :   '{'! ( IDENT '<-'^ IDENT ';'! )* ';'!? '}'!
                ;
mediateBody     :   '{'! ( IDENT '<-'^ IDENT ';'! )* ';'!? '}'! // TODO: replace this
                ;
atomicExpr      :   'file'^ FILENAME
                ;

*/ 
/* Lexer */
ABBREVHDR : 'Contents of the \.debug_abbrev section:';
NOTE : '[' .* ']';
IDENT  :   ('a'..'z'|'A'..'Z')('a'..'z'|'A'..'Z'|'0'..'9'|'_')* ;
INT :   '0'..'9'+ ;
NEWLINE:'\r'? '\n' ;
WS  :   (' '|'\t')+ {self.skip();} ;
FILENAME : '\"' ( ~'\"'|'\\\"' )+ '\"';
