all: sender receiver

sender: sender.c
	gcc -Wall -Werror -Wextra -lpthread -lrt -o sender sender.c

receiver: receiver.c
	gcc -Wall -Werror -Wextra -lpthread -lrt -o receiver receiver.c

rmv:
	rm -rf /dev/mqueue/*

clean:
	rm -rf ./sender ./receiver