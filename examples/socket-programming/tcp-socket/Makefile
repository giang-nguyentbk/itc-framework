all: server client

server: server.c
	gcc -Wall -Werror -Wextra -o server server.c

client: client.c
	gcc -Wall -Werror -Wextra -o client client.c

rs:
	./server

rc:
	./client

clean:
	rm -rf ./server ./client