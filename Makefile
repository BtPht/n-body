build:
	gcc -o main.o -c main.c -O2
	gcc -o n-bodies main.o ../raylib/src/libraylib.a -lc -lm -pthread

