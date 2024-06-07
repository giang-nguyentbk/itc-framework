ITCGWS_SRC		:= gateway/itc_gw.c
ITCGWS_INCS		:= \
			$(ITC_DIR)/if/itc.h \
			$(ITC_DIR)/inc/itc_impl.h \
			$(ITC_DIR)/inc/itc_proto.h \
			$(SW_DIR)/gateway/itc_gw_proto.h

ITCGWS_OBJ		:= gateway/itc_gw.o

ITCGWS_INCDIR		:= \
			-I$(ITC_DIR)/if \
			-I$(ITC_DIR)/inc \
			-I$(SW_DIR)/gateway \
			-I$(ROOT_DIR)/external/utilsFramework/include

all: $(BIN_DIR)/$(ITCGWS_OBJ) $(BIN_DIR)/gateway/$(TARGET_ITCGWS_W_LIBAR) $(BIN_DIR)/gateway/$(TARGET_ITCGWS_W_LIBSO)

# Build target 1 objects
$(BIN_DIR)/$(ITCGWS_OBJ): $(SW_DIR)/$(ITCGWS_SRC) $(ITCGWS_INCS)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CC \t\t $@"
	@$(ITC_CC) $(ITC_CFLAGS) -o $@ $(ITCGWS_INCDIR) $<

$(BIN_DIR)/gateway/$(TARGET_ITCGWS_W_LIBAR): $(BIN_DIR)/$(ITCGWS_OBJ)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  LINKING \t $@"
	@$(ITC_CC) $< -L$(ITCLIB_DIR) -litca -L$(UTILSFWK_TRACE_LIBDIR) -ltracea -o $@

$(BIN_DIR)/gateway/$(TARGET_ITCGWS_W_LIBSO): $(BIN_DIR)/$(ITCGWS_OBJ)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  LINKING \t $@"
	@$(ITC_CC) $< -L$(ITCLIB_DIR) -litc -L$(UTILSFWK_TRACE_LIBDIR) -ltrace -o $@