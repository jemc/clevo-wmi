%{
#include <stdio.h>
#include <stdlib.h>
%}
%option nounput
%option noinput
%x C_COMMENT
%%
"/*"               { BEGIN(C_COMMENT); }
<C_COMMENT>"*/"    { BEGIN(INITIAL); }
<C_COMMENT>.       {}
<C_COMMENT>\n      {}
<C_COMMENT><<EOF>> { fprintf(stderr, "Error: Unterminated comment\n"); yyterminate(); }

[ \t\n,"0x"]    ;
.		{ fprintf(stderr, "Warning: Unexpected character: %s\n", yytext); };
[0-9a-fA-F]{2}  { fprintf(stdout, "%c", (char)strtol(yytext, NULL, 16)); }
%%
int main() {
	//lex through the input:
	yylex();
	return 0;
}
