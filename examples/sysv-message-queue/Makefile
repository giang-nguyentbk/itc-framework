all: send receive filegen

send: sender.c
	gcc -Wall -Werror -Wextra -o send sender.c

receive: receiver.c
	gcc -Wall -Werror -Wextra -o receive receiver.c

filegen: sysvmsq_file_generator.c
	gcc -Wall -Werror -Wextra -o filegen sysvmsq_file_generator.c

gen:
	./filegen

rmgen:
	rm -rf /tmp/sysvmsq

clean:
	rm -rf ./send ./receive ./filegen