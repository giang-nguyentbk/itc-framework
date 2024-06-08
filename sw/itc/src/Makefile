ITC_SRC_DIR	:= $(ITC_DIR)/src


ITC_SRCS	+= \
		itc.c \
		allocators/itc_malloc.c \
		helpers/itc_queue.c \
		helpers/itc_threadmanager.c \
		transporters/itc_local.c \
		transporters/itc_lsocket.c \
		transporters/itc_sysvmq.c

ITC_OBJS	:= $(ITC_SRCS:%.c=$(OBJ_DIR)/%.o)

ITC_INCS	:= \
		-I$(ITC_DIR)/if \
		-I$(ITC_DIR)/inc \
		-I$(SDK_INC_DIR)
#		-I$(MOCK_UTILSFWK_INCDIR)

all: first $(ITC_OBJS) $(LIB_DIR)/$(TARGET_LIBAR) $(LIB_DIR)/$(TARGET_LIBSO) install-header-files-itc

first:
	@echo $(SDK_SYSROOT_DIR)

# Build target 1 objects
$(OBJ_DIR)/%.o: $(ITC_SRC_DIR)/%.c
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CC \t\t $@"
	@$(ITC_CC) $(ITC_LDFLAGS) $(ITC_CFLAGS) $(ITC_INCS) -o $@ $^

$(LIB_DIR)/$(TARGET_LIBAR): $(ITC_OBJS)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  AR \t\t $@"
	@$(ITC_AR) $(ITC_ARFLAGS) $@ $^

$(LIB_DIR)/$(TARGET_LIBSO): $(ITC_OBJS)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CCLD \t\t $@"
	@$(ITC_CC) $(ITC_SOFLAGS) -o $@ $^

install-header-files-itc:
	@mkdir -p $(INCLUDE_DIR)
	@echo "  CPY \t\t $(ITC_DIR)/if"
	@cp -rf $(ITC_DIR)/if/*.h $(INCLUDE_DIR)