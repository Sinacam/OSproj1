FLAGS = -Wall -Wextra -pedantic -DNDEBUG

all:
	gcc main.c $(FLAGS) -o program
run:
	./program

clean:
	rm -f program timing main.o
