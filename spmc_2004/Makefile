# Makefile for SPMC Queue with mpi_lib
CC = mpicc
CFLAGS = -Wall -Wextra -O2 -std=c99 -g
LDFLAGS = -lm

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
MPI_LIB_DIR = ../mpi_lib
MPI_LIB_INCLUDE = $(MPI_LIB_DIR)/include
MPI_LIB_LIB = $(MPI_LIB_DIR)/lib

# Include paths
INCLUDES = -I$(SRC_DIR) -I$(MPI_LIB_INCLUDE)

# Library paths
LIBS = -L$(MPI_LIB_DIR)/lib -lmpi_wrapper

# Add rpath to link shared libraries properly
LDFLAGS += -Wl,-rpath,$(MPI_LIB_DIR)/lib

# Source files
SOURCES = $(SRC_DIR)/main_spmc.c $(SRC_DIR)/spmc_queue.c
# SOURCES =  $(SRC_DIR)/spmc_queue.c
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Target executable
TARGET = $(BIN_DIR)/spmc_queue

.PHONY: all clean dirs run-2 run-4 run-8 debug release check-deps help

all: dirs $(TARGET)

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# Build target executable
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(OBJECTS) $(LIBS) $(LDFLAGS) -o $@
	@echo "Build complete: $@"

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean build files
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Clean complete"

# Run with different process counts (with proper library path)
run-2: $(TARGET)
	@echo "Running with 2 processes (1 producer, 1 consumer)..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun --allow-run-as-root -np 2 $(TARGET)

run-4: $(TARGET)
	@echo "Running with 4 processes (1 producer, 3 consumers)..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun --allow-run-as-root -np 4 $(TARGET)

run-8: $(TARGET)
	@echo "Running with 8 processes (1 producer, 7 consumers)..."
	LD_LIBRARY_PATH=$(MPI_LIB_DIR)/lib:$$LD_LIBRARY_PATH mpirun --allow-run-as-root -np 8 $(TARGET)

# Debug version
debug: CFLAGS += -DDEBUG -g -O0
debug: $(TARGET)

# Performance version
release: CFLAGS += -O3 -DNDEBUG
release: clean $(TARGET)

# Check for dependencies
check-deps:
	@echo "Checking dependencies..."
	@which mpicc > /dev/null || (echo "Error: mpicc not found. Install MPI development package." && exit 1)
	@test -f $(MPI_LIB_INCLUDE)/mpi_lib.h || (echo "Error: mpi_lib.h not found in $(MPI_LIB_INCLUDE)" && exit 1)
	@test -f $(MPI_LIB_LIB)/libmpi_wrapper.a || (echo "Error: libmpi_wrapper.a not found in $(MPI_LIB_LIB)" && exit 1)
	@echo "All dependencies found"

# Help target
help:
	@echo "Available targets:"
	@echo "  all        - Build the program"
	@echo "  clean      - Remove build files"
	@echo "  debug      - Build with debug flags"
	@echo "  release    - Build optimized version"
	@echo "  run-2      - Run with 2 processes"
	@echo "  run-4      - Run with 4 processes" 
	@echo "  run-8      - Run with 8 processes"
	@echo "  check-deps - Check if dependencies are available"
	@echo "  help       - Show this help message"

# Directory structure:
# - include/  : Header files (*.h)
# - src/      : Source files (*.c)
# - build/    : Object files (*.o)
# - bin/      : Executable files