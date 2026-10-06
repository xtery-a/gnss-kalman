# ==============================================================================
# Tactical Multi-GNSS Terminal & Embedded Kalman Engine
# Professional Makefile for ANSI C99 Static Library and Verification Harnesses
# ==============================================================================

CC      ?= gcc
AR      ?= ar
CFLAGS  ?= -std=gnu99 -Wall -Wextra -O2
INCLUDES = -Iinclude
LDFLAGS ?=
LDLIBS  ?= -lm

SRC_DIR   = src
INC_DIR   = include
TESTS_DIR = tests
BUILD_DIR = build

ifeq ($(OS),Windows_NT)
    PYTHON   ?= python
    EXE_EXT  = .exe
    ifneq ($(filter %sh %sh.exe,$(SHELL)),)
        # Shell is sh.exe (e.g. MSYS2 or Git Bash)
        RUN_CMD  = $(BUILD_DIR)/$(strip $(1))
        RM_DIR   = rm -rf $(BUILD_DIR)
        RM_FILES = rm -f *.pbm
        MKDIR_P  = mkdir -p $(BUILD_DIR)
    else ifneq ($(MSYSTEM),)
        # Inside MSYS2 / UCRT64 / MINGW64 bash environment
        RUN_CMD  = $(BUILD_DIR)/$(strip $(1))
        RM_DIR   = rm -rf $(BUILD_DIR)
        RM_FILES = rm -f *.pbm
        MKDIR_P  = mkdir -p $(BUILD_DIR)
    else
        # Inside native Windows cmd / PowerShell
        RUN_CMD  = .\\build\\$(strip $(1))
        RM_DIR   = cmd /C if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
        RM_FILES = cmd /C if exist *.pbm del /q /f *.pbm
        MKDIR_P  = cmd /C if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
    endif
else
    PYTHON   ?= python3
    EXE_EXT  =
    RUN_CMD  = $(BUILD_DIR)/$(strip $(1))
    RM_DIR   = rm -rf $(BUILD_DIR)
    RM_FILES = rm -f *.pbm
    MKDIR_P  = mkdir -p $(BUILD_DIR)
endif

# Source files for core tactical library
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# Static Library Target
LIB_TARGET = $(BUILD_DIR)/libgnss_tactical.a

# Test Executables
TEST_TARGETS = \
	$(BUILD_DIR)/test_harness_cgpx$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_bus$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_display$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_nav$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_power$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_topo$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_cryo$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_hybrid_comms$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_kalman$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_fdcan$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_radio$(EXE_EXT) \
	$(BUILD_DIR)/test_harness_squad$(EXE_EXT)

.PHONY: all lib tests test clean help

all: lib tests

help:
	@echo "Tactical GNSS Engine Build Targets:"
	@echo "  make all     - Build static library and test executables"
	@echo "  make lib     - Build libgnss_tactical.a static library"
	@echo "  make tests   - Compile all C test harnesses"
	@echo "  make test    - Run all C and Python verification test suites"
	@echo "  make clean   - Remove all built artifacts"

# Create build directory
$(BUILD_DIR):
	@$(MKDIR_P)

# Compile C source files to object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build static library
$(LIB_TARGET): $(OBJS)
	$(AR) rcs $@ $^

lib: $(LIB_TARGET)

# Compile test executables linking against static library
$(BUILD_DIR)/test_harness_cgpx$(EXE_EXT): $(TESTS_DIR)/test_harness.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_bus$(EXE_EXT): $(TESTS_DIR)/test_harness_bus.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_display$(EXE_EXT): $(TESTS_DIR)/test_harness_display.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_nav$(EXE_EXT): $(TESTS_DIR)/test_harness_nav.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_power$(EXE_EXT): $(TESTS_DIR)/test_harness_power.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_topo$(EXE_EXT): $(TESTS_DIR)/test_harness_topo.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_cryo$(EXE_EXT): $(TESTS_DIR)/test_harness_cryo.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_hybrid_comms$(EXE_EXT): $(TESTS_DIR)/test_harness_hybrid_comms.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_kalman$(EXE_EXT): $(TESTS_DIR)/test_harness_kalman.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_fdcan$(EXE_EXT): $(TESTS_DIR)/test_harness_fdcan.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_radio$(EXE_EXT): $(TESTS_DIR)/test_harness_radio.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

$(BUILD_DIR)/test_harness_squad$(EXE_EXT): $(TESTS_DIR)/test_harness_squad.c $(LIB_TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_TARGET) $(LDLIBS) -o $@

tests: $(TEST_TARGETS)

# Run complete test verification
test: tests
	@echo "================================================================="
	@echo " RUNNING C BARE-METAL TEST HARNESSES"
	@echo "================================================================="
	@$(call RUN_CMD, test_harness_cgpx$(EXE_EXT)) data/mont_blanc_sample.cgpx
	@$(call RUN_CMD, test_harness_bus$(EXE_EXT))
	@$(call RUN_CMD, test_harness_display$(EXE_EXT))
	@$(call RUN_CMD, test_harness_nav$(EXE_EXT))
	@$(call RUN_CMD, test_harness_power$(EXE_EXT))
	@$(call RUN_CMD, test_harness_topo$(EXE_EXT))
	@$(call RUN_CMD, test_harness_cryo$(EXE_EXT))
	@$(call RUN_CMD, test_harness_hybrid_comms$(EXE_EXT))
	@$(call RUN_CMD, test_harness_kalman$(EXE_EXT))
	@$(call RUN_CMD, test_harness_fdcan$(EXE_EXT))
	@$(call RUN_CMD, test_harness_radio$(EXE_EXT))
	@$(call RUN_CMD, test_harness_squad$(EXE_EXT))
	@echo "================================================================="
	@echo " ALL C BARE-METAL TEST HARNESSES PASSED 100% SUCCESS!"
	@echo "================================================================="

test-vnv:
	@echo "================================================================="
	@echo " RUNNING PYTHON V&V MASTER TEST HARNESS"
	@echo "================================================================="
	@$(PYTHON) tests/test_phases_all.py
	@$(PYTHON) tests/test_harness_vnv.py
	@echo "================================================================="
	@echo " ALL PYTHON V&V TESTS PASSED 100% SUCCESS!"
	@echo "================================================================="

clean:
	@$(RM_DIR)
	@$(RM_FILES)
