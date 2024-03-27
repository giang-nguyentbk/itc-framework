TARGET = itc_test_1
BIN = ./bin
# Only one of two -DMUTEX_... is enabled at a time, otherwise you will get multiple definition error in compilation
CFLAGS = -g -Wall -Wextra -lpthread -DUNITTEST -DMOCK_SENDER_UNITTEST -DMUTEX_TRACE_TIME_UNITTEST #-DMUTEX_TRACE_UNITTEST
CC = gcc

INCLUDE_DIR =
INCLUDE_DIR += -I ../../../if
INCLUDE_DIR += -I ../../../inc

SRC_DIR =
SRC_DIR += -I ../../../src
SRC_DIR += -I ../../../src/allocators
SRC_DIR += -I ../../../src/helpers
SRC_DIR += -I ../../../src/transporters
SRC_DIR += -I ./

SIG_DIR =
SIG_DIR += -I ../../../unitTest

vpath %.h 	$(INCLUDE_DIR)
vpath %.c 	$(SRC_DIR)
vpath %.sig 	$(SIG_DIR)

.PHONY: all

all: create_bin $(TARGET)

create_bin:
	@mkdir -p $(BIN)

$(TARGET): $(BIN)/itc.o $(BIN)/itc_test_1.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o 
	$(CC) $^ $(CFLAGS) -o $(BIN)/$(TARGET)

$(BIN)/itc.o: itc.c itc_impl.h itc.h itci_alloc.h itci_trans.h itc_threadmanager.h itc_proto.h itc_queue.h
	$(CC) -c  $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_test_1.o: itc_test_1.c itc_impl.h itc.h itc_threadmanager.h moduleXyz.sig
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

$(BIN)/itc_threadmanager.o: itc_threadmanager.c itc_impl.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_queue.o: itc_queue.c itc_impl.h itc_queue.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_malloc.o: itc_malloc.c itc_impl.h itci_alloc.h itc.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_local.o: itc_local.c itc_impl.h itci_trans.h itc.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_lsocket.o: itc_lsocket.c itc_impl.h itci_trans.h itc.h itc_proto.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_sysvmq.o: itc_sysvmq.c itc_impl.h itci_trans.h itc.h itc_threadmanager.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

run:
	$(BIN)/$(TARGET)

val:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)