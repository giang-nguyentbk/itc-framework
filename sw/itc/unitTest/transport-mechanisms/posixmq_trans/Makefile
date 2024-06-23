TARGET = itc_posixmq_test
BIN = ./bin
CFLAGS= -g -Wall -Wextra -DUNITTEST

SDK_SYSROOT_DIR		:= $(SDKSYSROOT)
SDK_USR_DIR		:= $(SDK_SYSROOT_DIR)/usr
SDK_LIB_DIR		:= $(SDK_USR_DIR)/lib
SDK_INC_DIR		:= $(SDK_USR_DIR)/include

INCLUDE_DIR =
INCLUDE_DIR += -I ../../../if
INCLUDE_DIR += -I ../../../inc
INCLUDE_DIR += -I $(SDK_INC_DIR)

SRC_DIR =
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

$(TARGET): $(BIN)/itc_posixmq.o $(BIN)/itc_posixmq_test.o
	$(CC) $^ $(CFLAGS) -L$(SDK_LIB_DIR) -ltraceifa -o $(BIN)/$(TARGET)

$(BIN)/itc_posixmq.o: itc_posixmq.c itc.h itc_impl.h itci_trans.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_posixmq_test.o: itc_posixmq_test.c itc.h itci_trans.h itc_impl.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

run:
	$(BIN)/$(TARGET)

val:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)