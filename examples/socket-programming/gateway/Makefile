BIN = ./bin
TARGET_1 = itc_gw
TARGET_2 = itc_gw_2
TARGET_3 = itc_coord
CFLAGS = -g -Wall -Werror -Wextra -lpthread #-DMUTEX_TRACE_TIME_UNITTEST #-DMUTEX_TRACE_UNITTEST
CC = gcc

INCLUDE_DIR =
INCLUDE_DIR += -I ../../../sw/itc/if
INCLUDE_DIR += -I ../../../sw/itc/inc
INCLUDE_DIR += -I ./

SRC_DIR =
SRC_DIR += -I ../../../sw/itc/src
SRC_DIR += -I ../../../sw/itc/src/allocators
SRC_DIR += -I ../../../sw/itc/src/helpers
SRC_DIR += -I ../../../sw/itc/src/transporters
SRC_DIR += -I ../../../sw/coord
SRC_DIR += -I ./

vpath %.h 	$(INCLUDE_DIR)
vpath %.c 	$(SRC_DIR)

.PHONY: all

all: create_bin $(TARGET_1) $(TARGET_2) $(TARGET_3)

create_bin:
	@mkdir -p $(BIN)

# Build itc gateway's executable files
$(TARGET_1): $(BIN)/itc_gw_daemon.o $(BIN)/itc.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o
	$(CC) $^ $(CFLAGS) -o $(BIN)/$(TARGET_1)

$(TARGET_2): $(BIN)/itc_gw_2.o $(BIN)/itc.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o
	$(CC) $^ $(CFLAGS) -o $(BIN)/$(TARGET_2)

$(TARGET_3): $(BIN)/itccoord.o $(BIN)/itc.o $(BIN)/itc_threadmanager.o $(BIN)/itc_queue.o $(BIN)/itc_malloc.o \
	   $(BIN)/itc_local.o $(BIN)/itc_lsocket.o $(BIN)/itc_sysvmq.o
	$(CC) $^ $(CFLAGS) -o $(BIN)/$(TARGET_3)

# Build itc gateway's object files
$(BIN)/itc_gw_daemon.o: itc_gw_daemon.c itc.h itc_impl.h itc_proto.h itc_gw_proto.h
	$(CC) -c  $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_gw_2.o: itc_gw_2.c itc.h itc_impl.h itc_proto.h itc_gw_proto.h
	$(CC) -c  $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

# Build itc coordinator's object file
$(BIN)/itccoord.o: itccoord.c itc_impl.h itc.h itc_proto.h itc_queue.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

# Build itc core lib's object files
$(BIN)/itc.o: itc.c itc_impl.h itc.h itci_alloc.h itci_trans.h itc_threadmanager.h itc_proto.h itc_queue.h
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


# Use "ps -xj" to find the daemon PID and kill it 
r1:
	@$(BIN)/$(TARGET_1) -n "/host_1/" -d

v1:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET_1) -n "/host_1/"

r2:
	@$(BIN)/$(TARGET_2) "/host_2/"

v2:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET_2) "/host_2/"

rc:
	@$(BIN)/$(TARGET_3) -d

vc:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET_3)

clean-exe:
	rm -rf $(BIN)

cle:
	rm -rf $(BIN)

clear-log:
	rm -rf *.log

cll:
	rm -rf *.log

cla:
	rm -rf $(BIN) *.log