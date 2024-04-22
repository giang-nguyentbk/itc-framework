ITC_UNITS	:= \
		src/ \
		src/allocators \
		src/helpers \
		src/transporters \

ITC_SRCS	+= \
		src/itc.c \
		src/allocators/itc_malloc.c \
		src/helpers/itc_queue.c \
		src/helpers/itc_threadmanager.c \
		src/transporters/itc_local.c \
		src/transporters/itc_lsocket.c \
		src/transporters/itc_sysvmq.c

ITC_OBJS	:= $(ITC_SRCS:%.c=$(BIN_DIR)/%.o)

ITC_INCS	:= \
		-I$(ITC_DIR)/if \
		-I$(ITC_DIR)/inc

all: $(ITC_OBJS) $(ITCLIB_DIR)/$(TARGET_LIBAR) $(ITCLIB_DIR)/$(TARGET_LIBSO)

# Build target 1 objects
$(BIN_DIR)/src/%.o: $(ITC_DIR)/src/%.c
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CC \t\t $@"
	@$(ITC_CC) $^ $(ITC_LDFLAGS) $(ITC_CFLAGS) $(ITC_INCS) -o $@

$(ITCLIB_DIR)/$(TARGET_LIBAR): $(ITC_OBJS)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  AR \t\t $@"
	@$(ITC_AR) $(ITC_ARFLAGS) $@ $^

$(ITCLIB_DIR)/$(TARGET_LIBSO): $(ITC_OBJS)
	@mkdir -p $(@D)
	@cd $(<D)
	@echo "  CCLD \t\t $@"
	@$(ITC_CC) $(ITC_SOFLAGS) $(ITCLIB_DIR)/$(TARGET_LIBSO) $^
