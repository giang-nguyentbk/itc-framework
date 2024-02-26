TARGET = itc_local_test
BIN = ../bin

INCLUDE_DIR =
INCLUDE_DIR += -I ../../../if
INCLUDE_DIR += -I ../../../inc

SRC_DIR =
SRC_DIR += -I ../../../src
SRC_DIR += -I ./

SIG_DIR =
SIG_DIR += -I ../../../unitTest

vpath %.h 	$(INCLUDE_DIR)
vpath %.c 	$(SRC_DIR)
vpath %.sig 	$(SIG_DIR)

.PHONY: all

all: $(TARGET)

$(TARGET): $(BIN)/itc_local.o $(BIN)/itc_local_test.o
	$(CC) $^ -o $(BIN)/$(TARGET)

$(BIN)/itc_local.o: itc_local.c itc.h itc_impl.h itci_trans.h
	$(CC) -DUNITTEST -c -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_local_test.o: itc_local_test.c itc.h itci_trans.h itc_impl.h
	$(CC) -c -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

run:
	$(BIN)/$(TARGET)

val:
	sudo valgrind --leak-check=yes --leak-check=full --show-leak-kinds=all $(BIN)/$(TARGET)

clean:
	rm -rf $(BIN)/*