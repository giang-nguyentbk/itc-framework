TARGET = itc_sysvmq_test
BIN = ./bin
CFLAGS= -g -Wall -Wextra -DUNITTEST -DSYSVMQ_TRANS_UNITTEST

INCLUDE_DIR =
INCLUDE_DIR += -I ../../../if
INCLUDE_DIR += -I ../../../inc

SRC_DIR =
SRC_DIR += -I ../../../src/transporters
SRC_DIR += -I ../../../src/helpers
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

$(TARGET): $(BIN)/itc_sysvmq.o $(BIN)/itc_sysvmq_test.o $(BIN)/itc_threadmanager.o
	$(CC) $^ $(CFLAGS) -o $(BIN)/$(TARGET)

$(BIN)/itc_sysvmq.o: itc_sysvmq.c itc.h itc_impl.h itci_trans.h itc_threadmanager.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_sysvmq_test.o: itc_sysvmq_test.c itc.h itci_trans.h itc_impl.h itc_threadmanager.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

$(BIN)/itc_threadmanager.o: itc_threadmanager.c itc_impl.h
	$(CC) -c $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

run:
	$(BIN)/$(TARGET)

val:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)