FLAGS = -Wall -Wextra -pedantic -lrt

all:
	gcc main.c $(FLAGS) -o program
run:
	./program

clean:
	rm -f program main.o
