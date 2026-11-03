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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

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
    table_set(&lvm.globals, AS_STRING(lvm.stack[0]), lvm.stack[1]);
    pop(); pop();
}

void init_lunar_vm() {
    reset_stack();
    lvm.objects=NULL; lvm.bytes_allocated=0; lvm.next_gc=1024*1024;
    lvm.gray_count=0; lvm.gray_capacity=0; lvm.gray_stack=NULL;
    lvm.open_upvalues=NULL;
    init_table(&lvm.globals);
    init_table(&lvm.strings);
    lvm.init_string=NULL;
    lvm.init_string=copy_str("init",4);

    register_std_natives();

    ObjNative* ffiLoadNative = new_native(ffi_load_library, "clib");
    push(OBJ_VAL(ffiLoadNative)); // Push to stack to protect from a sudden GC sweep
    push(OBJ_VAL(copy_str("clib", 4)));   // root the name too: table_set can trigger GC
    table_set(&lvm.globals, AS_STRING(lvm.stack_top[-1]), lvm.stack_top[-2]);
    pop(); pop();

    ObjNative* ffiBindNative = new_native(ffi_bind_function, "cbind");
    push(OBJ_VAL(ffiBindNative));
    push(OBJ_VAL(copy_str("cbind", 5)));
    table_set(&lvm.globals, AS_STRING(lvm.stack_top[-1]), lvm.stack_top[-2]);
    pop(); pop();

#ifdef LUNAR_SDL
    register_sdl_natives();
#endif
}

void free_lunar_vm() {
    free_table(&lvm.globals);
    free_table(&lvm.strings);
    lvm.init_string=NULL;
    freeObjects();
}

static void reset_stack() {
    lvm.stack_top=lvm.stack; lvm.frame_count=0; lvm.open_upvalues=NULL;
}

void push(Value val) {
    if(lvm.stack_top>=lvm.stack+STACK_MAX){fprintf(stderr,"Stack overflow.\n");exit(1);}
    *lvm.stack_top++=val;
}
Value pop()                 { return *--lvm.stack_top; }
static Value peek(int d)    { return lvm.stack_top[-1-d]; }

void runtime_error(const char* fmt, ...) {
    va_list args; va_start(args,fmt); vfprintf(stderr,fmt,args); va_end(args);
    fputs("\n",stderr);
    for(int i=lvm.frame_count-1;i>=0;i--){
        CallFrame* f=&lvm.frames[i]; ObjFunction* fn=f->closure->function;
        size_t instr=f->ip-fn->chunk.code-1;
        fprintf(stderr,"[line %d] in ",fn->chunk.lines[instr]);
        if(fn->name==NULL) fprintf(stderr,"script\n");
        else               fprintf(stderr,"%s()\n",fn->name->chars);
    }
    reset_stack();
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
            if(lvm.frame_count==0) return false;   // the native raised runtime_error, which reset the stack
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
            if (lvm.frame_count == 0) return false;   // runtime_error reset the stack
            
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
static int utf8_char_length(const char* s, int remaining) {
    u8 c = (u8)s[0];
    int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (n > remaining) return 1;
    for (int i = 1; i < n; i++) {
        if (((u8)s[i] & 0xC0) != 0x80) return 1;
    }
    return n;
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

InterpretResult interpret(const char* src) {
    ObjFunction* fn=compile(src);
    if(fn==NULL) return INTERPRET_COMPILE_ERR;
    push(OBJ_VAL(fn));
    ObjClosure* closure=new_closure(fn);
    pop(); push(OBJ_VAL(closure));
    call(closure,0);
    return run();
}

static InterpretResult run() {
    CallFrame* frame=&lvm.frames[lvm.frame_count-1];

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

    for(;;){
#ifdef LUNAR_DEBUG_TRACE_EXECUTION
        printf("          ");
        for(Value* s=lvm.stack;s<lvm.stack_top;s++){ printf("[ "); printValue(*s); printf(" ]"); }
        printf("\n");
        disassemble_instruction(&frame->closure->function->chunk,(int)(frame->ip-frame->closure->function->chunk.code));
#endif
        switch(READ_BYTE()){
            case OP_CONST:   k=READ_CONST(); op_const: push(k); break;
            case OP_NIL:     push(NIL_VAL); break;
            case OP_TRUE:    push(BOOL_VAL(true)); break;
            case OP_FALSE:   push(BOOL_VAL(false)); break;
            case OP_POP:     pop(); break;
            case OP_GET_LOCAL:  push(frame->slots[READ_BYTE()]); break;
            case OP_SET_LOCAL:  frame->slots[READ_BYTE()]=peek(0); break;
            case OP_DEFINE_GLOBAL: k=READ_CONST(); op_define_global: { ObjString* n=AS_STRING(k); table_set(&lvm.globals,n,peek(0)); pop(); break; }
            case OP_GET_GLOBAL: k=READ_CONST(); op_get_global: {
                ObjString* n=AS_STRING(k); Value v;
                if(!table_get(&lvm.globals,n,&v)){ runtime_error("Undefined variable '%s'.",n->chars); return INTERPRET_RUNTIME_ERR; }
                push(v); break;
            }
            case OP_SET_GLOBAL: k=READ_CONST(); op_set_global: {
                ObjString* n=AS_STRING(k);
                if(table_set(&lvm.globals,n,peek(0))){ table_delete(&lvm.globals,n); runtime_error("Undefined variable '%s'.",n->chars); return INTERPRET_RUNTIME_ERR; }
                break;
            }
            case OP_GET_UPVALUE: push(*frame->closure->upvalues[READ_BYTE()]->location); break;
            case OP_SET_UPVALUE: *frame->closure->upvalues[READ_BYTE()]->location=peek(0); break;
            case OP_CLOSE_UPVALUE: close_upvalues(lvm.stack_top-1); pop(); break;
            case OP_NEGATE:
                if(!IS_NUMBER(peek(0))){ runtime_error("Operand must be a number."); return INTERPRET_RUNTIME_ERR; }
                push(NUMBER_VAL(-AS_NUMBER(pop()))); break;
            case OP_ADD:
                if(IS_STRING(peek(0))&&IS_STRING(peek(1))){ concatenate(); }
                else if(IS_NUMBER(peek(0))&&IS_NUMBER(peek(1))){ double b=AS_NUMBER(pop()),a=AS_NUMBER(pop()); push(NUMBER_VAL(a+b)); }
                else{ runtime_error("Operands must be two numbers or two strings."); return INTERPRET_RUNTIME_ERR; }
                break;
            case OP_SUB: BINARY_OP(NUMBER_VAL,-); break;
            case OP_MUL: BINARY_OP(NUMBER_VAL,*); break;
            case OP_DIV: BINARY_OP(NUMBER_VAL,/); break;
            case OP_MOD: BINARY_BITWISE_OP(%); break;
            case OP_POW: {
                if(!IS_NUMBER(peek(0))||!IS_NUMBER(peek(1))){ runtime_error("Operands must be numbers."); return INTERPRET_RUNTIME_ERR; }
                double e=AS_NUMBER(pop()),b=AS_NUMBER(pop()),r;
                if(e==0.0) r=1.0; else if(b==0.0) r=0.0; else if(e==2.0) r=b*b;
                else if(e==0.5) r=sqrt(b); else if(b<0.0&&e!=(i32)e) r=NAN;
                else r=pow(b,e);
                push(NUMBER_VAL(r)); break;
            }
            case OP_BITWISE_AND:        BINARY_BITWISE_OP(&);  break;
            case OP_BITWISE_OR:         BINARY_BITWISE_OP(|);  break;
            case OP_BITWISE_XOR:        BINARY_BITWISE_OP(^);  break;
            case OP_BITWISE_LEFTSHIFT:  BINARY_BITWISE_OP(<<); break;
            case OP_BITWISE_RIGHTSHIFT: BINARY_BITWISE_OP(>>); break;
            case OP_BITWISE_NOT:
                if(!IS_NUMBER(peek(0))){ runtime_error("Operand must be a number."); return INTERPRET_RUNTIME_ERR; }
                push(NUMBER_VAL((double)(~(i32)AS_NUMBER(pop())))); break;
            case OP_NOT:     push(BOOL_VAL(is_falsey(pop()))); break;
            case OP_EQU:     { Value b=pop(),a=pop(); push(BOOL_VAL(values_equ(a,b))); break; }
            case OP_GREATER: BINARY_OP(BOOL_VAL,>); break;
            case OP_LESS:    BINARY_OP(BOOL_VAL,<); break;
            case OP_PRINT:   printValue(pop()); printf("\n"); break;
            case OP_JUMP:          { u16 o=READ_SHORT(); frame->ip+=o; break; }
            case OP_JUMP_IF_FALSE: { u16 o=READ_SHORT(); if(is_falsey(peek(0))) frame->ip+=o; break; }
            case OP_LOOP:          { u16 o=READ_SHORT(); frame->ip-=o; break; }
            case OP_CLOSURE: k=READ_CONST(); op_closure: {
                ObjFunction* fn=AS_FUNCTION(k);
                ObjClosure* closure=new_closure(fn);
                push(OBJ_VAL(closure));
                for(int i=0;i<closure->upvalue_count;i++){
                    u8 is_local=READ_BYTE(),index=READ_BYTE();
                    closure->upvalues[i]=is_local?capture_upvalue(frame->slots+index):frame->closure->upvalues[index];
                }
                break;
            }
            case OP_BUILD_ARRAY: {
                int n = READ_BYTE();
                ObjArray* array = new_array();
                push(OBJ_VAL(array));                 // root it: write_value_array can trigger GC
                for (int i = 0; i < n; i++) {
                    write_value_array(&array->items, peek(n - i));  // peek(n) = first element
                }
                Value result = pop();                 // the array
                lvm.stack_top -= n;                   // drop the elements
                push(result);
                break;
            }

            case OP_BUILD_MAP: {
                int n = READ_BYTE();
                ObjMap* map = new_map();
                push(OBJ_VAL(map));                   // root it: map_set can trigger GC
                if (!add_map_pairs(map, lvm.stack_top - 1 - 2*n, n)) return INTERPRET_RUNTIME_ERR;
                Value result = pop();                 // the map
                lvm.stack_top -= 2*n;                 // drop the pairs
                push(result);
                break;
            }

            case OP_APPEND_MAP: {
                int n = READ_BYTE();
                ObjMap* map = AS_MAP(peek(2*n));
                if (!add_map_pairs(map, lvm.stack_top - 2*n, n)) return INTERPRET_RUNTIME_ERR;
                lvm.stack_top -= 2*n;                 // drop the pairs, keep the map
                break;
            }

            // for-in: the collection is on top; push the items the loop walks
            case OP_ITER_PREP: {
                int nvars = READ_BYTE();
                Value seq = peek(0);
                if (IS_ARRAY(seq)) { push(seq); break; }        // walked live, no copy
                if (IS_MAP(seq)) {
                    ObjMap* m = AS_MAP(seq);
                    ObjArray* items = new_array();                // seq is still on the stack
                    for (int i = 0; i < m->count; i++) {
                        if (!IS_NIL(m->entries[i].key)) write_value_array(&items->items, m->entries[i].key);
                    }
                    push(OBJ_VAL(items));
                    break;
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
                    break;
                }
                runtime_error("Can only loop over arrays, maps, and strings.");
                return INTERPRET_RUNTIME_ERR;
            }

            // for-in step: slots base, base+1, base+2 hold the collection, items, and position
            case OP_FOR_ITER: {
                Value* base = frame->slots + READ_BYTE();
                int nvars   = READ_BYTE();
                u16 exit    = READ_SHORT();
                ObjArray* items = AS_ARRAY(base[1]);
                i32 i = (i32)AS_NUMBER(base[2]);
                if (IS_MAP(base[0])) {
                    Value v;
                    while (i < items->items.count && !map_get(AS_MAP(base[0]), items->items.values[i], &v)) i++;   // removed since
                    if (i >= items->items.count) { frame->ip += exit; break; }
                    base[2] = NUMBER_VAL(i + 1);
                    push(items->items.values[i]);
                    if (nvars == 2) push(v);
                    break;
                }
                if (i >= items->items.count) { frame->ip += exit; break; }   // arrays: length checked live
                base[2] = NUMBER_VAL(i + 1);
                if (nvars == 2) push(NUMBER_VAL(i));
                push(items->items.values[i]);
                break;
            }

            case OP_GET_INDEX: {
                if (!IS_ARRAY(peek(1))) {
                    if (IS_MAP(peek(1))) {
                        const char* err = map_key_error(peek(0));
                        if (err != NULL) { runtime_error("%s", err); return INTERPRET_RUNTIME_ERR; }
                        Value v;
                        if (!map_get(AS_MAP(peek(1)), peek(0), &v)) v = NIL_VAL;   // missing key
                        pop(); pop();
                        push(v);
                        break;
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
                break;
            }
            
            case OP_SET_INDEX: {
                if (!IS_ARRAY(peek(2))) {
                    if (IS_MAP(peek(2))) {
                        const char* err = map_key_error(peek(1));
                        if (err != NULL) { runtime_error("%s", err); return INTERPRET_RUNTIME_ERR; }
                        map_set(AS_MAP(peek(2)), peek(1), peek(0));   // all three still on the stack
                        Value v = pop();
                        pop(); pop();
                        push(v);                      // assignment is an expression
                        break;
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
                break;
            }

            case OP_CALL: {
                int argc=READ_BYTE();
                if(!call_value(peek(argc),argc)) return INTERPRET_RUNTIME_ERR;
                frame=&lvm.frames[lvm.frame_count-1]; break;
            }
            case OP_RET: {
                Value result=pop(); close_upvalues(frame->slots);
                lvm.frame_count--;
                if(lvm.frame_count==0){ pop(); return INTERPRET_OK; }
                lvm.stack_top=frame->slots; push(result);
                frame=&lvm.frames[lvm.frame_count-1]; break;
            }
            case OP_STRUCT: k=READ_CONST(); op_struct: push(OBJ_VAL(new_struct(AS_STRING(k)))); break;
            case OP_GET_PROPERTY: k=READ_CONST(); op_get_property: {
                if(!IS_INSTANCE(peek(0))){
                    if(IS_MAP(peek(0))){    // m.key is m["key"]; a missing key is nil
                        Value val; if(!map_get(AS_MAP(peek(0)),k,&val)) val=NIL_VAL;
                        pop(); push(val); break;
                    }
                    runtime_error("Only instances and maps have properties."); return INTERPRET_RUNTIME_ERR;
                }
                ObjInstance* inst=AS_INSTANCE(peek(0)); ObjString* name=AS_STRING(k);
                Value val;
                if(table_get(inst->fields,name,&val)){ pop(); push(val); break; }
                if(!bind_method(inst->klass,name)) return INTERPRET_RUNTIME_ERR;
                break;
            }
            case OP_SET_PROPERTY: k=READ_CONST(); op_set_property: {
                if(!IS_INSTANCE(peek(1))){
                    if(IS_MAP(peek(1))){    // m.key = v is m["key"] = v
                        map_set(AS_MAP(peek(1)),k,peek(0));
                        Value val=pop(); pop(); push(val); break;
                    }
                    runtime_error("Only instances and maps have fields."); return INTERPRET_RUNTIME_ERR;
                }
                ObjInstance* inst=AS_INSTANCE(peek(1)); ObjString* name=AS_STRING(k);
                table_set(inst->fields,name,peek(0));
                Value val=pop(); pop(); push(val); break;
            }
            case OP_METHOD: k=READ_CONST(); op_method: define_method(AS_STRING(k)); break;
            case OP_INVOKE: k=READ_CONST(); op_invoke: {
                ObjString* method=AS_STRING(k); int argc=READ_BYTE();
                if(!invoke(method,argc)) return INTERPRET_RUNTIME_ERR;
                frame=&lvm.frames[lvm.frame_count-1]; break;
            }
            case OP_INHERIT: {
                if(!IS_STRUCT(peek(1))){ runtime_error("Supertype must be a struct."); return INTERPRET_RUNTIME_ERR; }
                ObjStruct* sup=AS_STRUCT(peek(1)); ObjStruct* sub=AS_STRUCT(peek(0));
                table_add_all(sup->methods,sub->methods); pop(); break;
            }
            case OP_GET_SUPER: k=READ_CONST(); op_get_super: {
                ObjString* name=AS_STRING(k); ObjStruct* sup=AS_STRUCT(pop());
                if(!bind_method(sup,name)) return INTERPRET_RUNTIME_ERR;
                break;
            }
            case OP_SUPER_INVOKE: k=READ_CONST(); op_super_invoke: {
                ObjString* method=AS_STRING(k); int argc=READ_BYTE(); ObjStruct* sup=AS_STRUCT(pop());
                if(!invoke_from_class(sup,method,argc)) return INTERPRET_RUNTIME_ERR;
                frame=&lvm.frames[lvm.frame_count-1]; break;
            }
            case OP_APPEND_ARRAY: {
                int n = READ_BYTE();
                ObjArray* array = AS_ARRAY(peek(n));
                for (int i = 0; i < n; i++) {
                    write_value_array(&array->items, peek(n - 1 - i));  // peek(n-1) = first element
                }
                lvm.stack_top -= n;                   // drop the elements, keep the array
                break;
            }
            case OP_WIDE: {
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
            default:
                fprintf(stderr,"Unknown opcode: %d\n",*(frame->ip-1));
                return INTERPRET_RUNTIME_ERR;
        }
    }
#undef READ_BYTE
#undef READ_SHORT
#undef READ_24
#undef READ_CONST
#undef BINARY_OP
#undef BINARY_BITWISE_OP
}
