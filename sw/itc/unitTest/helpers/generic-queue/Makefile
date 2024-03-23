TARGET = itc_queue_test
BIN = ./bin
CFLAGS = -g -Wall -Wextra
CC = gcc

INCLUDE_DIR =
INCLUDE_DIR += -I ../../../if
INCLUDE_DIR += -I ../../../inc

SRC_DIR =
SRC_DIR += -I ../../../src/allocators
SRC_DIR += -I ../../../src/helpers
SRC_DIR += -I ../../../src/transporters
SRC_DIR += -I ./

#SIG_DIR =
#SIG_DIR += -I ../../../unitTest

vpath %.h 	$(INCLUDE_DIR)
vpath %.c 	$(SRC_DIR)
#vpath %.sig 	$(SIG_DIR)

.PHONY: all

all: create_bin $(TARGET)

create_bin:
	@mkdir -p $(BIN)

$(TARGET): $(BIN)/itc_queue.o $(BIN)/itc_queue_test.o
	$(CC) $^ $(CFLAGS) -o $(BIN)/$(TARGET)

$(BIN)/itc_queue.o: itc_queue.c itc_impl.h itc_queue.h
	$(CC) -c -DUNITTEST $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_queue_test.o: itc_queue_test.c itc_impl.h itc_queue.h
	$(CC) -c -DUNITTEST $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

run:
	$(BIN)/$(TARGET)

val:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)