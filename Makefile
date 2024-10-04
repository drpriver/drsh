ifeq ($(OS),Windows_NT)
DOT_EXE=.exe
# compiles with cl as well
CC=clang
endif

drsh$(DOT_EXE): drsh.c Makefile
	$(CC) $< -o $@
