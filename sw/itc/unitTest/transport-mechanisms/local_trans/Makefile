TARGET = itc_local_test
BIN = ./bin
CFLAGS= -g -Wall -Wextra

INCLUDE_DIR =
INCLUDE_DIR += -I ../../../if
INCLUDE_DIR += -I ../../../inc

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

$(TARGET): $(BIN)/itc_local.o $(BIN)/itc_local_test.o
	$(CC) $^ $(CFLAGS) -o $(BIN)/$(TARGET)

$(BIN)/itc_local.o: itc_local.c itc.h itc_impl.h itci_trans.h
	$(CC) -c -DUNITTEST -DLOCAL_TRANS_UNITTEST $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_local_test.o: itc_local_test.c itc.h itci_trans.h itc_impl.h
	$(CC) -c -DUNITTEST -DLOCAL_TRANS_UNITTEST $(CFLAGS) -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

run:
	$(BIN)/$(TARGET)

val:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)