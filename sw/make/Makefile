# --------------------------------------------------
# Call Makefiles for init setups and build database
# --------------------------------------------------
include Makefile.config

# --------------------------------------------------
# Call Makefiles for modules of the project
# --------------------------------------------------
include $(SW_DIR)/itc/src/Makefile
include $(SW_DIR)/coord/Makefile
include $(SW_DIR)/gateway/Makefile

all: rootMakefile

rootMakefile:
	@echo
	@echo "Run sw/make/Makefile: Compilation for $(TARGET_LIBSO) done! [OK]"
	@echo "Run sw/make/Makefile: Compilation for $(TARGET_ITCCORD_W_LIBAR) done! [OK]"
	@echo "Run sw/make/Makefile: Compilation for $(TARGET_ITCCORD_W_LIBSO) done! [OK]"
	@echo "Run sw/make/Makefile: Compilation for $(TARGET_ITCGWS_W_LIBAR) done! [OK]"
	@echo "Run sw/make/Makefile: Compilation for $(TARGET_ITCGWS_W_LIBSO) done! [OK]"
	@echo
	@echo "Note that: you have to:"
	@echo "In bash shell: Run \"export LD_LIBRARY_PATH=<your-path-to-libitc.so>:\$$LD_LIBRARY_PATH\" before running $(TARGET_ITCCORD_W_LIBSO) or $(TARGET_ITCGWS_W_LIBSO)!"
	@echo "In tcsh shell: Run \"setenv LD_LIBRARY_PATH <your-path-to-libitc.so>:\$$LD_LIBRARY_PATH\" before running $(TARGET_ITCCORD_W_LIBSO) or $(TARGET_ITCGWS_W_LIBSO)!"
	@echo "or \"setenv LD_LIBRARY_PATH <your-path-to-libitc.so>:\", if LD_LIBRARY_PATH is undefined variable!"

clean:
	@echo
	@echo "  RMV \t\t $(BIN_DIR)"
	@$(RMV) $(BIN_DIR)
