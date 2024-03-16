all: test

test: pthread_cancel_destructor_test.c
	gcc -Wall -Werror -Wextra -o test pthread_cancel_destructor_test.c

run:
	./test

clean:
	rm -rf ./test