#ifdef LUNAR_SDL

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "lunar_sdl.h"
#include "vm.h"
#include "value.h"
#include "object.h"

static SDL_Window*      window      = NULL;
static SDL_Renderer*    renderer    = NULL;
static const Uint8* keyboard        = NULL;

// texture table — Value can't hold a raw pointer, so textures are
// referenced by integer handle (their index in this array)
#define MAX_TEXTURES 256
static SDL_Texture* textures[MAX_TEXTURES];
static i32 texture_count = 0;


static Value sdl_init_native(i32 argc, Value* args) {
    if (argc < 3) return BOOL_VAL(false);

    const char* title = AS_CSTRING(args[0]);
    i32 w = (i32)AS_NUMBER(args[1]);
    i32 h = (i32)AS_NUMBER(args[2]);

    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    keyboard = SDL_GetKeyboardState(NULL);

    // PNG + JPG support; add IMG_INIT_TIF etc. if you need those formats
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);

    return BOOL_VAL(true);
}

// sdl_load_texture(path) -> handle (number), or -1 on failure
static Value sdl_load_texture_native(i32 argc, Value* args) {
    if (argc < 1 || !IS_STRING(args[0])) return NUMBER_VAL(-1);
    if (texture_count >= MAX_TEXTURES) return NUMBER_VAL(-1);

    const char* path = AS_CSTRING(args[0]);
    SDL_Texture* tex = IMG_LoadTexture(renderer, path);

    if (tex == NULL) {
        printf("sdl_load_texture: failed to load '%s': %s\n", path, IMG_GetError());
        return NUMBER_VAL(-1);
    }

    i32 handle = texture_count;
    textures[texture_count] = tex;
    texture_count++;

    return NUMBER_VAL((double)handle);
}

// sdl_draw_texture(handle, x, y, w, h)
static Value sdl_draw_texture_native(i32 argc, Value* args) {
    if (argc < 5) return NIL_VAL;

    i32 handle = (i32)AS_NUMBER(args[0]);
    if (handle < 0 || handle >= texture_count) return NIL_VAL;

    SDL_Rect dst = {
        (int)AS_NUMBER(args[1]),
        (int)AS_NUMBER(args[2]),
        (int)AS_NUMBER(args[3]),
        (int)AS_NUMBER(args[4])
    };

    SDL_RenderCopy(renderer, textures[handle], NULL, &dst);
    return NIL_VAL;
}

// sdl_texture_width(handle) / sdl_texture_height(handle) —
// handy for sizing rects to the image's native resolution
static Value sdl_texture_width_native(i32 argc, Value* args) {
    if (argc < 1) return NUMBER_VAL(0);
    i32 handle = (i32)AS_NUMBER(args[0]);
    if (handle < 0 || handle >= texture_count) return NUMBER_VAL(0);

    int w, h;
    SDL_QueryTexture(textures[handle], NULL, NULL, &w, &h);
    return NUMBER_VAL((double)w);
}

static Value sdl_texture_height_native(i32 argc, Value* args) {
    if (argc < 1) return NUMBER_VAL(0);
    i32 handle = (i32)AS_NUMBER(args[0]);
    if (handle < 0 || handle >= texture_count) return NUMBER_VAL(0);

    int w, h;
    SDL_QueryTexture(textures[handle], NULL, NULL, &w, &h);
    return NUMBER_VAL((double)h);
}

static Value sdl_key_down_native(i32 argc, Value* args) {
    if (argc < 1 || !IS_STRING(args[0])) return BOOL_VAL(false);
    const char* key = AS_CSTRING(args[0]);
    SDL_Scancode code;

    if      (strcmp(key, "up")     == 0) code = SDL_SCANCODE_UP;
    else if (strcmp(key, "down")   == 0) code = SDL_SCANCODE_DOWN;
    else if (strcmp(key, "left")   == 0) code = SDL_SCANCODE_LEFT;
    else if (strcmp(key, "right")  == 0) code = SDL_SCANCODE_RIGHT;
    else if (strcmp(key, "space")  == 0) code = SDL_SCANCODE_SPACE;
    else if (strcmp(key, "escape") == 0) code = SDL_SCANCODE_ESCAPE;
    else if (strcmp(key, "w")      == 0) code = SDL_SCANCODE_W;
    else if (strcmp(key, "a")      == 0) code = SDL_SCANCODE_A;
    else if (strcmp(key, "s")      == 0) code = SDL_SCANCODE_S;
    else if (strcmp(key, "d")      == 0) code = SDL_SCANCODE_D;
    else return BOOL_VAL(false);

    SDL_PumpEvents();
    return BOOL_VAL(keyboard[code] != 0);
}

static Value sdl_fill_rect_native(int argc, Value* args) {
    if (argc < 8) return NIL_VAL;
    SDL_Rect rect = {
        (int)AS_NUMBER(args[0]),
        (int)AS_NUMBER(args[1]),
        (int)AS_NUMBER(args[2]),
        (int)AS_NUMBER(args[3])
    };
    SDL_SetRenderDrawColor(renderer,
        (int)AS_NUMBER(args[4]),
        (int)AS_NUMBER(args[5]),
        (int)AS_NUMBER(args[6]),
        (int)AS_NUMBER(args[7])
    );
    SDL_RenderFillRect(renderer, &rect);
    return NIL_VAL;
}

static Value sdl_quit_native(i32 argc, Value* args) {
    for (i32 i = 0; i < texture_count; i++) {
        SDL_DestroyTexture(textures[i]);
    }
    texture_count = 0;

    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return NIL_VAL;
}

static Value sdl_poll_native(i32 argc, Value* args) {
    SDL_Event e;

    if (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return OBJ_VAL(copy_str("quit", 4));
    }

    return NIL_VAL;
}

static Value sdl_clear_native(i32 argc, Value* args) {
    i32 r = (i32)AS_NUMBER(args[0]);
    i32 g = (i32)AS_NUMBER(args[1]);
    i32 b = (i32)AS_NUMBER(args[2]);
    SDL_SetRenderDrawColor(renderer, r, g, b, 0xFF);
    SDL_RenderClear(renderer);
    return NIL_VAL;
}

static Value sdl_present_native(i32 argc, Value* args) {
    SDL_RenderPresent(renderer);
    return NIL_VAL;
}

static Value sdl_delay_native(i32 argc, Value* args) {
    if (argc < 1) return NIL_VAL;
    SDL_Delay((i32)AS_NUMBER(args[0]));
    return NIL_VAL;
}

void register_sdl_natives() {
    define_native("sdl_init",           sdl_init_native);
    define_native("sdl_quit",           sdl_quit_native);
    define_native("sdl_key_down",       sdl_key_down_native);
    define_native("sdl_poll",           sdl_poll_native);
    define_native("sdl_clear",          sdl_clear_native);
    define_native("sdl_present",        sdl_present_native);
    define_native("sdl_delay",          sdl_delay_native);
    define_native("sdl_fill_rect",      sdl_fill_rect_native);
    define_native("sdl_load_texture",   sdl_load_texture_native);
    define_native("sdl_draw_texture",   sdl_draw_texture_native);
    define_native("sdl_texture_width",  sdl_texture_width_native);
    define_native("sdl_texture_height", sdl_texture_height_native);
}

#endif