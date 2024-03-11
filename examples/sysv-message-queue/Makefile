all: send receive filegen

send: sender.c
	gcc -Wall -Werror -Wextra -o send sender.c

receive: receiver.c
	gcc -Wall -Werror -Wextra -o receive receiver.c

filegen: sudo_sysvmsq_filepath_generator.c
	gcc -Wall -Werror -Wextra -o filegen sudo_sysvmsq_filepath_generator.c

gen:
	sudo ./filegen

clean:
	rm -rf ./send ./receive ./filegen