CC      ?= gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function

TARGET_BASE := lunar

# --- Platform detection -------------------------------------------------
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
    EXE_EXT  := .exe
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        PLATFORM := macos
    else
        PLATFORM := linux
    endif
    EXE_EXT :=
endif

TARGET          := $(TARGET_BASE)$(EXE_EXT)
DEBUG_TARGET    := $(TARGET_BASE)_debug$(EXE_EXT)
GCSTRESS_TARGET := $(TARGET_BASE)_gcstress$(EXE_EXT)

# --- dlopen/dlsym ---------------------------------------------------------
# Linux needs an explicit -ldl. macOS has dlopen/dlsym built into libSystem
# already. Windows uses LoadLibrary/GetProcAddress instead (no -ldl needed).
ifeq ($(PLATFORM),linux)
    DL_LIBS := -ldl
else
    DL_LIBS :=
endif

LIBS = -lm $(DL_LIBS)

# --- libffi ---------------------------------------------------------------
# pkg-config works fine on Linux, and on Windows under MSYS2/MinGW
# (pacman -S mingw-w64-x86_64-libffi mingw-w64-x86_64-pkg-config).
# On macOS, Homebrew's libffi is keg-only and NOT on the default
# pkg-config search path, so fall back to `brew --prefix` if pkg-config
# can't find it.
ifeq ($(PLATFORM),macos)
    FFI_FOUND := $(shell pkg-config --exists libffi 2>/dev/null && echo yes)
    ifeq ($(FFI_FOUND),yes)
        FFI_CFLAGS := $(shell pkg-config --cflags libffi)
        FFI_LIBS   := $(shell pkg-config --libs libffi)
    else
        FFI_PREFIX := $(shell brew --prefix libffi 2>/dev/null)
        FFI_CFLAGS := -I$(FFI_PREFIX)/include
        FFI_LIBS   := -L$(FFI_PREFIX)/lib -lffi
    endif
else
    FFI_CFLAGS := $(shell pkg-config --cflags libffi)
    FFI_LIBS   := $(shell pkg-config --libs libffi)
endif

SRC = src/chunk.c src/compiler.c src/debug.c src/main.c \
      src/memory.c src/object.c src/scanner.c src/table.c \
      src/value.c src/vm.c src/lunar_std.c src/lunar_ffi.c
SDL_SRC = $(SRC) src/lunar_sdl.c

.PHONY: all debug gc-stress sdl clean

all: $(SRC)
	$(CC) $(CFLAGS) $(FFI_CFLAGS) -O2 -o $(TARGET) $(SRC) $(LIBS) $(FFI_LIBS)

debug: $(SRC)
	$(CC) $(CFLAGS) $(FFI_CFLAGS) -O0 -g \
		-DLUNAR_DEBUG_TRACE_EXECUTION \
		-DLUNAR_DEBUG_PRINT_CODE \
		-o $(DEBUG_TARGET) $(SRC) $(LIBS) $(FFI_LIBS)

gc-stress: $(SRC)
	$(CC) $(CFLAGS) $(FFI_CFLAGS) -O0 -g \
		-DLUNAR_DEBUG_STRESS_GC \
		-DLUNAR_DEBUG_LOG_GC \
		-o $(GCSTRESS_TARGET) $(SRC) $(LIBS) $(FFI_LIBS)

sdl: $(SDL_SRC)
	$(CC) $(CFLAGS) $(FFI_CFLAGS) $(shell pkg-config --cflags sdl2) -O2 -DLUNAR_SDL \
		-o $(TARGET) $(SDL_SRC) $(shell pkg-config --libs sdl2) $(LIBS) $(FFI_LIBS)

clean:
	rm -f $(TARGET) $(DEBUG_TARGET) $(GCSTRESS_TARGET)