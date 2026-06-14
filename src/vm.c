#include "vm.h"
#include "debug.h"
#include "compiler.h"
#include "object.h"
#include "memory.h"
#include "table.h"

#ifdef LUNAR_SDL
#include "lunar_sdl.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

LunarVM lvm;

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

/* ── natives ── */
static Value clock_native(int argc, Value* args) { return NUMBER_VAL((double)clock()/CLOCKS_PER_SEC); }
static Value sqrt_native(int argc, Value* args)  { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(sqrt(AS_NUMBER(args[0]))); }
static Value abs_native(int argc, Value* args)   { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(fabs(AS_NUMBER(args[0]))); }
static Value floor_native(int argc, Value* args) { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(floor(AS_NUMBER(args[0]))); }
static Value ceil_native(int argc, Value* args)  { if(argc!=1||!IS_NUMBER(args[0]))return NIL_VAL; return NUMBER_VAL(ceil(AS_NUMBER(args[0]))); }
static Value len_native(int argc, Value* args)   { if(argc!=1||!IS_STRING(args[0]))return NIL_VAL; return NUMBER_VAL((double)AS_STRING(args[0])->length); }
static Value str_native(int argc, Value* args) {
    if(argc!=1) return NIL_VAL;
    if(IS_STRING(args[0])) return args[0];
    char buf[64]; int len;
    if     (IS_NUMBER(args[0])) len=snprintf(buf,sizeof(buf),"%g",AS_NUMBER(args[0]));
    else if(IS_BOOL(args[0]))   len=snprintf(buf,sizeof(buf),"%s",AS_BOOL(args[0])?"true":"false");
    else if(IS_NIL(args[0]))    len=snprintf(buf,sizeof(buf),"nil");
    else return NIL_VAL;
    return OBJ_VAL(copy_str(buf,len));
}

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
    define_native("clock",clock_native);
    define_native("sqrt",sqrt_native);
    define_native("abs",abs_native);
    define_native("floor",floor_native);
    define_native("ceil",ceil_native);
    define_native("str",str_native);
    define_native("len",len_native);
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

static void runtime_error(const char* fmt, ...) {
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

static bool invoke_from_class(ObjStruct* klass, ObjString* name, int argc) {
    Value method;
    if(!table_get(klass->methods,name,&method)){ runtime_error("Undefined property '%s'.",name->chars); return false; }
    return call(AS_CLOSURE(method),argc);
}

static bool invoke(ObjString* name, int argc) {
    Value receiver=peek(argc);
    if(!IS_INSTANCE(receiver)){ runtime_error("Only instances have methods."); return false; }
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
#define READ_CONST()  (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONST())

#define BINARY_OP(vtype,op) do{ \
    if(!IS_NUMBER(peek(0))||!IS_NUMBER(peek(1))){ runtime_error("Operands must be numbers."); return INTERPRET_RUNTIME_ERR; } \
    double b=AS_NUMBER(pop()),a=AS_NUMBER(pop()); push(vtype(a op b)); }while(0)

#define BINARY_BITWISE_OP(op) do{ \
    if(!IS_NUMBER(peek(0))||!IS_NUMBER(peek(1))){ runtime_error("Operands must be numbers."); return INTERPRET_RUNTIME_ERR; } \
    i32 b=(i32)AS_NUMBER(pop()),a=(i32)AS_NUMBER(pop()); push(NUMBER_VAL((double)(a op b))); }while(0)

    for(;;){
#ifdef LUNAR_DEBUG_TRACE_EXECUTION
        printf("          ");
        for(Value* s=lvm.stack;s<lvm.stack_top;s++){ printf("[ "); printValue(*s); printf(" ]"); }
        printf("\n");
        disassemble_instruction(&frame->closure->function->chunk,(int)(frame->ip-frame->closure->function->chunk.code));
#endif
        switch(READ_BYTE()){
            case OP_CONST:   push(READ_CONST()); break;
            case OP_NIL:     push(NIL_VAL); break;
            case OP_TRUE:    push(BOOL_VAL(true)); break;
            case OP_FALSE:   push(BOOL_VAL(false)); break;
            case OP_POP:     pop(); break;
            case OP_GET_LOCAL:  push(frame->slots[READ_BYTE()]); break;
            case OP_SET_LOCAL:  frame->slots[READ_BYTE()]=peek(0); break;
            case OP_DEFINE_GLOBAL: { ObjString* n=READ_STRING(); table_set(&lvm.globals,n,peek(0)); pop(); break; }
            case OP_GET_GLOBAL: {
                ObjString* n=READ_STRING(); Value v;
                if(!table_get(&lvm.globals,n,&v)){ runtime_error("Undefined variable '%s'.",n->chars); return INTERPRET_RUNTIME_ERR; }
                push(v); break;
            }
            case OP_SET_GLOBAL: {
                ObjString* n=READ_STRING();
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
            case OP_CLOSURE: {
                ObjFunction* fn=AS_FUNCTION(READ_CONST());
                ObjClosure* closure=new_closure(fn);
                push(OBJ_VAL(closure));
                for(int i=0;i<closure->upvalue_count;i++){
                    u8 is_local=READ_BYTE(),index=READ_BYTE();
                    closure->upvalues[i]=is_local?capture_upvalue(frame->slots+index):frame->closure->upvalues[index];
                }
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
            case OP_STRUCT: push(OBJ_VAL(new_struct(READ_STRING()))); break;
            case OP_GET_PROPERTY: {
                if(!IS_INSTANCE(peek(0))){ runtime_error("Only instances have properties."); return INTERPRET_RUNTIME_ERR; }
                ObjInstance* inst=AS_INSTANCE(peek(0)); ObjString* name=READ_STRING();
                Value val;
                if(table_get(inst->fields,name,&val)){ pop(); push(val); break; }
                if(!bind_method(inst->klass,name)) return INTERPRET_RUNTIME_ERR;
                break;
            }
            case OP_SET_PROPERTY: {
                if(!IS_INSTANCE(peek(1))){ runtime_error("Only instances have fields."); return INTERPRET_RUNTIME_ERR; }
                ObjInstance* inst=AS_INSTANCE(peek(1)); ObjString* name=READ_STRING();
                table_set(inst->fields,name,peek(0));
                Value val=pop(); pop(); push(val); break;
            }
            case OP_METHOD: define_method(READ_STRING()); break;
            case OP_INVOKE: {
                ObjString* method=READ_STRING(); int argc=READ_BYTE();
                if(!invoke(method,argc)) return INTERPRET_RUNTIME_ERR;
                frame=&lvm.frames[lvm.frame_count-1]; break;
            }
            case OP_INHERIT: {
                if(!IS_STRUCT(peek(1))){ runtime_error("Supertype must be a struct."); return INTERPRET_RUNTIME_ERR; }
                ObjStruct* sup=AS_STRUCT(peek(1)); ObjStruct* sub=AS_STRUCT(peek(0));
                table_add_all(sup->methods,sub->methods); pop(); break;
            }
            case OP_GET_SUPER: {
                ObjString* name=READ_STRING(); ObjStruct* sup=AS_STRUCT(pop());
                if(!bind_method(sup,name)) return INTERPRET_RUNTIME_ERR;
                break;
            }
            case OP_SUPER_INVOKE: {
                ObjString* method=READ_STRING(); int argc=READ_BYTE(); ObjStruct* sup=AS_STRUCT(pop());
                if(!invoke_from_class(sup,method,argc)) return INTERPRET_RUNTIME_ERR;
                frame=&lvm.frames[lvm.frame_count-1]; break;
            }
            default:
                fprintf(stderr,"Unknown opcode: %d\n",*(frame->ip-1));
                return INTERPRET_RUNTIME_ERR;
        }
    }
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONST
#undef READ_STRING
#undef BINARY_OP
#undef BINARY_BITWISE_OP
}
