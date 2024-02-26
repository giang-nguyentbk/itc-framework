TARGET = itc_malloc_test
BIN = ../bin
CFLAGS= -g -Wall -Wextra

INCLUDE_DIR =
INCLUDE_DIR += -I ../../../if
INCLUDE_DIR += -I ../../../inc

SRC_DIR =
SRC_DIR += -I ../../../src
SRC_DIR += -I ./

#SIG_DIR =
#SIG_DIR += -I ../../../unitTest

vpath %.h 	$(INCLUDE_DIR)
vpath %.c 	$(SRC_DIR)
#vpath %.sig 	$(SIG_DIR)

.PHONY: all

all: $(TARGET)

$(TARGET): $(BIN)/itc_malloc.o $(BIN)/itc_malloc_test.o
	$(CC) $^ $(CFLAGS) -o $(BIN)/$(TARGET)

$(BIN)/itc_malloc.o: itc_malloc.c itc.h itc_impl.h itci_alloc.h
	$(CC) -c -DUNITTEST $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_malloc_test.o: itc_malloc_test.c itc.h itci_alloc.h itc_impl.h
	$(CC) -c -DUNITTEST $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

run:
	$(BIN)/$(TARGET)

val:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)/*