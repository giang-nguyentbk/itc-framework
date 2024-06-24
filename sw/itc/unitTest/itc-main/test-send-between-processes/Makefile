TARGET_SENDER = itc_test_sender
TARGET_RECEIVER = itc_test_receiver
BIN = ./bin
# Only one of two -DMUTEX_... is enabled at a time, otherwise you will get multiple definition error in compilation
# -DMUTEX_TRACE_TIME_UNITTEST #-DMUTEX_TRACE_UNITTEST
# -DMUTEX_TRACE_UNITTEST #-DMUTEX_TRACE_TIME_UNITTEST
# #-DMUTEX_TRACE_TIME_UNITTEST #-DMUTEX_TRACE_UNITTEST
CFLAGS = -g -Wall -Wextra -lpthread #-DMUTEX_TRACE_TIME_UNITTEST #-DMUTEX_TRACE_UNITTEST
CC = gcc

SDK_SYSROOT_DIR		:= $(SDKSYSROOT)
SDK_USR_DIR		:= $(SDK_SYSROOT_DIR)/usr
SDK_LIB_DIR		:= $(SDK_USR_DIR)/lib
SDK_INC_DIR		:= $(SDK_USR_DIR)/include

INCLUDE_DIR =
INCLUDE_DIR += -I ../../../if
INCLUDE_DIR += -I ../../../inc
INCLUDE_DIR += -I $(SDK_INC_DIR)

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

all: create_bin $(TARGET_SENDER) $(TARGET_RECEIVER)

create_bin:
	@mkdir -p $(BIN)

$(TARGET_SENDER): $(BIN)/itc_s.o $(BIN)/itc_test_sender.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o
	$(CC) $^ $(CFLAGS) -DMOCK_SENDER_UNITTEST -L$(SDK_LIB_DIR) -ltraceifa -o $(BIN)/$(TARGET_SENDER)

$(TARGET_RECEIVER): $(BIN)/itc_r.o $(BIN)/itc_test_receiver.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o
	$(CC) $^ $(CFLAGS) -DMOCK_RECEIVER_UNITTEST -L$(SDK_LIB_DIR) -ltraceifa -o $(BIN)/$(TARGET_RECEIVER)

$(BIN)/itc_s.o: itc.c itc_impl.h itc.h itci_alloc.h itci_trans.h itc_threadmanager.h itc_proto.h itc_queue.h
	$(CC) -c  $(CFLAGS) -DMOCK_SENDER_UNITTEST -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_r.o: itc.c itc_impl.h itc.h itci_alloc.h itci_trans.h itc_threadmanager.h itc_proto.h itc_queue.h
	$(CC) -c  $(CFLAGS) -DMOCK_RECEIVER_UNITTEST -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_test_sender.o: itc_test_sender.c itc_impl.h itc.h itc_threadmanager.h moduleXyz.sig
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

$(BIN)/itc_test_receiver.o: itc_test_receiver.c itc_impl.h itc.h itc_threadmanager.h moduleXyz.sig
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

rs:
	$(BIN)/$(TARGET_SENDER)

rr:
	$(BIN)/$(TARGET_RECEIVER)

vs:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET_SENDER)

vr:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET_RECEIVER)

clean:
	rm -rf $(BIN)