FLAGS = -O2 -Wall -Wextra -pedantic

all:
	g++ main.c $(FLAGS) -o program
run:
	./program

clean:
	rm -f program timing main.o
