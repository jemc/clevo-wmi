%{
#include <stdio.h>
#include <stdlib.h>
%}
%option nounput
%option noinput
%option yylineno
%x C_COMMENT
%%
"/*"			{ BEGIN(C_COMMENT); }
<C_COMMENT>"*/"		{ BEGIN(INITIAL); }
<C_COMMENT>.		{}
<C_COMMENT>\n		{}
<C_COMMENT><<EOF>>	{ fprintf(stderr, "Error: Unterminated comment\n"); yyterminate(); }

[ \t\n,"0x"]		;
.			{ fprintf(stderr, "Warning: Unexpected character '%s' on line %d\n", yytext, yylineno); };
[0-9a-fA-F]{2}		{ fprintf(stdout, "%c", (char)strtol(yytext, NULL, 16)); }
%%
int main(int argc, char**argv)
{
	++argv, --argc;	/* skip over program name */
	if ( argc > 0 )
		yyin = fopen( argv[0], "r" );
	else
		yyin = stdin;

	yylex();
	return 0;
}

