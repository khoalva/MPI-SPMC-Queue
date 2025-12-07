# Makefile for Micro Benchmark Library
# Proper micro benchmarking for SPMC Queue with fixed operations

CC = mpicc
CFLAGS = -O2 -Wall -Wextra -std=c99 -fPIC
LDFLAGS = -lm

# Directories
SRCDIR = .
INCDIR = .
LIBDIR = lib
OBJDIR = obj
EXAMPLEDIR = examples

# Library configuration
LIB_NAME = microbenchmark
STATIC_LIB = lib$(LIB_NAME).a
SHARED_LIB = lib$(LIB_NAME).so

# Source files
SOURCES = micro_benchmark.c
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)
HEADERS = micro_benchmark.h

# Default target
all: directories $(STATIC_LIB) $(SHARED_LIB) examples

# Create necessary directories
directories:
	@mkdir -p $(OBJDIR) $(LIBDIR) $(EXAMPLEDIR)

# Static library
$(STATIC_LIB): $(OBJECTS)
	@mkdir -p $(LIBDIR)
	ar rcs $(LIBDIR)/$(STATIC_LIB) $(OBJECTS)
	@echo "Created static library: $(LIBDIR)/$(STATIC_LIB)"

# Shared library
$(SHARED_LIB): $(OBJECTS)
	@mkdir -p $(LIBDIR)
	$(CC) -shared -o $(LIBDIR)/$(SHARED_LIB) $(OBJECTS) $(LDFLAGS)
	@echo "Created shared library: $(LIBDIR)/$(SHARED_LIB)"

# Compile object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

# Examples
examples: spmc_microbenchmark

spmc_microbenchmark: $(EXAMPLEDIR)/spmc_microbenchmark.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -I$(INCDIR) -I../spmc_BBQ/src -I../mpi_lib/include \
		$(EXAMPLEDIR)/spmc_microbenchmark.c ../spmc_BBQ/build/spmc_queue.o \
		../spmc_BBQ/build/bitmap.o \
		$(LIBDIR)/$(STATIC_LIB) ../mpi_lib/lib/libmpi_wrapper.a \
		-lm -o $(EXAMPLEDIR)/spmc_microbenchmark
	@echo "Built micro benchmark example: $(EXAMPLEDIR)/spmc_microbenchmark"

# Test targets
test: test-micro

test-micro: spmc_microbenchmark
	@echo "Running micro benchmark test (5 procs, 10000 ops)..."
	cd $(EXAMPLEDIR) && mpirun -np 5 ./spmc_microbenchmark 10000

test-micro-small: spmc_microbenchmark
	@echo "Running small micro benchmark (3 procs, 5000 ops)..."
	cd $(EXAMPLEDIR) && mpirun -np 3 ./spmc_microbenchmark 5000

test-micro-large: spmc_microbenchmark
	@echo "Running large micro benchmark (9 procs, 20000 ops)..."
	cd $(EXAMPLEDIR) && mpirun -np 9 ./spmc_microbenchmark 20000

# Clean targets
clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(EXAMPLEDIR)/spmc_microbenchmark
	rm -f *.csv
	rm -f $(EXAMPLEDIR)/*.csv

clean-all: clean
	rm -rf $(OBJDIR) $(LIBDIR)
	rm -f $(EXAMPLEDIR)/spmc_microbenchmark

# Install target
install: all
	@echo "Installing micro benchmark library..."
	sudo cp $(LIBDIR)/$(STATIC_LIB) /usr/local/lib/
	sudo cp $(LIBDIR)/$(SHARED_LIB) /usr/local/lib/
	sudo cp $(INCDIR)/micro_benchmark.h /usr/local/include/
	sudo ldconfig

# Uninstall target
uninstall:
	@echo "Uninstalling micro benchmark library..."
	sudo rm -f /usr/local/lib/$(STATIC_LIB)
	sudo rm -f /usr/local/lib/$(SHARED_LIB)
	sudo rm -f /usr/local/include/micro_benchmark.h
	sudo ldconfig

# Help target
help:
	@echo "Available targets:"
	@echo "  all              - Build both static and shared libraries and examples"
	@echo "  static           - Build static library only"
	@echo "  shared           - Build shared library only"
	@echo "  examples         - Build example programs"
	@echo "  test             - Run basic micro benchmark test"
	@echo "  test-micro       - Run micro benchmark (5 procs, 10000 ops)"
	@echo "  test-micro-small - Run small micro benchmark (3 procs, 5000 ops)"
	@echo "  test-micro-large - Run large micro benchmark (9 procs, 20000 ops)"
	@echo "  clean            - Remove object files and executables"
	@echo "  clean-all        - Remove all generated files and directories"
	@echo "  install          - Install library system-wide"
	@echo "  uninstall        - Remove library from system"
	@echo "  help             - Show this help message"

# Debug target
debug: CFLAGS += -g -DDEBUG -O0
debug: all

# Optimized target
optimize: CFLAGS += -O3 -march=native -DNDEBUG
optimize: all

.PHONY: all directories examples test test-micro test-micro-small test-micro-large clean clean-all install uninstall help debug optimize
