# Makefile for SPMC Queue Test Cases
CC = mpicc
CFLAGS = -Wall -Wextra -O2 -std=c99 -g
LDFLAGS = -lm

# Directories
SRC_DIR = ../src
TEST_DIR = .
BUILD_DIR = build
BIN_DIR = bin
MPI_LIB_DIR = ../../mpi_lib

# Include paths
INCLUDES = -I$(SRC_DIR) -I$(MPI_LIB_DIR)/include

# Library paths  
LIBS = -L$(MPI_LIB_DIR)/lib -lmpi_wrapper

# Add rpath for shared libraries
LDFLAGS += -Wl,-rpath,$(MPI_LIB_DIR)/lib

# Source files for the SPMC queue library (excluding main)
LIB_SOURCES = $(SRC_DIR)/spmc_queue.c $(SRC_DIR)/bitmap.c
LIB_OBJECTS = $(LIB_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Test source files
TEST_SOURCES = $(wildcard test_spmc_*.c)
TEST_TARGETS = $(TEST_SOURCES:%.c=$(BIN_DIR)/%)

.PHONY: all clean dirs help run-all check-deps

all: dirs $(TEST_TARGETS)

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# Build library objects
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling library $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build test executables
$(BIN_DIR)/test_spmc_%: $(TEST_DIR)/test_spmc_%.c $(LIB_OBJECTS)
	@echo "Building test $@..."
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_OBJECTS) $(LIBS) $(LDFLAGS) -o $@

# Build bitmap functions test (with MPI dependencies)
$(BIN_DIR)/test_bitmap_functions: $(TEST_DIR)/test_bitmap_functions.c $(LIB_OBJECTS)
	@echo "Building bitmap functions test $@..."
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_OBJECTS) $(LIBS) $(LDFLAGS) -o $@

# Build sync_bitmap_row test (with MPI support)
$(BIN_DIR)/test_sync_bitmap_row: $(TEST_DIR)/test_sync_bitmap_row.c $(LIB_OBJECTS)
	@echo "Building sync_bitmap_row test $@..."
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB_OBJECTS) $(LIBS) $(LDFLAGS) -o $@

# Clean build files
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Clean complete"

# Run individual tests
run-basic: $(BIN_DIR)/test_spmc_basic
	@echo "Running basic functionality test with 2 processes..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 2 $(BIN_DIR)/test_spmc_basic

run-competition: $(BIN_DIR)/test_spmc_competition
	@echo "Running multiple consumer competition test with 4 processes..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 4 $(BIN_DIR)/test_spmc_competition

run-stress: $(BIN_DIR)/test_spmc_stress
	@echo "Running high throughput stress test with 4 processes..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 4 $(BIN_DIR)/test_spmc_stress

run-edge-cases: $(BIN_DIR)/test_spmc_edge_cases
	@echo "Running edge cases test with 2 processes..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 2 $(BIN_DIR)/test_spmc_edge_cases

run-waitfree: $(BIN_DIR)/test_spmc_waitfree
	@echo "Running waitfree property test with 3 processes..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 3 $(BIN_DIR)/test_spmc_waitfree

run-correctness: $(BIN_DIR)/test_spmc_correctness
	@echo "Running correctness verification test with 3 processes..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 3 $(BIN_DIR)/test_spmc_correctness

run-comprehensive: $(BIN_DIR)/test_spmc_comprehensive
	@echo "Running comprehensive test suite with 4 processes..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 4 $(BIN_DIR)/test_spmc_comprehensive

run-bitmap: $(BIN_DIR)/test_bitmap_functions
	@echo "Running bitmap functions unit test..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH $(BIN_DIR)/test_bitmap_functions

run-overtaking: $(BIN_DIR)/test_spmc_overtaking
	@echo "Running consumer overtaking test with 4 processes..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 4 $(BIN_DIR)/test_spmc_overtaking

run-sync-bitmap: $(BIN_DIR)/test_sync_bitmap_row
	@echo "Running sync_bitmap_row data race test with 4 processes..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 4 $(BIN_DIR)/test_sync_bitmap_row

# Run all tests sequentially
run-all: all
	@echo "Running complete SPMC queue test suite..."
	@echo ""
	@echo "=== Test 1: Basic Functionality ==="
	@$(MAKE) run-basic
	@echo ""
	@echo "=== Test 2: Multiple Consumer Competition ==="
	@$(MAKE) run-competition
	@echo ""
	@echo "=== Test 3: High Throughput Stress ==="
	@$(MAKE) run-stress
	@echo ""
	@echo "=== Test 4: Edge Cases ==="
	@$(MAKE) run-edge-cases
	@echo ""
	@echo "=== Test 5: Waitfree Properties ==="
	@$(MAKE) run-waitfree
	@echo ""
	@echo "=== Test 6: Correctness Verification ==="
	@$(MAKE) run-correctness
	@echo ""
	@echo "=== Test 7: Comprehensive Suite ==="
	@$(MAKE) run-comprehensive
	@echo ""
	@echo "=== Test 8: Bitmap Functions ==="
	@$(MAKE) run-bitmap
	@echo ""
	@echo "=== Test 9: Consumer Overtaking ==="
	@$(MAKE) run-overtaking
	@echo ""
	@echo "=== Test 10: Sync Bitmap Row Data Race ==="
	@$(MAKE) run-sync-bitmap
	@echo ""
	@echo "=== All Tests Completed ==="

# Run with different process counts
run-scalability: all
	@echo "Running scalability tests with different process counts..."
	@echo ""
	@echo "=== 2 Processes (1P + 1C) ==="
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 2 $(BIN_DIR)/test_spmc_comprehensive
	@echo ""
	@echo "=== 4 Processes (1P + 3C) ==="
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 4 $(BIN_DIR)/test_spmc_comprehensive
	@echo ""
	@echo "=== 6 Processes (1P + 5C) ==="
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 6 $(BIN_DIR)/test_spmc_comprehensive
	@echo ""
	@echo "=== 8 Processes (1P + 7C) ==="
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun -np 8 $(BIN_DIR)/test_spmc_comprehensive

# Check for dependencies
check-deps:
	@echo "Checking test dependencies..."
	@which mpicc > /dev/null || (echo "Error: mpicc not found" && exit 1)
	@test -f $(MPI_LIB_DIR)/include/mpi_lib.h || (echo "Error: mpi_lib.h not found" && exit 1)
	@test -f $(MPI_LIB_DIR)/lib/libmpi_wrapper.a || (echo "Error: libmpi_wrapper.a not found" && exit 1)
	@test -f $(SRC_DIR)/spmc_queue.h || (echo "Error: spmc_queue.h not found" && exit 1)
	@test -f $(SRC_DIR)/spmc_queue.c || (echo "Error: spmc_queue.c not found" && exit 1)
	@test -f $(SRC_DIR)/bitmap.h || (echo "Error: bitmap.h not found" && exit 1)
	@test -f $(SRC_DIR)/bitmap.c || (echo "Error: bitmap.c not found" && exit 1)
	@echo "All dependencies found"

# Help target
help:
	@echo "Available targets:"
	@echo "  all               - Build all test executables"
	@echo "  clean             - Remove build files"
	@echo "  check-deps        - Check if dependencies are available"
	@echo ""
	@echo "Individual tests:"
	@echo "  run-basic         - Test basic functionality (2 processes)"
	@echo "  run-competition   - Test multiple consumer competition (4 processes)"
	@echo "  run-stress        - Test high throughput stress (4 processes)"
	@echo "  run-edge-cases    - Test edge cases and boundary conditions (2 processes)"
	@echo "  run-waitfree      - Test waitfree properties (3 processes)"
	@echo "  run-correctness   - Test correctness verification (3 processes)"
	@echo "  run-comprehensive - Test comprehensive suite (4 processes)"
	@echo "  run-bitmap        - Test bitmap functions unit tests (no MPI)"
	@echo "  run-overtaking    - Test consumer overtaking scenarios (4 processes)"
	@echo ""
	@echo "Test suites:"
	@echo "  run-all           - Run all tests sequentially"
	@echo "  run-scalability   - Test with different process counts (2,4,6,8)"
	@echo ""
	@echo "Usage examples:"
	@echo "  make run-all                    - Run complete test suite"
	@echo "  make run-stress                 - Run just the stress test"
	@echo "  make run-scalability           - Test scalability across process counts"

# List available tests
list-tests:
	@echo "Available test cases:"
	@echo "1. test_spmc_basic          - Basic enqueue/dequeue functionality"
	@echo "2. test_spmc_competition    - Multiple consumer competition"
	@echo "3. test_spmc_stress         - High throughput stress testing"
	@echo "4. test_spmc_edge_cases     - Edge cases and boundary conditions"
	@echo "5. test_spmc_waitfree       - Waitfree property verification"
	@echo "6. test_spmc_correctness    - Data integrity and correctness"
	@echo "7. test_spmc_comprehensive  - Comprehensive test suite"
	@echo "8. test_bitmap_functions    - Bitmap functions unit tests"
	@echo "9. test_spmc_overtaking     - Consumer overtaking producer scenarios"
	@echo "8. test_bitmap_functions    - Bitmap functions unit tests"