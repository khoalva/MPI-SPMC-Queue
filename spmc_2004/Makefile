CC=mpicc
CFLAGS=-O2 -Wall
MPI_LIB_DIR=../mpi_lib
MPI_LIB_INCLUDE=$(MPI_LIB_DIR)/include
MPI_LIB_LIB=$(MPI_LIB_DIR)/lib
INCLUDES=-I$(MPI_LIB_INCLUDE)
LDFLAGS=-L$(MPI_LIB_LIB) -Wl,-rpath,$(MPI_LIB_LIB) -lmpi_wrapper

# SPMC sources
SOURCES=main_spmc.c spmc_queue.c

# Object files
OBJECTS=$(SOURCES:.c=.o)

# Executable
EXECUTABLE=queue_spmc

all: check_lib $(EXECUTABLE)

# Check if MPI library exists
check_lib:
	@if [ ! -f $(MPI_LIB_LIB)/libmpi_wrapper.a ]; then \
		echo "MPI library not found. Building it..."; \
		make -C $(MPI_LIB_DIR); \
	fi

# SPMC implementation
$(EXECUTABLE): $(OBJECTS) check_lib
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

# Compilation rules
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# SPMC implementation needs include path
main_spmc.o: main_spmc.c spmc_queue.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

spmc_queue.o: spmc_queue.c spmc_queue.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build MPI library
build_lib:
	make -C $(MPI_LIB_DIR)

# Clean everything
clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

# Clean including library
distclean: clean
	make -C $(MPI_LIB_DIR) clean

# Help target
help:
	@echo "Available targets:"
	@echo "  all           - Build SPMC implementation"
	@echo "  queue_spmc    - Build SPMC implementation using new MPI library"
	@echo "  build_lib     - Build MPI library"
	@echo "  clean         - Clean build artifacts"
	@echo "  distclean     - Clean everything including library"
	@echo "  help          - Show this help"

.PHONY: all check_lib build_lib clean distclean help

	