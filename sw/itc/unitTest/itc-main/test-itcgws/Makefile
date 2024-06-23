BIN = ./bin
TARGET_1 = itc_gw
TARGET_2 = itc_gw_2
TARGET_3 = itc_coord
TARGET_4 = itc_coord_peer
TARGET_SENDER = itc_test_sender
TARGET_RECEIVER = itc_test_receiver

CFLAGS = -g -Wall -Werror -Wextra -lpthread #-DMUTEX_TRACE_TIME_UNITTEST #-DMUTEX_TRACE_UNITTEST
CC = gcc

ROOT_DIR := $(shell git rev-parse --show-toplevel)
UTILSFWK_TRACE_LIBDIR = $(ROOT_DIR)/external/utilsFramework/lib
UTILSFWK_TRACE_INCDIR = $(ROOT_DIR)/external/utilsFramework/include


INCLUDE_DIR =
INCLUDE_DIR += -I ../../../if
INCLUDE_DIR += -I ../../../inc
INCLUDE_DIR += -I ./
INCLUDE_DIR += -I$(UTILSFWK_TRACE_INCDIR)

SRC_DIR =
SRC_DIR += -I ../../../src
SRC_DIR += -I ../../../src/allocators
SRC_DIR += -I ../../../src/helpers
SRC_DIR += -I ../../../src/transporters
SRC_DIR += -I ../../../../coord
SRC_DIR += -I ./peer-source-files
SRC_DIR += -I ./

SIG_DIR =
SIG_DIR += -I ../../../unitTest

vpath %.h 	$(INCLUDE_DIR)
vpath %.c 	$(SRC_DIR)
vpath %.sig 	$(SIG_DIR)

.PHONY: all

all: create_bin $(TARGET_1) $(TARGET_2) $(TARGET_3) $(TARGET_4) $(TARGET_SENDER) $(TARGET_RECEIVER)

create_bin:
	@mkdir -p $(BIN)

# Build itc executable files
$(TARGET_1): $(BIN)/itc_gw_daemon.o $(BIN)/itc.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o $(BIN)/itc_posixmq.o
	$(CC) $^ $(CFLAGS) -L$(UTILSFWK_TRACE_LIBDIR) -ltracea -o $(BIN)/$(TARGET_1)

$(TARGET_2): $(BIN)/itc_gw_2_daemon.o $(BIN)/itc_peer.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o $(BIN)/itc_posixmq.o
	$(CC) $^ $(CFLAGS) -L$(UTILSFWK_TRACE_LIBDIR) -ltracea -o $(BIN)/$(TARGET_2)

$(TARGET_3): $(BIN)/itccoord.o $(BIN)/itc.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o $(BIN)/itc_posixmq.o
	$(CC) $^ $(CFLAGS) -L$(UTILSFWK_TRACE_LIBDIR) -ltracea -o $(BIN)/$(TARGET_3)
	sudo setcap 'CAP_SYS_RESOURCE=+ep' $(BIN)/$(TARGET_3)

$(TARGET_4): $(BIN)/itccoord_peer.o $(BIN)/itc_peer.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o $(BIN)/itc_posixmq.o
	$(CC) $^ $(CFLAGS) -L$(UTILSFWK_TRACE_LIBDIR) -ltracea -o $(BIN)/$(TARGET_4)
	sudo setcap 'CAP_SYS_RESOURCE=+ep' $(BIN)/$(TARGET_4)

$(TARGET_SENDER): $(BIN)/itc_test_sender.o $(BIN)/itc.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o $(BIN)/itc_posixmq.o
	$(CC) $^ $(CFLAGS) -L$(UTILSFWK_TRACE_LIBDIR) -ltracea -o $(BIN)/$(TARGET_SENDER)

$(TARGET_RECEIVER): $(BIN)/itc_test_receiver.o $(BIN)/itc_peer.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o $(BIN)/itc_posixmq.o
	$(CC) $^ $(CFLAGS) -L$(UTILSFWK_TRACE_LIBDIR) -ltracea -o $(BIN)/$(TARGET_RECEIVER)

# Build sender and receiver test files
$(BIN)/itc_test_sender.o: itc_test_sender.c itc_impl.h itc.h itc_threadmanager.h moduleXyz.sig
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

$(BIN)/itc_test_receiver.o: itc_test_receiver.c itc_impl.h itc.h itc_threadmanager.h moduleXyz.sig
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

# Build itc gateway's object files
$(BIN)/itc_gw_daemon.o: itc_gw_daemon.c itc.h itc_impl.h itc_proto.h itc_gw_proto.h
	$(CC) -c  $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_gw_2_daemon.o: itc_gw_2_daemon.c itc.h itc_impl.h itc_proto.h itc_gw_proto.h
	$(CC) -c  $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

# Build itc coordinator's object file
$(BIN)/itccoord.o: itccoord.c itc_impl.h itc.h itc_proto.h itc_queue.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

$(BIN)/itccoord_peer.o: itccoord_peer.c itc_impl.h itc.h itc_proto.h itc_queue.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

# Build itc core lib's object files
$(BIN)/itc.o: itc.c itc_impl.h itc.h itci_alloc.h itci_trans.h itc_threadmanager.h itc_proto.h itc_queue.h
	$(CC) -c  $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_peer.o: itc_peer.c itc_impl.h itc.h itci_alloc.h itci_trans.h itc_threadmanager.h itc_proto.h itc_queue.h
	$(CC) -c  $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

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

$(BIN)/itc_posixmq.o: itc_posixmq.c itc_impl.h itci_trans.h itc.h
	$(CC) -c $(CFLAGS) -lrt -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<


# Use "ps -xj" to find the daemon PID and kill it 
r1:
	@$(BIN)/$(TARGET_1) -n "/host_1/"

v1:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all --track-origins=yes $(BIN)/$(TARGET_1) -n "/host_1/"

r2:
	@$(BIN)/$(TARGET_2) -n "/host_2/"

v2:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all --track-origins=yes $(BIN)/$(TARGET_2) -n "/host_2/"

rc:
	@$(BIN)/$(TARGET_3)

vc:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all --track-origins=yes $(BIN)/$(TARGET_3)

rcp:
	@$(BIN)/$(TARGET_4)

vcp:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all --track-origins=yes $(BIN)/$(TARGET_4)

rs:
	$(BIN)/$(TARGET_SENDER)

rr:
	$(BIN)/$(TARGET_RECEIVER)

vs:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all --track-origins=yes $(BIN)/$(TARGET_SENDER)

vr:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all --track-origins=yes $(BIN)/$(TARGET_RECEIVER)

# After this we have to manually "ps -xj" to find the daemon PID and kill it
runs:
	@$(BIN)/$(TARGET_3) -d
	@sleep 1
	@$(BIN)/$(TARGET_1) -n "/ubuntu/" -d
	@sleep 1
	@$(BIN)/$(TARGET_SENDER)

runr:
	@$(BIN)/$(TARGET_4) -d
	@sleep 1
	@$(BIN)/$(TARGET_2) -n "/debian/" -d
	@sleep 1
	@$(BIN)/$(TARGET_RECEIVER)


# After this we have to manually "ps -xj" to find the daemon PID and kill it or kill it automatically by
proc_itcgws = $(shell ps -ef | grep itc_gw | grep -v grep | awk '{ print $$2 ; }')
proc_itccoord = $(shell ps -ef | grep itc_coord | grep -v grep | awk '{ print $$2 ; }')
kill_itcgws = $(if $(proc_itcgws), "kill" "-15" $(proc_itcgws), "echo" "no matching processes")
kill_itccoord = $(if $(proc_itccoord), "kill" "-15" $(proc_itccoord), "echo" "no matching processes")
killa:
	@echo 'proc_itcgws == ['$(proc_itcgws)']'
	@$(kill_itcgws)
	@echo 'proc_itccoord == ['$(proc_itccoord)']'
	@$(kill_itccoord)


clean-exe:
	rm -rf $(BIN)

clear-log:
	rm -rf *.log

cla:
	rm -rf $(BIN) *.log