ITCGWS_SRC_DIR		:= $(SW_DIR)/gateway

ITCGWS_SRCS		= 
ITCGWS_SRCS		+= itc_gw.c

ITCGWS_OBJ		:= $(ITCGWS_SRCS:%.c=$(OBJ_DIR)/%.o)

ITCGWS_INCDIR		:= \
			-I$(ITC_DIR)/if \
			-I$(ITC_DIR)/inc \
			-I$(SW_DIR)/gateway \
			-I$(SDK_INC_DIR)
#			-I$(MOCK_UTILSFWK_INCDIR)

all: $(ITCGWS_OBJ) $(EXEC_DIR)/$(TARGET_ITCGWS_W_LIBAR) $(EXEC_DIR)/$(TARGET_ITCGWS_W_LIBSO)

# Build target 1 objects
$(OBJ_DIR)/%.o: $(ITCGWS_SRC_DIR)/%.c
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CC \t\t $@"
	@$(ITC_CC) $(ITC_CFLAGS) $(ITCGWS_INCDIR) -o $@ $<

$(EXEC_DIR)/$(TARGET_ITCGWS_W_LIBAR): $(ITCGWS_OBJ)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CCLD \t\t $@"
	@$(ITC_CC) $^ -L$(LIB_DIR) -litca -L$(SDK_LIB_DIR) -ltraceifa -o $@
	@echo "  ROOT \t\t $@"
	@sudo setcap 'CAP_SYS_RESOURCE=+ep' $@
#	@$(ITC_CC) $^ -L$(LIB_DIR) -litca -L$(MOCK_UTILSFWK_LIBDIR) -ltraceifa -o $@

$(EXEC_DIR)/$(TARGET_ITCGWS_W_LIBSO): $(ITCGWS_OBJ)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CCLD \t\t $@"
	@$(ITC_CC) $^ -L$(LIB_DIR) -litc -L$(SDK_LIB_DIR) -ltraceif -o $@
	@echo "  ROOT \t\t $@"
	@sudo setcap 'CAP_SYS_RESOURCE=+ep' $@
#	@$(ITC_CC) $^ -L$(LIB_DIR) -litc -L$(MOCK_UTILSFWK_LIBDIR) -ltraceif -o $@