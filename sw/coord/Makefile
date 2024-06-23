ITCCOORD_SRC_DIR	:= $(SW_DIR)/coord

ITCCOORD_SRCS		=
ITCCOORD_SRCS		+= itccoord.c

ITCCOORD_OBJS		:= $(ITCCOORD_SRCS:%.c=$(OBJ_DIR)/%.o)

ITCCOORD_INCDIR		:= \
			-I$(ITC_DIR)/if \
			-I$(ITC_DIR)/inc \
			-I$(SDK_INC_DIR)
#			-I$(MOCK_UTILSFWK_INCDIR)

all: $(ITCCOORD_OBJS) $(EXEC_DIR)/$(TARGET_ITCCOORD_W_LIBAR) $(EXEC_DIR)/$(TARGET_ITCCOORD_W_LIBSO)

# Build target 1 objects
$(OBJ_DIR)/%.o: $(ITCCOORD_SRC_DIR)/%.c
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CC \t\t $@"
	@$(ITC_CC) $(ITC_CFLAGS) $(ITCCOORD_INCDIR) -o $@ $<

$(EXEC_DIR)/$(TARGET_ITCCOORD_W_LIBAR): $(ITCCOORD_OBJS)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CCLD \t\t $@"
	@$(ITC_CC) $^ -L$(LIB_DIR) -litca -L$(SDK_LIB_DIR) -ltraceifa -o $@
	@echo "  ROOT \t\t $@"
	@sudo setcap 'CAP_SYS_RESOURCE=+ep' $@
#	@$(ITC_CC) $^ -L$(LIB_DIR) -litca -L$(MOCK_UTILSFWK_LIBDIR) -ltraceifa -o $@

$(EXEC_DIR)/$(TARGET_ITCCOORD_W_LIBSO): $(ITCCOORD_OBJS)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CCLD \t\t $@"
	@$(ITC_CC) $^ -L$(LIB_DIR) -litc -L$(SDK_LIB_DIR) -ltraceif -o $@
	@echo "  ROOT \t\t $@"
	@sudo setcap 'CAP_SYS_RESOURCE=+ep' $@
#	@$(ITC_CC) $^ -L$(LIB_DIR) -litc -L$(MOCK_UTILSFWK_LIBDIR) -ltraceif -o $@