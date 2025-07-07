# MPI Library Makefile
CC = mpicc
AR = ar
CFLAGS = -O2 -Wall -Wextra -std=c99 -fPIC
INCLUDES = -Iinclude
SRCDIR = src
INCDIR = include
BUILDDIR = build
LIBDIR = lib
EXAMPLEDIR = examples

# Library information
LIBNAME = mpi_wrapper
STATIC_LIB = lib$(LIBNAME).a
SHARED_LIB = lib$(LIBNAME).so
VERSION = 1.0.0
MAJOR_VERSION = 1

# Source files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Default target
all: directories $(LIBDIR)/$(STATIC_LIB) $(LIBDIR)/$(SHARED_LIB)

# Create necessary directories
directories:
	@mkdir -p $(BUILDDIR) $(LIBDIR)

# Static library
$(LIBDIR)/$(STATIC_LIB): $(OBJECTS)
	$(AR) rcs $@ $^
	@echo "Static library created: $@"

# Shared library
$(LIBDIR)/$(SHARED_LIB): $(OBJECTS)
	$(CC) -shared -Wl,-soname,$(SHARED_LIB).$(VERSION) -o $@ $^
	@echo "Shared library created: $@"
	@cd $(LIBDIR) && ln -sf $(SHARED_LIB) $(SHARED_LIB).$(VERSION)
	@cd $(LIBDIR) && ln -sf $(SHARED_LIB) $(SHARED_LIB).$(MAJOR_VERSION)
	@echo "Version symlinks created"

# Compile source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Build examples
examples: $(LIBDIR)/$(STATIC_LIB)
	@echo "Building examples..."
	@make -C $(EXAMPLEDIR) LIBDIR=../$(LIBDIR) INCDIR=../$(INCDIR)

# Run tests
test: examples
	@echo "Running comprehensive test suite..."
	@cd $(EXAMPLEDIR) && mpirun -np 4 ./test_mpi_lib

# Run basic test
test-basic: examples
	@echo "Running basic test..."
	@cd $(EXAMPLEDIR) && mpirun -np 2 ./basic_example

# Install library (requires sudo on most systems)
install: all
	@echo "Installing MPI library..."
	sudo cp $(LIBDIR)/$(STATIC_LIB) /usr/local/lib/
	sudo cp $(LIBDIR)/$(SHARED_LIB) /usr/local/lib/
	sudo cp $(INCDIR)/mpi_lib.h /usr/local/include/
	sudo ldconfig
	@echo "Installation complete!"

# Uninstall library
uninstall:
	@echo "Uninstalling MPI library..."
	sudo rm -f /usr/local/lib/$(STATIC_LIB)
	sudo rm -f /usr/local/lib/$(SHARED_LIB)
	sudo rm -f /usr/local/include/mpi_lib.h
	sudo ldconfig
	@echo "Uninstall complete!"

# Clean build artifacts
clean:
	rm -rf $(BUILDDIR) $(LIBDIR)
	@make -C $(EXAMPLEDIR) clean 2>/dev/null || true

# Clean everything including examples
distclean: clean
	@make -C $(EXAMPLEDIR) distclean 2>/dev/null || true

# Show help
help:
	@echo "MPI Wrapper Library Build System"
	@echo "Available targets:"
	@echo "  all        - Build static and shared libraries (default)"
	@echo "  examples   - Build example programs"
	@echo "  test       - Run comprehensive test suite (requires 4 processes)"
	@echo "  test-basic - Run basic test (requires 2 processes)"
	@echo "  install    - Install library to system (requires sudo)"
	@echo "  uninstall  - Remove library from system (requires sudo)"
	@echo "  clean      - Remove build artifacts"
	@echo "  distclean  - Remove all generated files"
	@echo "  debug      - Build with debug symbols"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Usage examples:"
	@echo "  make                    # Build libraries"
	@echo "  make examples          # Build examples"
	@echo "  make test              # Run tests"
	@echo "  make install           # Install to system"
	@echo "  make clean             # Clean build"

# Debugging target
debug: CFLAGS += -g -DDEBUG
debug: all

# Phony targets
.PHONY: all directories examples test test-basic install uninstall clean distclean help debug

# Print variables for debugging
print-vars:
	@echo "CC = $(CC)"
	@echo "CFLAGS = $(CFLAGS)"
	@echo "SOURCES = $(SOURCES)"
	@echo "OBJECTS = $(OBJECTS)"
	@echo "STATIC_LIB = $(LIBDIR)/$(STATIC_LIB)"
	@echo "SHARED_LIB = $(LIBDIR)/$(SHARED_LIB)"
