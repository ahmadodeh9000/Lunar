CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
LIBS   = -lm

SRC = src/chunk.c src/compiler.c src/debug.c src/main.c \
      src/memory.c src/object.c src/scanner.c src/table.c \
      src/value.c src/vm.c

SDL_SRC = $(SRC) src/lunar_sdl.c

TARGET = lunar

all:
	$(CC) $(CFLAGS) -O2 -o $(TARGET) $(SRC) $(LIBS)

debug:
	$(CC) $(CFLAGS) -O0 -g \
		-DLUNAR_DEBUG_TRACE_EXECUTION \
		-DLUNAR_DEBUG_PRINT_CODE \
		-o $(TARGET)_debug $(SRC) $(LIBS)

gc-stress:
	$(CC) $(CFLAGS) -O0 -g \
		-DLUNAR_DEBUG_STRESS_GC \
		-DLUNAR_DEBUG_LOG_GC \
		-o $(TARGET)_gcstress $(SRC) $(LIBS)

sdl:
	$(CC) $(CFLAGS) -O2 -DLUNAR_SDL \
		$(shell pkg-config --cflags sdl2) \
		-o $(TARGET) $(SDL_SRC) \
		$(shell pkg-config --libs sdl2) $(LIBS)

clean:
	rm -f $(TARGET) $(TARGET)_debug $(TARGET)_gcstress

.PHONY: all debug gc-stress sdl clean