CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
LIBS    = -lm
SRC     = $(wildcard src/*.c)
TARGET  = lunar

# Release build (default)
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -O2 -o $@ $^ $(LIBS)

# Debug build — enables bytecode tracing + GC logging
debug: $(SRC)
	$(CC) $(CFLAGS) -O0 -g \
	    -DLUNAR_DEBUG_TRACE_EXECUTION \
	    -DLUNAR_DEBUG_PRINT_CODE \
	    -o $(TARGET)_debug $^ $(LIBS)

# Stress-test GC (collect on every allocation)
gc-stress: $(SRC)
	$(CC) $(CFLAGS) -O0 -g \
	    -DLUNAR_DEBUG_STRESS_GC \
	    -DLUNAR_DEBUG_LOG_GC \
	    -o $(TARGET)_gcstress $^ $(LIBS)

clean:
	rm -f $(TARGET) $(TARGET)_debug $(TARGET)_gcstress

.PHONY: all debug gc-stress clean
