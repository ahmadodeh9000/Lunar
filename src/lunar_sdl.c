#ifdef LUNAR_SDL

#include <stdlib.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include "lunar_sdl.h"
#include "vm.h"
#include "value.h"
#include "object.h"

#if !SDL_VERSION_ATLEAST(2, 0, 18)
#error "Lunar's triangle drawing needs SDL 2.0.18 or newer (SDL_RenderGeometry)"
#endif

static SDL_Window*      window      = NULL;
static SDL_Renderer*    renderer    = NULL;
static const Uint8* keyboard        = NULL;

// texture table — Value can't hold a raw pointer, so textures are
// referenced by integer handle (their index in this array)
#define MAX_TEXTURES 256
static SDL_Texture* textures[MAX_TEXTURES];
static i32 texture_count = 0;

// mouse motion gathered by the latest sdl_poll
static i32 mouse_dx = 0;
static i32 mouse_dy = 0;


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

/*================================
  ==========KEYS==================
  ================================
*/
typedef struct {
    const char*  name;
    SDL_Scancode code;
} KeyEntry;

static const KeyEntry KEY_TABLE[] = {
    // arrows
    {"up", SDL_SCANCODE_UP}, {"down", SDL_SCANCODE_DOWN},
    {"left", SDL_SCANCODE_LEFT}, {"right", SDL_SCANCODE_RIGHT},

    // whitespace / editing
    {"space", SDL_SCANCODE_SPACE}, {"enter", SDL_SCANCODE_RETURN},
    {"return", SDL_SCANCODE_RETURN}, {"escape", SDL_SCANCODE_ESCAPE},
    {"tab", SDL_SCANCODE_TAB}, {"backspace", SDL_SCANCODE_BACKSPACE},
    {"delete", SDL_SCANCODE_DELETE}, {"insert", SDL_SCANCODE_INSERT},

    // navigation
    {"home", SDL_SCANCODE_HOME}, {"end", SDL_SCANCODE_END},
    {"pageup", SDL_SCANCODE_PAGEUP}, {"pagedown", SDL_SCANCODE_PAGEDOWN},

    // modifiers
    {"shift", SDL_SCANCODE_LSHIFT}, {"lshift", SDL_SCANCODE_LSHIFT},
    {"rshift", SDL_SCANCODE_RSHIFT},
    {"ctrl", SDL_SCANCODE_LCTRL}, {"lctrl", SDL_SCANCODE_LCTRL},
    {"rctrl", SDL_SCANCODE_RCTRL},
    {"alt", SDL_SCANCODE_LALT}, {"lalt", SDL_SCANCODE_LALT},
    {"ralt", SDL_SCANCODE_RALT},
    {"gui", SDL_SCANCODE_LGUI}, {"lgui", SDL_SCANCODE_LGUI},
    {"rgui", SDL_SCANCODE_RGUI},
    {"capslock", SDL_SCANCODE_CAPSLOCK},

    // punctuation
    {"minus", SDL_SCANCODE_MINUS}, {"equals", SDL_SCANCODE_EQUALS},
    {"leftbracket", SDL_SCANCODE_LEFTBRACKET},
    {"rightbracket", SDL_SCANCODE_RIGHTBRACKET},
    {"backslash", SDL_SCANCODE_BACKSLASH},
    {"semicolon", SDL_SCANCODE_SEMICOLON},
    {"apostrophe", SDL_SCANCODE_APOSTROPHE},
    {"grave", SDL_SCANCODE_GRAVE}, {"comma", SDL_SCANCODE_COMMA},
    {"period", SDL_SCANCODE_PERIOD}, {"slash", SDL_SCANCODE_SLASH},

    // keypad
    {"kp0", SDL_SCANCODE_KP_0}, {"kp1", SDL_SCANCODE_KP_1},
    {"kp2", SDL_SCANCODE_KP_2}, {"kp3", SDL_SCANCODE_KP_3},
    {"kp4", SDL_SCANCODE_KP_4}, {"kp5", SDL_SCANCODE_KP_5},
    {"kp6", SDL_SCANCODE_KP_6}, {"kp7", SDL_SCANCODE_KP_7},
    {"kp8", SDL_SCANCODE_KP_8}, {"kp9", SDL_SCANCODE_KP_9},
    {"kpenter", SDL_SCANCODE_KP_ENTER}, {"kpplus", SDL_SCANCODE_KP_PLUS},
    {"kpminus", SDL_SCANCODE_KP_MINUS},
    {"kpmultiply", SDL_SCANCODE_KP_MULTIPLY},
    {"kpdivide", SDL_SCANCODE_KP_DIVIDE},
    {"kpperiod", SDL_SCANCODE_KP_PERIOD},

    // misc
    {"printscreen", SDL_SCANCODE_PRINTSCREEN},
    {"scrolllock", SDL_SCANCODE_SCROLLLOCK},
    {"pause", SDL_SCANCODE_PAUSE}, {"numlock", SDL_SCANCODE_NUMLOCKCLEAR},
};

static bool lookup_scancode(const char* key, SDL_Scancode* out) {
    size_t len = strlen(key);

    // a-z (single letter, case-insensitive)
    if (len == 1) {
        char c = key[0];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c >= 'a' && c <= 'z') { *out = SDL_SCANCODE_A + (c - 'a'); return true; }
        if (c >= '1' && c <= '9') { *out = SDL_SCANCODE_1 + (c - '1'); return true; }
        if (c == '0')             { *out = SDL_SCANCODE_0; return true; }
    }

    // f1 - f12
    if ((key[0] == 'f' || key[0] == 'F') && len >= 2 && len <= 3) {
        int n = atoi(key + 1);
        if (n >= 1 && n <= 12) { *out = SDL_SCANCODE_F1 + (n - 1); return true; }
    }

    // everything else
    for (size_t i = 0; i < sizeof(KEY_TABLE) / sizeof(KEY_TABLE[0]); i++) {
        if (strcmp(key, KEY_TABLE[i].name) == 0) {
            *out = KEY_TABLE[i].code;
            return true;
        }
    }

    // any other SDL key name, e.g. "Left Shift"
    SDL_Scancode code = SDL_GetScancodeFromName(key);
    if (code == SDL_SCANCODE_UNKNOWN) return false;
    *out = code;
    return true;
}

static Value sdl_key_down_native(i32 argc, Value* args) {
    if (argc < 1 || !IS_STRING(args[0])) return BOOL_VAL(false);

    SDL_Scancode code;
    if (!lookup_scancode(AS_CSTRING(args[0]), &code)) return BOOL_VAL(false);

    SDL_PumpEvents();
    const Uint8* keyboard = SDL_GetKeyboardState(NULL);
    return BOOL_VAL(keyboard[code] != 0);
}

// sdl_set_relative_mouse(bool) -> true on success; hides the cursor and keeps
// reporting motion past the window edges
static Value sdl_set_relative_mouse_native(i32 argc, Value* args) {
    if (argc < 1 || !IS_BOOL(args[0])) return BOOL_VAL(false);
    return BOOL_VAL(SDL_SetRelativeMouseMode(AS_BOOL(args[0]) ? SDL_TRUE : SDL_FALSE) == 0);
}

static Value sdl_mouse_dx_native(i32 argc, Value* args) { return NUMBER_VAL((double)mouse_dx); }
static Value sdl_mouse_dy_native(i32 argc, Value* args) { return NUMBER_VAL((double)mouse_dy); }

// sdl_mouse_down(button): 1 left, 2 middle, 3 right
static Value sdl_mouse_down_native(i32 argc, Value* args) {
    if (argc < 1 || !IS_NUMBER(args[0])) return BOOL_VAL(false);
    i32 button = (i32)AS_NUMBER(args[0]);
    if (button < 1 || button > 5) return BOOL_VAL(false);
    return BOOL_VAL((SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(button)) != 0);
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

/*================================
  ==========TRIANGLES=============
  ================================
*/
static bool all_numbers(i32 argc, Value* args, i32 want) {
    if (argc < want) return false;
    for (i32 i = 0; i < want; i++) {
        if (!IS_NUMBER(args[i])) return false;
    }
    return true;
}

static Uint8 color_byte(Value v) {
    double d = AS_NUMBER(v);
    return d >= 255 ? 255 : d > 0 ? (Uint8)d : 0;
}

static SDL_Vertex solid_vertex(Value x, Value y, SDL_Color color) {
    SDL_Vertex v = {{(float)AS_NUMBER(x), (float)AS_NUMBER(y)}, color, {0, 0}};
    return v;
}

// sdl_draw_tri(x1,y1, x2,y2, x3,y3, r,g,b,a)
static Value sdl_draw_tri_native(i32 argc, Value* args) {
    if (!all_numbers(argc, args, 10) || renderer == NULL) return NIL_VAL;

    SDL_Color color = {
        color_byte(args[6]), color_byte(args[7]),
        color_byte(args[8]), color_byte(args[9])
    };
    SDL_Vertex v[3] = {
        solid_vertex(args[0], args[1], color),
        solid_vertex(args[2], args[3], color),
        solid_vertex(args[4], args[5], color),
    };
    SDL_RenderGeometry(renderer, NULL, v, 3, NULL, 0);
    return NIL_VAL;
}

static SDL_Vertex textured_vertex(Value* a, SDL_Color color) {
    SDL_Vertex v = {
        {(float)AS_NUMBER(a[0]), (float)AS_NUMBER(a[1])}, color,
        {(float)AS_NUMBER(a[2]), (float)AS_NUMBER(a[3])}
    };
    return v;
}

// sdl_draw_tri_tex(tex, x1,y1,u1,v1, x2,y2,u2,v2, x3,y3,u3,v3, shade)
// UVs run 0-1 across the texture; shade 0-255 darkens it, 255 leaves it as is
static Value sdl_draw_tri_tex_native(i32 argc, Value* args) {
    if (!all_numbers(argc, args, 14) || renderer == NULL) return NIL_VAL;

    i32 handle = (i32)AS_NUMBER(args[0]);
    if (handle < 0 || handle >= texture_count) return NIL_VAL;

    Uint8 c = color_byte(args[13]);
    SDL_Color color = {c, c, c, 255};
    SDL_Vertex v[3] = {
        textured_vertex(args + 1, color),
        textured_vertex(args + 5, color),
        textured_vertex(args + 9, color),
    };
    SDL_RenderGeometry(renderer, textures[handle], v, 3, NULL, 0);
    return NIL_VAL;
}

// reused between calls so a batched draw doesn't allocate every frame
static SDL_Vertex* batch_verts = NULL;
static i32 batch_verts_cap = 0;
static int* batch_indices = NULL;
static i32 batch_indices_cap = 0;

#define VERTEX_STRIDE 5

// sdl_draw_tris(tex, verts, vcount, indices, icount) -> triangles drawn, or nil
// on a bad argument, in which case nothing is drawn.
// tex: a texture handle, or -1 for untextured.
// verts: flat array of vcount vertices, 5 numbers each: x, y, u, v, rgba, where
//   rgba packs the color as r*16777216 + g*65536 + b*256 + a.
// indices: array of vertex numbers, 3 per triangle, of which the first icount
//   are used; or nil to draw vertices in order (icount is then ignored).
// Triangles are drawn in order, later ones on top.
static Value sdl_draw_tris_native(i32 argc, Value* args) {
    if (argc < 5 || renderer == NULL) return NIL_VAL;
    if (!IS_NUMBER(args[0]) || !IS_ARRAY(args[1]) || !IS_NUMBER(args[2]) || !IS_NUMBER(args[4])) return NIL_VAL;
    if (!IS_ARRAY(args[3]) && !IS_NIL(args[3])) return NIL_VAL;

    i32 handle = (i32)AS_NUMBER(args[0]);
    if (handle != -1 && (handle < 0 || handle >= texture_count)) return NIL_VAL;
    SDL_Texture* tex = handle == -1 ? NULL : textures[handle];

    ValueArray* src = &AS_ARRAY(args[1])->items;
    double vcount_d = AS_NUMBER(args[2]);
    if (!(vcount_d >= 0) || vcount_d * VERTEX_STRIDE > src->count) return NIL_VAL;
    i32 vcount = (i32)vcount_d;

    if (vcount > batch_verts_cap) {
        SDL_Vertex* grown = realloc(batch_verts, sizeof(SDL_Vertex) * vcount);
        if (grown == NULL) return NIL_VAL;
        batch_verts = grown;
        batch_verts_cap = vcount;
    }

    Value* in = src->values;
    for (i32 i = 0; i < vcount; i++, in += VERTEX_STRIDE) {
        if (!IS_NUMBER(in[0]) || !IS_NUMBER(in[1]) || !IS_NUMBER(in[2]) ||
            !IS_NUMBER(in[3]) || !IS_NUMBER(in[4])) return NIL_VAL;
        double packed = AS_NUMBER(in[4]);
        if (!(packed >= 0 && packed <= 4294967295.0)) return NIL_VAL;
        Uint32 rgba = (Uint32)packed;
        SDL_Vertex* out = &batch_verts[i];
        out->position.x = (float)AS_NUMBER(in[0]);
        out->position.y = (float)AS_NUMBER(in[1]);
        out->tex_coord.x = (float)AS_NUMBER(in[2]);
        out->tex_coord.y = (float)AS_NUMBER(in[3]);
        out->color.r = (Uint8)(rgba >> 24);
        out->color.g = (Uint8)(rgba >> 16);
        out->color.b = (Uint8)(rgba >> 8);
        out->color.a = (Uint8)rgba;
    }

    if (IS_NIL(args[3])) {
        if (SDL_RenderGeometry(renderer, tex, batch_verts, vcount, NULL, 0) != 0) return NIL_VAL;
        return NUMBER_VAL((double)(vcount / 3));
    }

    ValueArray* idx = &AS_ARRAY(args[3])->items;
    double icount_d = AS_NUMBER(args[4]);
    if (!(icount_d >= 0) || icount_d > idx->count) return NIL_VAL;
    i32 icount = (i32)icount_d;

    if (icount > batch_indices_cap) {
        int* grown = realloc(batch_indices, sizeof(int) * icount);
        if (grown == NULL) return NIL_VAL;
        batch_indices = grown;
        batch_indices_cap = icount;
    }
    for (i32 i = 0; i < icount; i++) {
        if (!IS_NUMBER(idx->values[i])) return NIL_VAL;
        double n = AS_NUMBER(idx->values[i]);
        if (!(n >= 0 && n < vcount)) return NIL_VAL;
        batch_indices[i] = (int)n;
    }

    if (SDL_RenderGeometry(renderer, tex, batch_verts, vcount, batch_indices, icount) != 0) return NIL_VAL;
    return NUMBER_VAL((double)(icount / 3));
}

static Value sdl_quit_native(i32 argc, Value* args) {
    for (i32 i = 0; i < texture_count; i++) {
        SDL_DestroyTexture(textures[i]);
    }
    texture_count = 0;

    free(batch_verts);
    batch_verts = NULL;
    batch_verts_cap = 0;
    free(batch_indices);
    batch_indices = NULL;
    batch_indices_cap = 0;

    IMG_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return NIL_VAL;
}

// Drains the whole queue: scripts poll once per frame, and one event per call
// lets mouse motion pile up in front of a quit.
static Value sdl_poll_native(i32 argc, Value* args) {
    SDL_Event e;
    bool quit = false;

    mouse_dx = 0;
    mouse_dy = 0;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit = true;
        if (e.type == SDL_MOUSEMOTION) {
            mouse_dx += e.motion.xrel;
            mouse_dy += e.motion.yrel;
        }
    }

    return quit ? OBJ_VAL(copy_str("quit", 4)) : NIL_VAL;
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

// sdl_time() -> wall-clock seconds, unlike clock() which counts CPU time
// and so misses time spent waiting on vsync
static Value sdl_time_native(i32 argc, Value* args) {
    static Uint64 start = 0;
    Uint64 now = SDL_GetPerformanceCounter();
    if (start == 0) start = now;
    return NUMBER_VAL((double)(now - start) / (double)SDL_GetPerformanceFrequency());
}

// sdl_set_vsync(bool) -> true on success
static Value sdl_set_vsync_native(i32 argc, Value* args) {
    if (argc < 1 || !IS_BOOL(args[0]) || renderer == NULL) return BOOL_VAL(false);
    return BOOL_VAL(SDL_RenderSetVSync(renderer, AS_BOOL(args[0]) ? 1 : 0) == 0);
}

// sdl_set_blend(bool) -> makes alpha apply to sdl_fill_rect and untextured
// triangles; off by default
static Value sdl_set_blend_native(i32 argc, Value* args) {
    if (argc < 1 || !IS_BOOL(args[0]) || renderer == NULL) return BOOL_VAL(false);
    SDL_BlendMode mode = AS_BOOL(args[0]) ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE;
    return BOOL_VAL(SDL_SetRenderDrawBlendMode(renderer, mode) == 0);
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
    define_native("sdl_draw_tri",       sdl_draw_tri_native);
    define_native("sdl_draw_tri_tex",   sdl_draw_tri_tex_native);
    define_native("sdl_draw_tris",      sdl_draw_tris_native);
    define_native("sdl_load_texture",   sdl_load_texture_native);
    define_native("sdl_draw_texture",   sdl_draw_texture_native);
    define_native("sdl_texture_width",  sdl_texture_width_native);
    define_native("sdl_texture_height", sdl_texture_height_native);
    define_native("sdl_set_relative_mouse", sdl_set_relative_mouse_native);
    define_native("sdl_mouse_dx",       sdl_mouse_dx_native);
    define_native("sdl_mouse_dy",       sdl_mouse_dy_native);
    define_native("sdl_mouse_down",     sdl_mouse_down_native);
    define_native("sdl_time",           sdl_time_native);
    define_native("sdl_set_vsync",      sdl_set_vsync_native);
    define_native("sdl_set_blend",      sdl_set_blend_native);
}

#endif