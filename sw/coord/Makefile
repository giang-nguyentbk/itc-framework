ITCCOORD_SRC		:= coord/itccoord.c
ITCCOORD_INCS		:= \
			$(ITC_DIR)/if/itc.h \
			$(ITC_DIR)/inc/itc_impl.h \
			$(ITC_DIR)/inc/itc_proto.h \
			$(ITC_DIR)/inc/itc_queue.h

ITCCOORD_OBJ		:= coord/itccoord.o

ITCCOORD_INCDIR		:= \
			-I$(ITC_DIR)/if \
			-I$(ITC_DIR)/inc

all: $(BIN_DIR)/$(ITCCOORD_OBJ) $(BIN_DIR)/coord/$(TARGET_ITCCORD_W_LIBAR) $(BIN_DIR)/coord/$(TARGET_ITCCORD_W_LIBSO)

# Build target 1 objects
$(BIN_DIR)/$(ITCCOORD_OBJ): $(SW_DIR)/$(ITCCOORD_SRC) $(ITCCOORD_INCS)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CC \t\t $@"
	@$(ITC_CC) $(ITC_CFLAGS) -o $@ $(ITCCOORD_INCDIR) $<

$(BIN_DIR)/coord/$(TARGET_ITCCORD_W_LIBAR): $(BIN_DIR)/$(ITCCOORD_OBJ)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  LINKING \t $@"
	@$(ITC_CC) $< -L$(ITCLIB_DIR) -litca -o $@

$(BIN_DIR)/coord/$(TARGET_ITCCORD_W_LIBSO): $(BIN_DIR)/$(ITCCOORD_OBJ)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  LINKING \t $@"
	@$(ITC_CC) $< -L$(ITCLIB_DIR) -litc -o $@