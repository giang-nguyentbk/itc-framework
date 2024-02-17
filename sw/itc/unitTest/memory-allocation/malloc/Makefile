TARGET = itc_malloc_test
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

$(TARGET): $(BIN)/itc_malloc.o $(BIN)/itc_malloc_test.o
	$(CC) $^ -o $(BIN)/$(TARGET)

$(BIN)/itc_malloc.o: itc_malloc.c itc.h itc_impl.h itci_alloc.h
	$(CC) -c -o $@ $(INCLUDE_DIR) $(SRC_DIR) $<

$(BIN)/itc_malloc_test.o: itc_malloc_test.c itc.h itci_alloc.h itc_impl.h moduleXyz.sig
	$(CC) -c -o $@ $(INCLUDE_DIR) $(SRC_DIR) $(SIG_DIR) $<

run:
	$(BIN)/$(TARGET) 0
# 0 = ITC_MALLOC

val:
	sudo valgrind --leak-check=yes $(BIN)/$(TARGET) 0

clean:
	rm -rf $(BIN)/*