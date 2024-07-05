all: sender receiver

sender: sender.c
	gcc -Wall -Werror -Wextra -lrt -o sender sender.c

receiver: receiver.c
	gcc -Wall -Werror -Wextra -lrt -o receiver receiver.c

rmgen:
	rm -rf /dev/shm/*

clean:
	rm -rf ./sender ./receiver