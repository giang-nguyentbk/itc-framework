all: server

server: server.c
	gcc -Wall -Werror -Wextra -o server server.c

run:
	./server

# Use "telnet localhost 8888" to test it

clean:
	rm -rf ./server