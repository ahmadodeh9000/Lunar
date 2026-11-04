#define _DEFAULT_SOURCE     // realpath, strdup, getcwd under -std=c11 with glibc
#include "vm.h"
#include "debug.h"
#include "compiler.h"
#include "object.h"
#include "memory.h"
#include "table.h"
#include "lunar_ffi.h"
#include "map.h"

#ifdef LUNAR_SDL
#include "lunar_sdl.h"
#endif

#include "lunar_std.h"
#include "lunar_modules.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

LunarVM lvm;


void runtime_error(const char* fmt, ...);

static InterpretResult run();
static void            reset_stack();
static bool            call(ObjClosure* closure, int argc);
static bool            call_value(Value callee, int argc);
static ObjUpvalue*     capture_upvalue(Value* local);
static void            close_upvalues(Value* last);
static void            define_method(ObjString* name);
static bool            bind_method(ObjStruct* klass, ObjString* name);
static bool            invoke(ObjString* name, int argc);
static bool            invoke_from_class(ObjStruct* klass, ObjString* name, int argc);


void define_native(const char* name, NativeFn fn) {
    push(OBJ_VAL(copy_str(name,(int)strlen(name))));
    push(OBJ_VAL(new_native(fn,name)));
    table_set(&lvm.builtins, AS_STRING(lvm.stack_top[-2]), lvm.stack_top[-1]);
    pop(); pop();
}

void init_lunar_vm() {
    reset_stack();
    lvm.objects=NULL; lvm.bytes_allocated=0; lvm.next_gc=1024*1024;
    lvm.gray_count=0; lvm.gray_capacity=0; lvm.gray_stack=NULL;
    lvm.open_upvalues=NULL;
    init_table(&lvm.builtins);
    init_table(&lvm.modules);
    init_table(&lvm.builtin_modules);
    init_table(&lvm.strings);
    lvm.main_module=NULL; lvm.main_dir=NULL;
    lvm.init_string=NULL;
    lvm.init_string=copy_str("init",4);

    register_std_natives();

    ObjNative* ffiLoadNative = new_native(ffi_load_library, "clib");
    push(OBJ_VAL(ffiLoadNative)); // Push to stack to protect from a sudden GC sweep
    push(OBJ_VAL(copy_str("clib", 4)));   // root the name too: table_set can trigger GC
    table_set(&lvm.builtins, AS_STRING(lvm.stack_top[-1]), lvm.stack_top[-2]);
    pop(); pop();

    ObjNative* ffiBindNative = new_native(ffi_bind_function, "cbind");
    push(OBJ_VAL(ffiBindNative));
    push(OBJ_VAL(copy_str("cbind", 5)));
    table_set(&lvm.builtins, AS_STRING(lvm.stack_top[-1]), lvm.stack_top[-2]);
    pop(); pop();

#ifdef LUNAR_SDL
    register_sdl_natives();
#endif
}

void free_lunar_vm() {
    free_table(&lvm.builtins);
    free_table(&lvm.modules);
    free_table(&lvm.builtin_modules);
    free_table(&lvm.strings);
    free(lvm.main_dir); lvm.main_dir=NULL; lvm.main_module=NULL;
    lvm.init_string=NULL;
    freeObjects();
}

static void reset_stack() {
    lvm.stack_top=lvm.stack; lvm.frame_count=0; lvm.open_upvalues=NULL;
    lvm.handler_count=0; lvm.error_pending=false; lvm.error_value=NIL_VAL;
    lvm.loading_count=0;
}

void push(Value val) {
    if(lvm.stack_top>=lvm.stack+STACK_MAX){fprintf(stderr,"Stack overflow.\n");exit(1);}
    *lvm.stack_top++=val;
}
Value pop()                 { return *--lvm.stack_top; }
static Value peek(int d)    { return lvm.stack_top[-1-d]; }

void runtime_error(const char* fmt, ...) {
    va_list args, copy; va_start(args,fmt); va_copy(copy,args);
    int len=vsnprintf(NULL,0,fmt,copy); va_end(copy);
    char* buf=(char*)malloc((size_t)len+1);
    if(buf==NULL){ fprintf(stderr,"OOM error message\n"); exit(1); }
    vsnprintf(buf,(size_t)len+1,fmt,args); va_end(args);
    lvm.error_value=OBJ_VAL(copy_str(buf,len));     // can GC; error_value is a root
    free(buf);
    lvm.error_pending=true;
}

static int frame_line(CallFrame* f) {
    ObjFunction* fn=f->closure->function;
    return fn->chunk.lines[f->ip-fn->chunk.code-1];
}

static void print_frame(CallFrame* f) {
    ObjFunction* fn=f->closure->function;
    bool imported=fn->module!=lvm.main_module;      // the main script's lines look as they always did
    if(imported) fprintf(stderr,"[%s line %d] in ",fn->module->name->chars,frame_line(f));
    else         fprintf(stderr,"[line %d] in ",frame_line(f));
    if(fn->name==NULL) fprintf(stderr,imported?"module\n":"script\n");
    else               fprintf(stderr,"%s()\n",fn->name->chars);
}

// Prints an error nobody caught: the value, then the stack trace from where it was
// thrown. A run of identical frames (deep recursion) shows 3 of them and a count.
static void report_uncaught(Value err) {
    if(IS_STRING(err)){ fwrite(AS_CSTRING(err),1,AS_STRING(err)->length,stderr); }
    else { int n; char* text=value_text(err,&n); fwrite(text,1,n,stderr); free(text); }
    fputs("\n",stderr);
    for(int i=lvm.frame_count-1;i>=0;){
        CallFrame* f=&lvm.frames[i];
        int same=1;
        while(i-same>=0 && lvm.frames[i-same].closure->function==f->closure->function &&
              frame_line(&lvm.frames[i-same])==frame_line(f)) same++;
        for(int k=0;k<same && k<3;k++) print_frame(f);
        if(same>3) fprintf(stderr,"... repeated %d more time%s\n",same-3,same==4?"":"s");
        i-=same;
    }
}

// Modules whose top-level frame is gone (an error unwound past it) never finished
// loading: they leave the cache, so importing them again runs them again.
static void abandon_loading(int frames_left) {
    while(lvm.loading_count>0 && lvm.loading[lvm.loading_count-1].frame>=frames_left){
        ObjModule* m=lvm.loading[--lvm.loading_count].module;
        table_delete(&lvm.modules,m->file);
    }
}

// Sends the error in error_value to the newest try handler: back to its frame and
// stack height, with the error pushed for the catch variable. With no handler it's
// reported and the VM reset; returns false then.
static bool catch_error() {
    Value err=lvm.error_value;
    if(lvm.handler_count==0){ report_uncaught(err); abandon_loading(0); reset_stack(); return false; }
    Handler* h=&lvm.handlers[--lvm.handler_count];
    abandon_loading(h->frame_count);
    lvm.frame_count=h->frame_count;
    close_upvalues(h->stack_top);
    lvm.stack_top=h->stack_top;
    push(err);
    lvm.error_value=NIL_VAL; lvm.error_pending=false;
    lvm.frames[lvm.frame_count-1].ip=h->catch_ip;
    return true;
}

static bool is_falsey(Value v){ return IS_NIL(v)||(IS_BOOL(v)&&!AS_BOOL(v)); }

static void concatenate() {
    ObjString* b=AS_STRING(peek(0)); ObjString* a=AS_STRING(peek(1));
    int len=a->length+b->length; char* buf=ALLOCATE(char,len+1);
    memcpy(buf,a->chars,a->length); memcpy(buf+a->length,b->chars,b->length);
    buf[len]='\0';
    ObjString* res=take_str(buf,len); pop(); pop(); push(OBJ_VAL(res));
}

static bool call(ObjClosure* closure, int argc) {
    if(argc!=closure->function->arity){
        runtime_error("Expected %d arguments but got %d.",closure->function->arity,argc);
        return false;
    }
    if(lvm.frame_count==FRAMES_MAX){ runtime_error("Stack overflow."); return false; }
    CallFrame* frame=&lvm.frames[lvm.frame_count++];
    frame->closure=closure; frame->ip=closure->function->chunk.code;
    frame->slots=lvm.stack_top-argc-1;
    return true;
}

static bool call_value(Value callee, int argc) {
    if(!IS_OBJ(callee)){ runtime_error("Can only call functions and structs."); return false; }
    switch(OBJ_TYPE(callee)){
        case OBJ_CLOSURE: return call(AS_CLOSURE(callee),argc);
        case OBJ_NATIVE: {
            NativeFn fn=AS_NATIVE(callee);
            Value res=fn(argc,lvm.stack_top-argc);
            if(lvm.error_pending) return false;     // the native raised runtime_error
            lvm.stack_top-=argc+1; push(res); return true;
        }
        case OBJ_STRUCT: {
            ObjStruct* klass=AS_STRUCT(callee);
            ObjInstance* inst=new_instance(klass);
            lvm.stack_top[-argc-1]=OBJ_VAL(inst);
            Value init; if(table_get(klass->methods,lvm.init_string,&init)) return call(AS_CLOSURE(init),argc);
            if(argc!=0){ runtime_error("Expected 0 arguments but got %d.",argc); return false; }
            return true;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod* bm=AS_BOUND_METHOD(callee);
            lvm.stack_top[-argc-1]=bm->receiver;
            return call(bm->method,argc);
        }

        case OBJ_FFI_FUNC: {
            ObjFFIFunc* func = AS_FFI_FUNC(callee);
            
            // The arguments are sitting on the stack right before the top
            Value* args = lvm.stack_top - argc;
            
            // Invoke the FFI engine directly!
            Value result = ffi_call_bound(func, argc,args);
            if (lvm.error_pending) return false;      // it raised runtime_error
            
            // Wind down the stack frame: pop args and the function object itself
            lvm.stack_top -= argc + 1;
            
            // Push the results right back up
            push(result);
            return true;
        }

        default: runtime_error("Can only call functions and structs."); return false;
    }
}

static ObjUpvalue* capture_upvalue(Value* local) {
    ObjUpvalue* prev=NULL; ObjUpvalue* uv=lvm.open_upvalues;
    while(uv!=NULL&&uv->location>local){ prev=uv; uv=uv->next; }
    if(uv!=NULL&&uv->location==local) return uv;
    ObjUpvalue* created=new_upvalue(local);
    created->next=uv;
    if(prev==NULL) lvm.open_upvalues=created; else prev->next=created;
    return created;
}

static void close_upvalues(Value* last) {
    while(lvm.open_upvalues!=NULL&&lvm.open_upvalues->location>=last){
        ObjUpvalue* uv=lvm.open_upvalues;
        uv->closed=*uv->location; uv->location=&uv->closed;
        lvm.open_upvalues=uv->next;
    }
}

static void define_method(ObjString* name) {
    Value m=peek(0); ObjStruct* k=AS_STRUCT(peek(1));
    table_set(k->methods,name,m); pop();
}

static bool bind_method(ObjStruct* klass, ObjString* name) {
    Value method;
    if(!table_get(klass->methods,name,&method)){ runtime_error("Undefined property '%s'.",name->chars); return false; }
    ObjBoundMethod* bm=new_bound_method(peek(0),AS_CLOSURE(method));
    pop(); push(OBJ_VAL(bm)); return true;
}

// adds n key/value pairs (key, value, key, value, ...) that stay on the stack meanwhile
static bool add_map_pairs(ObjMap* map, Value* pairs, int n) {
    for (int i = 0; i < n; i++) {
        const char* err = map_key_error(pairs[2*i]);
        if (err != NULL) { runtime_error("%s", err); return false; }
        map_set(map, pairs[2*i], pairs[2*i+1]);
    }
    return true;
}

// bytes in the UTF-8 character at s; a malformed one counts as a single byte
int utf8_char_length(const char* s, int remaining) {
    u8 c = (u8)s[0];
    int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (n > remaining) return 1;
    for (int i = 1; i < n; i++) {
        if (((u8)s[i] & 0xC0) != 0x80) return 1;
    }
    return n;
}

// joins n values into one string, each as the text print shows (OP_BUILD_STRING);
// the values stay on the stack meanwhile, since allocating can GC
static ObjString* join_parts(Value* parts, int n) {
    char* texts[UINT8_MAX];
    int   lens[UINT8_MAX];
    int   total = 0;
    for (int i = 0; i < n; i++) {
        if (IS_STRING(parts[i])) { texts[i] = NULL; lens[i] = AS_STRING(parts[i])->length; }
        else texts[i] = value_text(parts[i], &lens[i]);
        total += lens[i];
    }
    char* buf = ALLOCATE(char, total + 1);
    int at = 0;
    for (int i = 0; i < n; i++) {
        memcpy(buf + at, texts[i] != NULL ? texts[i] : AS_CSTRING(parts[i]), lens[i]);
        at += lens[i];
        free(texts[i]);
    }
    buf[total] = '\0';
    return take_str(buf, total);
}

static bool invoke_from_class(ObjStruct* klass, ObjString* name, int argc) {
    Value method;
    if(!table_get(klass->methods,name,&method)){ runtime_error("Undefined property '%s'.",name->chars); return false; }
    return call(AS_CLOSURE(method),argc);
}

static bool invoke(ObjString* name, int argc) {
    Value receiver=peek(argc);
    if(!IS_INSTANCE(receiver)){
        if(IS_MAP(receiver)){
            // maps have no methods: m.f(x) calls the function stored at "f", with no self
            Value val; if(!map_get(AS_MAP(receiver),OBJ_VAL(name),&val)) val=NIL_VAL;
            lvm.stack_top[-argc-1]=val; return call_value(val,argc);
        }
        runtime_error("Only instances and maps have methods."); return false;
    }
    ObjInstance* inst=AS_INSTANCE(receiver);
    Value val;
    if(table_get(inst->fields,name,&val)){ lvm.stack_top[-argc-1]=val; return call_value(val,argc); }
    return invoke_from_class(inst->klass,name,argc);
}

// Runs until the script ends. run() leaves on every runtime error and throw, with the
// error pending; if a try handler takes it, run() starts again inside the catch.
// (Keeping the error exits as plain returns keeps the dispatch loop as fast as before.)
static InterpretResult run_catching() {
    for(;;){
        InterpretResult result=run();
        if(result!=INTERPRET_RUNTIME_ERR || !lvm.error_pending) return result;   // done, or an internal bug
        if(!catch_error()) return INTERPRET_RUNTIME_ERR;
    }
}

/* ── modules ── */

// The name shown for a module: its path relative to the main script's directory,
// or the full path when it's somewhere else.
static ObjString* module_name(const char* canonical) {
    size_t dir=strlen(lvm.main_dir);
    if(strncmp(canonical,lvm.main_dir,dir)==0 && canonical[dir]=='/') canonical+=dir+1;
    return copy_str(canonical,(int)strlen(canonical));
}

// a new module whose globals start as the built-ins; it's left pushed on the stack
static ObjModule* push_new_module(ObjString* file, ObjString* name) {
    ObjModule* module=new_module(file,name);
    push(OBJ_VAL(module));
    table_add_all(&lvm.builtins,&module->globals);     // can GC; the module is on the stack
    return module;
}

// reads a whole file into a malloc'd, NUL-terminated buffer, or NULL
static char* read_source(const char* path) {
    struct stat st;
    if(stat(path,&st)!=0 || !S_ISREG(st.st_mode)) return NULL;
    FILE* f=fopen(path,"rb");
    if(f==NULL) return NULL;
    char* buf=(char*)malloc((size_t)st.st_size+1);
    if(buf==NULL){ fclose(f); return NULL; }
    size_t n=fread(buf,1,(size_t)st.st_size,f);
    buf[n]='\0';
    fclose(f);
    return buf;
}

// The "Circular import: a -> b -> a." message: the modules still loading, from the
// one being imported again to the newest, then that one again. The main script is
// always loading and isn't on the loading stack: it heads the chain.
static void circular_import_error(ObjModule* again) {
    int from=0;
    while(from<lvm.loading_count && lvm.loading[from].module!=again) from++;
    bool main_first=from==lvm.loading_count;
    if(main_first) from=0;
    size_t len=32+lvm.main_module->name->length;
    for(int i=from;i<lvm.loading_count;i++) len+=lvm.loading[i].module->name->length+4;
    len+=again->name->length;
    char* msg=(char*)malloc(len);
    if(msg==NULL){ fprintf(stderr,"OOM import\n"); exit(1); }
    strcpy(msg,"Circular import: ");
    if(main_first){ strcat(msg,lvm.main_module->name->chars); strcat(msg," -> "); }
    for(int i=from;i<lvm.loading_count;i++){ strcat(msg,lvm.loading[i].module->name->chars); strcat(msg," -> "); }
    strcat(msg,again->name->chars);
    runtime_error("%s.",msg);
    free(msg);
}

// import(path): the path is on top of the stack. A module already loaded gives its
// exports at once; otherwise its file is compiled and its top-level code is called in
// a new frame, whose OP_MODULE_RET caches the exports and hands them back.
static bool import_module(ObjModule* importer) {
    if(!IS_STRING(peek(0))){
        runtime_error("import() expects a string path (got %s).",value_type_name(peek(0)));
        return false;
    }
    ObjString* path=AS_STRING(peek(0));
    size_t plen=(size_t)path->length;
    bool is_file=strchr(path->chars,'/')!=NULL || (plen>=6 && strcmp(path->chars+plen-6,".lunar")==0);
    if(!is_file) return import_builtin(path);

    // relative to the importing file's directory (the current one for the REPL)
    char cwd[PATH_MAX];
    const char* dir;
    size_t dlen;
    if(importer->file!=NULL){
        dir=importer->file->chars;
        dlen=(size_t)(strrchr(dir,'/')-dir);
    } else {
        if(getcwd(cwd,sizeof cwd)==NULL) strcpy(cwd,".");
        dir=cwd; dlen=strlen(cwd);
    }
    char* joined=(char*)malloc(dlen+plen+2);
    if(joined==NULL){ fprintf(stderr,"OOM import\n"); exit(1); }
    if(path->chars[0]=='/') strcpy(joined,path->chars);
    else { memcpy(joined,dir,dlen); joined[dlen]='/'; strcpy(joined+dlen+1,path->chars); }

    char canonical[PATH_MAX];
    if(realpath(joined,canonical)==NULL){
        runtime_error("Can't find module '%s' (looked for %s).",path->chars,joined);
        free(joined);
        return false;
    }
    free(joined);

    Value* path_slot=lvm.stack_top-1;
    ObjString* file=copy_str(canonical,(int)strlen(canonical));
    push(OBJ_VAL(file));
    Value cached;
    if(table_get(&lvm.modules,file,&cached)){
        ObjModule* m=AS_MODULE(cached);
        if(!m->loaded){ circular_import_error(m); return false; }
        lvm.stack_top=path_slot;
        push(m->exports);
        return true;
    }

    char* src=read_source(canonical);
    if(src==NULL){ runtime_error("Can't read module '%s'.",path->chars); return false; }

    ObjString* name=module_name(canonical);
    push(OBJ_VAL(name));
    ObjModule* module=push_new_module(file,name);
    table_set(&lvm.modules,file,OBJ_VAL(module));       // marks it as loading, for cycles
    ObjFunction* fn=compile(src,module);
    free(src);
    if(fn==NULL){
        table_delete(&lvm.modules,file);
        runtime_error("Can't compile module '%s'.",name->chars);
        return false;
    }
    push(OBJ_VAL(fn));
    ObjClosure* closure=new_closure(fn);
    lvm.stack_top=path_slot;                            // the frame's slot 0 replaces the path
    push(OBJ_VAL(closure));
    if(!call(closure,0)){ table_delete(&lvm.modules,file); return false; }
    lvm.loading[lvm.loading_count].module=module;
    lvm.loading[lvm.loading_count].frame=lvm.frame_count-1;
    lvm.loading_count++;
    return true;
}

// the module for the main script (or the REPL), made on the first interpret()
static void make_main_module(const char* path) {
    char canonical[PATH_MAX];
    ObjString* file=NULL;
    if(path!=NULL && realpath(path,canonical)!=NULL){
        lvm.main_dir=strdup(canonical);
        *strrchr(lvm.main_dir,'/')='\0';
        file=copy_str(canonical,(int)strlen(canonical));
    } else {
        if(getcwd(canonical,sizeof canonical)==NULL) strcpy(canonical,".");
        lvm.main_dir=strdup(canonical);
    }
    push(file!=NULL?OBJ_VAL(file):NIL_VAL);
    ObjString* name=file!=NULL?module_name(file->chars):copy_str("<repl>",6);
    push(OBJ_VAL(name));
    ObjModule* module=push_new_module(file,name);
    lvm.main_module=module;         // never marked loaded: importing the main script is a cycle
    if(file!=NULL) table_set(&lvm.modules,file,OBJ_VAL(module));
    lvm.stack_top-=3;
}

InterpretResult interpret(const char* src, const char* path) {
    if(lvm.main_module==NULL) make_main_module(path);
    ObjFunction* fn=compile(src,lvm.main_module);
    if(fn==NULL) return INTERPRET_COMPILE_ERR;
    push(OBJ_VAL(fn));
    ObjClosure* closure=new_closure(fn);
    pop(); push(OBJ_VAL(closure));
    call(closure,0);
    return run_catching();
}

// How run() goes from one instruction to the next. With GCC and clang each handler
// jumps straight to the next one's label through dispatch_table ("computed goto"),
// which the CPU predicts better than one shared switch. Other compilers, trace
// builds, and -DLUNAR_NO_COMPUTED_GOTO use the switch. In the goto version run()
// isn't a loop, so a handler that ends in break instead of NEXT doesn't compile.
#if defined(__GNUC__) && !defined(LUNAR_NO_COMPUTED_GOTO) && !defined(LUNAR_DEBUG_TRACE_EXECUTION)
#define LUNAR_COMPUTED_GOTO
#define DISPATCH_LOOP
#define DISPATCH_SWITCH     NEXT;
#define CASE(op)            L_##op
#define CASE_DEFAULT        L_unknown
#define NEXT                goto *dispatch_table[READ_BYTE()]
#else
#define DISPATCH_LOOP       for(;;)
#define DISPATCH_SWITCH     switch(READ_BYTE())
#define CASE(op)            case op
#define CASE_DEFAULT        default
#define NEXT                break
#endif

static InterpretResult run() {
    CallFrame* frame;
    Table* globals;     // the current function's module globals

// after any change of frame: the newest frame, and its module's globals
#define LOAD_FRAME() (frame=&lvm.frames[lvm.frame_count-1], \
                      globals=&frame->closure->function->module->globals)
    LOAD_FRAME();

#define READ_BYTE()   (*frame->ip++)
#define READ_SHORT()  (frame->ip+=2,(u16)((frame->ip[-2]<<8)|frame->ip[-1]))
#define READ_24()     (frame->ip+=3,(u32)((frame->ip[-3]<<16)|(frame->ip[-2]<<8)|frame->ip[-1]))
#define READ_CONST()  (frame->closure->function->chunk.constants.values[READ_BYTE()])

#define BINARY_OP(vtype,op) do{ \
    if(!IS_NUMBER(peek(0))||!IS_NUMBER(peek(1))){ runtime_error("Operands must be numbers."); return INTERPRET_RUNTIME_ERR; } \
    double b=AS_NUMBER(pop()),a=AS_NUMBER(pop()); push(vtype(a op b)); }while(0)

#define BINARY_BITWISE_OP(op) do{ \
    if(!IS_NUMBER(peek(0))||!IS_NUMBER(peek(1))){ runtime_error("Operands must be numbers."); return INTERPRET_RUNTIME_ERR; } \
    i32 b=(i32)AS_NUMBER(pop()),a=(i32)AS_NUMBER(pop()); push(NUMBER_VAL((double)(a op b))); }while(0)

    Value k;    // the constant operand; OP_WIDE loads it and jumps to the opcode's label

#ifdef LUNAR_COMPUTED_GOTO
    // every byte that isn't an opcode goes to the "Unknown opcode" handler
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
#endif
#define DISPATCH_ENTRY(op) [op]=&&L_##op,
    static void* dispatch_table[256]={ [0 ... 255]=&&L_unknown, LUNAR_OPCODES(DISPATCH_ENTRY) };
#undef DISPATCH_ENTRY
#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif
#endif

    DISPATCH_LOOP {
#ifdef LUNAR_DEBUG_TRACE_EXECUTION
        printf("          ");
        for(Value* s=lvm.stack;s<lvm.stack_top;s++){ printf("[ "); printValue(*s); printf(" ]"); }
        printf("\n");
        disassemble_instruction(&frame->closure->function->chunk,(int)(frame->ip-frame->closure->function->chunk.code));
#endif
        DISPATCH_SWITCH {
            CASE(OP_CONST):   k=READ_CONST(); op_const: push(k); NEXT;
            CASE(OP_NIL):     push(NIL_VAL); NEXT;
            CASE(OP_TRUE):    push(BOOL_VAL(true)); NEXT;
            CASE(OP_FALSE):   push(BOOL_VAL(false)); NEXT;
            CASE(OP_POP):     pop(); NEXT;
            CASE(OP_GET_LOCAL):  push(frame->slots[READ_BYTE()]); NEXT;
            CASE(OP_SET_LOCAL):  frame->slots[READ_BYTE()]=peek(0); NEXT;
            CASE(OP_DEFINE_GLOBAL): k=READ_CONST(); op_define_global: { ObjString* n=AS_STRING(k); table_set(globals,n,peek(0)); pop(); NEXT; }
            CASE(OP_GET_GLOBAL): k=READ_CONST(); op_get_global: {
                ObjString* n=AS_STRING(k); Value v;
                if(!table_get(globals,n,&v)){ runtime_error("Undefined variable '%s'.",n->chars); return INTERPRET_RUNTIME_ERR; }
                push(v); NEXT;
            }
            CASE(OP_SET_GLOBAL): k=READ_CONST(); op_set_global: {
                ObjString* n=AS_STRING(k);
                if(table_set(globals,n,peek(0))){ table_delete(globals,n); runtime_error("Undefined variable '%s'.",n->chars); return INTERPRET_RUNTIME_ERR; }
                NEXT;
            }
            CASE(OP_GET_UPVALUE): push(*frame->closure->upvalues[READ_BYTE()]->location); NEXT;
            CASE(OP_SET_UPVALUE): *frame->closure->upvalues[READ_BYTE()]->location=peek(0); NEXT;
            CASE(OP_CLOSE_UPVALUE): close_upvalues(lvm.stack_top-1); pop(); NEXT;
            CASE(OP_NEGATE):
                if(!IS_NUMBER(peek(0))){ runtime_error("Operand must be a number."); return INTERPRET_RUNTIME_ERR; }
                push(NUMBER_VAL(-AS_NUMBER(pop()))); NEXT;
            CASE(OP_ADD):
                if(IS_STRING(peek(0))&&IS_STRING(peek(1))){ concatenate(); }
                else if(IS_NUMBER(peek(0))&&IS_NUMBER(peek(1))){ double b=AS_NUMBER(pop()),a=AS_NUMBER(pop()); push(NUMBER_VAL(a+b)); }
                else{ runtime_error("Operands must be two numbers or two strings."); return INTERPRET_RUNTIME_ERR; }
                NEXT;
            CASE(OP_SUB): BINARY_OP(NUMBER_VAL,-); NEXT;
            CASE(OP_MUL): BINARY_OP(NUMBER_VAL,*); NEXT;
            CASE(OP_DIV): BINARY_OP(NUMBER_VAL,/); NEXT;
            CASE(OP_MOD): BINARY_BITWISE_OP(%); NEXT;
            CASE(OP_POW): {
                if(!IS_NUMBER(peek(0))||!IS_NUMBER(peek(1))){ runtime_error("Operands must be numbers."); return INTERPRET_RUNTIME_ERR; }
                double e=AS_NUMBER(pop()),b=AS_NUMBER(pop()),r;
                if(e==0.0) r=1.0; else if(b==0.0) r=0.0; else if(e==2.0) r=b*b;
                else if(e==0.5) r=sqrt(b); else if(b<0.0&&e!=(i32)e) r=NAN;
                else r=pow(b,e);
                push(NUMBER_VAL(r)); NEXT;
            }
            CASE(OP_BITWISE_AND):        BINARY_BITWISE_OP(&);  NEXT;
            CASE(OP_BITWISE_OR):         BINARY_BITWISE_OP(|);  NEXT;
            CASE(OP_BITWISE_XOR):        BINARY_BITWISE_OP(^);  NEXT;
            CASE(OP_BITWISE_LEFTSHIFT):  BINARY_BITWISE_OP(<<); NEXT;
            CASE(OP_BITWISE_RIGHTSHIFT): BINARY_BITWISE_OP(>>); NEXT;
            CASE(OP_BITWISE_NOT):
                if(!IS_NUMBER(peek(0))){ runtime_error("Operand must be a number."); return INTERPRET_RUNTIME_ERR; }
                push(NUMBER_VAL((double)(~(i32)AS_NUMBER(pop())))); NEXT;
            CASE(OP_NOT):     push(BOOL_VAL(is_falsey(pop()))); NEXT;
            CASE(OP_EQU):     { Value b=pop(),a=pop(); push(BOOL_VAL(values_equ(a,b))); NEXT; }
            CASE(OP_GREATER): BINARY_OP(BOOL_VAL,>); NEXT;
            CASE(OP_LESS):    BINARY_OP(BOOL_VAL,<); NEXT;
            CASE(OP_PRINT):   printValue(pop()); printf("\n"); NEXT;
            CASE(OP_JUMP):          { u16 o=READ_SHORT(); frame->ip+=o; NEXT; }
            CASE(OP_JUMP_IF_FALSE): { u16 o=READ_SHORT(); if(is_falsey(peek(0))) frame->ip+=o; NEXT; }
            CASE(OP_LOOP):          { u16 o=READ_SHORT(); frame->ip-=o; NEXT; }
            CASE(OP_CLOSURE): k=READ_CONST(); op_closure: {
                ObjFunction* fn=AS_FUNCTION(k);
                ObjClosure* closure=new_closure(fn);
                push(OBJ_VAL(closure));
                for(int i=0;i<closure->upvalue_count;i++){
                    u8 is_local=READ_BYTE(),index=READ_BYTE();
                    closure->upvalues[i]=is_local?capture_upvalue(frame->slots+index):frame->closure->upvalues[index];
                }
                NEXT;
            }
            CASE(OP_BUILD_ARRAY): {
                int n = READ_BYTE();
                ObjArray* array = new_array();
                push(OBJ_VAL(array));                 // root it: write_value_array can trigger GC
                for (int i = 0; i < n; i++) {
                    write_value_array(&array->items, peek(n - i));  // peek(n) = first element
                }
                Value result = pop();                 // the array
                lvm.stack_top -= n;                   // drop the elements
                push(result);
                NEXT;
            }

            CASE(OP_BUILD_MAP): {
                int n = READ_BYTE();
                ObjMap* map = new_map();
                push(OBJ_VAL(map));                   // root it: map_set can trigger GC
                if (!add_map_pairs(map, lvm.stack_top - 1 - 2*n, n)) return INTERPRET_RUNTIME_ERR;
                Value result = pop();                 // the map
                lvm.stack_top -= 2*n;                 // drop the pairs
                push(result);
                NEXT;
            }

            CASE(OP_APPEND_MAP): {
                int n = READ_BYTE();
                ObjMap* map = AS_MAP(peek(2*n));
                if (!add_map_pairs(map, lvm.stack_top - 2*n, n)) return INTERPRET_RUNTIME_ERR;
                lvm.stack_top -= 2*n;                 // drop the pairs, keep the map
                NEXT;
            }

            // for-in: the collection is on top; push the items the loop walks
            CASE(OP_ITER_PREP): {
                int nvars = READ_BYTE();
                Value seq = peek(0);
                if (IS_ARRAY(seq)) { push(seq); NEXT; }        // walked live, no copy
                if (IS_MAP(seq)) {
                    ObjMap* m = AS_MAP(seq);
                    ObjArray* items = new_array();                // seq is still on the stack
                    for (int i = 0; i < m->count; i++) {
                        if (!IS_NIL(m->entries[i].key)) write_value_array(&items->items, m->entries[i].key);
                    }
                    push(OBJ_VAL(items));
                    NEXT;
                }
                if (IS_STRING(seq)) {
                    if (nvars == 2) {
                        runtime_error("Two-variable for-in isn't supported on strings yet.");
                        return INTERPRET_RUNTIME_ERR;
                    }
                    ObjString* s = AS_STRING(seq);
                    ObjArray* items = new_array();
                    push(OBJ_VAL(items));                         // root it before copying characters
                    for (int i = 0; i < s->length; ) {
                        int n = utf8_char_length(s->chars + i, s->length - i);
                        write_value_array(&items->items, OBJ_VAL(copy_str(s->chars + i, n)));
                        i += n;
                    }
                    NEXT;
                }
                runtime_error("Can only loop over arrays, maps, and strings.");
                return INTERPRET_RUNTIME_ERR;
            }

            // for-in step: slots base, base+1, base+2 hold the collection, items, and position
            CASE(OP_FOR_ITER): {
                Value* base = frame->slots + READ_BYTE();
                int nvars   = READ_BYTE();
                u16 exit    = READ_SHORT();
                ObjArray* items = AS_ARRAY(base[1]);
                i32 i = (i32)AS_NUMBER(base[2]);
                if (IS_MAP(base[0])) {
                    Value v;
                    while (i < items->items.count && !map_get(AS_MAP(base[0]), items->items.values[i], &v)) i++;   // removed since
                    if (i >= items->items.count) { frame->ip += exit; NEXT; }
                    base[2] = NUMBER_VAL(i + 1);
                    push(items->items.values[i]);
                    if (nvars == 2) push(v);
                    NEXT;
                }
                if (i >= items->items.count) { frame->ip += exit; NEXT; }   // arrays: length checked live
                base[2] = NUMBER_VAL(i + 1);
                if (nvars == 2) push(NUMBER_VAL(i));
                push(items->items.values[i]);
                NEXT;
            }

            CASE(OP_TRY): {
                u16 o=READ_SHORT();
                if(lvm.handler_count==HANDLERS_MAX){ runtime_error("Too many nested try blocks."); return INTERPRET_RUNTIME_ERR; }
                Handler* h=&lvm.handlers[lvm.handler_count++];
                h->frame_count=lvm.frame_count; h->stack_top=lvm.stack_top; h->catch_ip=frame->ip+o;
                NEXT;
            }
            CASE(OP_POP_HANDLER): lvm.handler_count--; NEXT;
            CASE(OP_BUILD_STRING): {
                int n = READ_BYTE();
                ObjString* joined = join_parts(lvm.stack_top - n, n);
                lvm.stack_top -= n;
                push(OBJ_VAL(joined));
                NEXT;
            }
            CASE(OP_THROW): lvm.error_value=pop(); lvm.error_pending=true; return INTERPRET_RUNTIME_ERR;

            CASE(OP_GET_INDEX): {
                if (!IS_ARRAY(peek(1))) {
                    if (IS_MAP(peek(1))) {
                        const char* err = map_key_error(peek(0));
                        if (err != NULL) { runtime_error("%s", err); return INTERPRET_RUNTIME_ERR; }
                        Value v;
                        if (!map_get(AS_MAP(peek(1)), peek(0), &v)) v = NIL_VAL;   // missing key
                        pop(); pop();
                        push(v);
                        NEXT;
                    }
                    runtime_error("Only arrays and maps can be indexed.");
                    return INTERPRET_RUNTIME_ERR;
                }
                if (!IS_NUMBER(peek(0))) {
                    runtime_error("Array index must be a number.");
                    return INTERPRET_RUNTIME_ERR;
                }
                ObjArray* array = AS_ARRAY(peek(1));
                double d = AS_NUMBER(peek(0));
                if (d != floor(d) || d < 0 || d >= array->items.count) {
                    runtime_error("Array index %g out of bounds (length %d).", d, array->items.count);
                    return INTERPRET_RUNTIME_ERR;
                }
                Value v = array->items.values[(i32)d];
                pop(); pop();
                push(v);
                NEXT;
            }
            
            CASE(OP_SET_INDEX): {
                if (!IS_ARRAY(peek(2))) {
                    if (IS_MAP(peek(2))) {
                        const char* err = map_key_error(peek(1));
                        if (err != NULL) { runtime_error("%s", err); return INTERPRET_RUNTIME_ERR; }
                        map_set(AS_MAP(peek(2)), peek(1), peek(0));   // all three still on the stack
                        Value v = pop();
                        pop(); pop();
                        push(v);                      // assignment is an expression
                        NEXT;
                    }
                    runtime_error("Only arrays and maps can be indexed.");
                    return INTERPRET_RUNTIME_ERR;
                }
                if (!IS_NUMBER(peek(1))) {
                    runtime_error("Array index must be a number.");
                    return INTERPRET_RUNTIME_ERR;
                }
                ObjArray* array = AS_ARRAY(peek(2));
                double d = AS_NUMBER(peek(1));
                if (d != floor(d) || d < 0 || d >= array->items.count) {
                    runtime_error("Array index %g out of bounds (length %d).", d, array->items.count);
                    return INTERPRET_RUNTIME_ERR;
                }
                array->items.values[(i32)d] = peek(0);
                Value v = pop();
                pop(); pop();
                push(v);                              // assignment is an expression
                NEXT;
            }

            CASE(OP_CALL): {
                int argc=READ_BYTE();
                if(!call_value(peek(argc),argc)) return INTERPRET_RUNTIME_ERR;
                LOAD_FRAME(); NEXT;
            }
            CASE(OP_RET): {
                Value result=pop(); close_upvalues(frame->slots);
                lvm.frame_count--;
                if(lvm.frame_count==0){ pop(); return INTERPRET_OK; }
                lvm.stack_top=frame->slots; push(result);
                LOAD_FRAME(); NEXT;
            }
            // the end of a file's top-level code: the end of the script, or of an import
            CASE(OP_MODULE_RET): {
                Value result=pop(); close_upvalues(frame->slots);
                lvm.frame_count--;
                if(lvm.frame_count==0){ pop(); return INTERPRET_OK; }
                ObjModule* m=frame->closure->function->module;
                m->exports=result; m->loaded=true;
                lvm.loading_count--;
                lvm.stack_top=frame->slots; push(result);
                LOAD_FRAME(); NEXT;
            }
            CASE(OP_IMPORT):
                if(!import_module(frame->closure->function->module)) return INTERPRET_RUNTIME_ERR;
                LOAD_FRAME(); NEXT;
            CASE(OP_STRUCT): k=READ_CONST(); op_struct: push(OBJ_VAL(new_struct(AS_STRING(k)))); NEXT;
            CASE(OP_GET_PROPERTY): k=READ_CONST(); op_get_property: {
                if(!IS_INSTANCE(peek(0))){
                    if(IS_MAP(peek(0))){    // m.key is m["key"]; a missing key is nil
                        Value val; if(!map_get(AS_MAP(peek(0)),k,&val)) val=NIL_VAL;
                        pop(); push(val); NEXT;
                    }
                    runtime_error("Only instances and maps have properties."); return INTERPRET_RUNTIME_ERR;
                }
                ObjInstance* inst=AS_INSTANCE(peek(0)); ObjString* name=AS_STRING(k);
                Value val;
                if(table_get(inst->fields,name,&val)){ pop(); push(val); NEXT; }
                if(!bind_method(inst->klass,name)) return INTERPRET_RUNTIME_ERR;
                NEXT;
            }
            CASE(OP_SET_PROPERTY): k=READ_CONST(); op_set_property: {
                if(!IS_INSTANCE(peek(1))){
                    if(IS_MAP(peek(1))){    // m.key = v is m["key"] = v
                        map_set(AS_MAP(peek(1)),k,peek(0));
                        Value val=pop(); pop(); push(val); NEXT;
                    }
                    runtime_error("Only instances and maps have fields."); return INTERPRET_RUNTIME_ERR;
                }
                ObjInstance* inst=AS_INSTANCE(peek(1)); ObjString* name=AS_STRING(k);
                table_set(inst->fields,name,peek(0));
                Value val=pop(); pop(); push(val); NEXT;
            }
            CASE(OP_METHOD): k=READ_CONST(); op_method: define_method(AS_STRING(k)); NEXT;
            CASE(OP_INVOKE): k=READ_CONST(); op_invoke: {
                ObjString* method=AS_STRING(k); int argc=READ_BYTE();
                if(!invoke(method,argc)) return INTERPRET_RUNTIME_ERR;
                LOAD_FRAME(); NEXT;
            }
            CASE(OP_INHERIT): {
                if(!IS_STRUCT(peek(1))){ runtime_error("Supertype must be a struct."); return INTERPRET_RUNTIME_ERR; }
                ObjStruct* sup=AS_STRUCT(peek(1)); ObjStruct* sub=AS_STRUCT(peek(0));
                table_add_all(sup->methods,sub->methods); pop(); NEXT;
            }
            CASE(OP_GET_SUPER): k=READ_CONST(); op_get_super: {
                ObjString* name=AS_STRING(k); ObjStruct* sup=AS_STRUCT(pop());
                if(!bind_method(sup,name)) return INTERPRET_RUNTIME_ERR;
                NEXT;
            }
            CASE(OP_SUPER_INVOKE): k=READ_CONST(); op_super_invoke: {
                ObjString* method=AS_STRING(k); int argc=READ_BYTE(); ObjStruct* sup=AS_STRUCT(pop());
                if(!invoke_from_class(sup,method,argc)) return INTERPRET_RUNTIME_ERR;
                LOAD_FRAME(); NEXT;
            }
            CASE(OP_APPEND_ARRAY): {
                int n = READ_BYTE();
                ObjArray* array = AS_ARRAY(peek(n));
                for (int i = 0; i < n; i++) {
                    write_value_array(&array->items, peek(n - 1 - i));  // peek(n-1) = first element
                }
                lvm.stack_top -= n;                   // drop the elements, keep the array
                NEXT;
            }
            CASE(OP_WIDE): {
                u8 op=READ_BYTE();
                k=frame->closure->function->chunk.constants.values[READ_24()];
                switch(op){
                    case OP_CONST:          goto op_const;
                    case OP_DEFINE_GLOBAL:  goto op_define_global;
                    case OP_GET_GLOBAL:     goto op_get_global;
                    case OP_SET_GLOBAL:     goto op_set_global;
                    case OP_CLOSURE:        goto op_closure;
                    case OP_STRUCT:         goto op_struct;
                    case OP_GET_PROPERTY:   goto op_get_property;
                    case OP_SET_PROPERTY:   goto op_set_property;
                    case OP_METHOD:         goto op_method;
                    case OP_INVOKE:         goto op_invoke;
                    case OP_GET_SUPER:      goto op_get_super;
                    case OP_SUPER_INVOKE:   goto op_super_invoke;
                    default:
                        fprintf(stderr,"Unknown wide opcode: %d\n",op);
                        return INTERPRET_RUNTIME_ERR;
                }
            }
            CASE_DEFAULT:
                fprintf(stderr,"Unknown opcode: %d\n",*(frame->ip-1));
                return INTERPRET_RUNTIME_ERR;
        }
    }
#undef LOAD_FRAME
#undef READ_BYTE
#undef READ_SHORT
#undef READ_24
#undef READ_CONST
#undef BINARY_OP
#undef BINARY_BITWISE_OP
#undef DISPATCH_LOOP
#undef DISPATCH_SWITCH
#undef CASE
#undef CASE_DEFAULT
#undef NEXT
}
