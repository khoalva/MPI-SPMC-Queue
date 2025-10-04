# Makefile for Benchmark Library
# High-performance benchmarking for SPMC Queue

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
LIB_NAME = benchmark
STATIC_LIB = lib$(LIB_NAME).a
SHARED_LIB = lib$(LIB_NAME).so
SHARED_LIB_VERSION = lib$(LIB_NAME).so.1.0.0
SHARED_LIB_SONAME = lib$(LIB_NAME).so.1

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
HEADERS = $(wildcard $(INCDIR)/*.h)

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
	$(CC) -shared -Wl,-soname,$(SHARED_LIB_SONAME) -o $(LIBDIR)/$(SHARED_LIB_VERSION) $(OBJECTS) $(LDFLAGS)
	cd $(LIBDIR) && ln -sf $(SHARED_LIB_VERSION) $(SHARED_LIB_SONAME)
	cd $(LIBDIR) && ln -sf $(SHARED_LIB_SONAME) $(SHARED_LIB)
	@echo "Created shared library: $(LIBDIR)/$(SHARED_LIB)"

# Compile object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

# Examples
examples: spmc_benchmark

spmc_benchmark: $(EXAMPLEDIR)/spmc_benchmark.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -I$(INCDIR) -I../spmc_2017/src -I../mpi_lib/include \
		$(EXAMPLEDIR)/spmc_benchmark.c ../spmc_2017/build/spmc_queue.o \
		$(LIBDIR)/$(STATIC_LIB) ../mpi_lib/lib/libmpi_wrapper.a \
		-lm -o $(EXAMPLEDIR)/spmc_benchmark

# Test targets
test: test-quick test-throughput test-latency

test-quick: spmc_benchmark
	@echo "Running quick benchmark test..."
	cd $(EXAMPLEDIR) && mpirun -np 3 ./spmc_benchmark quick

test-throughput: spmc_benchmark
	@echo "Running throughput benchmark test..."
	cd $(EXAMPLEDIR) && mpirun -np 4 ./spmc_benchmark throughput

test-latency: spmc_benchmark
	@echo "Running latency benchmark test..."
	cd $(EXAMPLEDIR) && mpirun -np 3 ./spmc_benchmark latency

test-all: spmc_benchmark
	@echo "Running comprehensive benchmark suite..."
	cd $(EXAMPLEDIR) && mpirun -np 3 ./spmc_benchmark quick
	cd $(EXAMPLEDIR) && mpirun -np 4 ./spmc_benchmark throughput
	cd $(EXAMPLEDIR) && mpirun -np 3 ./spmc_benchmark latency
	cd $(EXAMPLEDIR) && mpirun -np 6 ./spmc_benchmark scalability

# Clean targets
clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(EXAMPLEDIR)/spmc_benchmark
	rm -f *.csv

clean-all: clean
	rm -rf $(OBJDIR) $(LIBDIR)
	rm -f $(EXAMPLEDIR)/*

# Install target
install: all
	@echo "Installing benchmark library..."
	sudo cp $(LIBDIR)/$(STATIC_LIB) /usr/local/lib/
	sudo cp $(LIBDIR)/$(SHARED_LIB)* /usr/local/lib/
	sudo cp $(INCDIR)/benchmark.h /usr/local/include/
	sudo ldconfig

# Uninstall target
uninstall:
	@echo "Uninstalling benchmark library..."
	sudo rm -f /usr/local/lib/$(STATIC_LIB)
	sudo rm -f /usr/local/lib/$(SHARED_LIB)*
	sudo rm -f /usr/local/include/benchmark.h
	sudo ldconfig

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build both static and shared libraries and examples"
	@echo "  static       - Build static library only"
	@echo "  shared       - Build shared library only"
	@echo "  examples     - Build example programs"
	@echo "  test         - Run basic benchmark tests"
	@echo "  test-quick   - Run quick benchmark test"
	@echo "  test-throughput - Run throughput benchmark test"
	@echo "  test-latency - Run latency benchmark test"
	@echo "  test-all     - Run comprehensive benchmark suite"
	@echo "  clean        - Remove object files and executables"
	@echo "  clean-all    - Remove all generated files and directories"
	@echo "  install      - Install library system-wide"
	@echo "  uninstall    - Remove library from system"
	@echo "  help         - Show this help message"

# Debug target
debug: CFLAGS += -g -DDEBUG -O0
debug: all

# Optimized target
optimize: CFLAGS += -O3 -march=native -DNDEBUG
optimize: all

.PHONY: all directories examples test test-quick test-throughput test-latency test-all clean clean-all install uninstall help debug optimize
