# Examples Makefile
CC = mpicc
CFLAGS = -O2 -Wall -Wextra -std=c99
LIBDIR = ../lib
INCDIR = ../include
INCLUDES = -I$(INCDIR)
LDFLAGS = $(LIBDIR)/libmpi_wrapper.a

# Example programs
EXAMPLES = basic_example window_example hello_world point_to_point test_mpi_lib one_sided collective

all: $(EXAMPLES)

basic_example: basic_example.c
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) -o $@

window_example: window_example.c
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) -o $@

hello_world: hello_world.c
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) -o $@

point_to_point: point_to_point.c
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) -o $@

test_mpi_lib: test_mpi_lib.c
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) -o $@

one_sided: one_sided.c
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) -o $@

collective: collective.c
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LDFLAGS) -o $@

clean:
	rm -f $(EXAMPLES)

test: all
	@echo "Running basic example with 4 processes..."
	mpirun -np 4 ./basic_example
	@echo ""
	@echo "Running window example with 3 processes..."
	mpirun -np 3 ./window_example

distclean: clean

.PHONY: all clean distclean
