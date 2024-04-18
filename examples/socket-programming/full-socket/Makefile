BIN = ./bin


all: create_bin $(BIN)/socket $(BIN)/socket_peer

create_bin:
	@mkdir -p $(BIN)

$(BIN)/socket: socket.c proto.h
	gcc -Wall -Werror -Wextra -I ./ -o $(BIN)/socket socket.c

$(BIN)/socket_peer: socket_peer.c proto.h
	gcc -Wall -Werror -Wextra -I ./ -o $(BIN)/socket_peer socket_peer.c

r1:
	$(BIN)/socket

v1:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/socket

r2:
	$(BIN)/socket_peer

v2:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/socket_peer


clean:
	rm -rf $(BIN)