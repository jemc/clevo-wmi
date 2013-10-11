all: hexxer

hexxer: lex.yy.c
	gcc -Wall -O2 -o hexxer lex.yy.c -lfl

lex.yy.c: hex.l
	flex -o lex.yy.c hex.l

clean:
	rm -rf *.o hexxer lex.yy.c
