

#include "vm.h"
#include "debug.h"
#include "compiler.h"


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>

LunarVM lvm;


/* helper functions */
static InterpretResult run();
static void reset_stack();




void init_lunar_vm() {
    reset_stack();
}

static void reset_stack() {
    lvm.stack_top = lvm.stack;
}


void free_lunar_vm() {


}

InterpretResult interpret(const char* src) {
    Chunk chunk;
    init_chunk(&chunk);

    if (!compile(src,&chunk)) {
        free_chunk(&chunk);
        return INTERPRET_COMPPILE_ERR;
    }

    lvm.chunk   = &chunk;
    lvm.ip      = lvm.chunk->code;

    InterpretResult result = run();
    free_chunk(&chunk);
    return result;

}

void push(Value val) {

    if (lvm.stack_top >= lvm.stack + STACK_MAX) {
        fprintf(stderr,"STACKOVERFLOW !\n");
        exit(-42);
    }

    *lvm.stack_top = val;
    lvm.stack_top++;
}

Value pop() {
    lvm.stack_top--;
    return *lvm.stack_top;
}

static Value peek(int distance) {
  return lvm.stack_top[-1 - distance];
}

static void runtime_error(const char* format, ...) {
    va_list args;
    va_start(args,format);
    vfprintf(stderr,format,args);
    va_end(args);

    fputs("\n",stderr);

    size_t instruct = lvm.ip - lvm.chunk->code - 1;
    i32 line = lvm.chunk->lines[instruct];
    fprintf(stderr,"[line %d] in script\n",line);

    reset_stack();
}


static bool is_falsey(Value val) {
    return IS_NIL(val) || (IS_BOOL(val) && !AS_BOOL(val));
}

InterpretResult run() {

#define READ_BYTE()     (*lvm.ip++)
#define READ_CONST()    (lvm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtime_error("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

#define BINARY_BITWISE_OP(op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtime_error("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERR; \
      } \
      i32 b = (i32) AS_NUMBER(pop()); \
      i32 a = (i32) AS_NUMBER(pop()); \
      push(NUMBER_VAL(a op b)); \
    } while (false)




/* MIGHT BE BUGGY */
#define FAST_POW() ({                          \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtime_error("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERR; \
    } \
    double _e = AS_NUMBER(pop());                         \
    double _b = AS_NUMBER(pop());                          \
    double _r;                                          \
    if      (_e == 0.0)  _r = 1.0;                     \
    else if (_b == 0.0)  _r = 0.0;                     \
    else if (_b == 1.0)  _r = 1.0;                     \
    else if (_e == 1.0)  _r = _b;                      \
    else if (_e == 2.0)  _r = _b * _b;                 \
    else if (_e == 0.5)  _r = sqrt(_b);                \
    else if (_e == (i32)_e) {                           \
        i32 _ei = (i32)_e;                              \
        int _neg = _ei < 0;                             \
        if (_neg) _ei = -_ei;                           \
        _r = 1.0;                                       \
        double _base = _b;                              \
        while (_ei) {                                   \
            if (_ei & 1) _r *= _base;                   \
            _base *= _base;                             \
            _ei >>= 1;                                  \
        }                                               \
        if (_neg) _r = 1.0 / _r;                       \
    }                                                   \
    else if (_b < 0.0)  _r = NAN;                      \
    else                _r = exp(_e * log(_b));         \
    _r;                                                 \
})



/* main logic down here */

    while (true) {


#ifdef DEBUG_TRACE_EXECUTION

        for (Value* slot = lvm.stack; slot < lvm.stack_top; slot++) {
            printf("[ ");
            printValue(*slot);
            printf(" ]");
        }
        printf("\n");


        disassemble_instruction(lvm.chunk,(i32)(lvm.ip - lvm.chunk->code));
#endif

        u8 instr;

        switch(instr = READ_BYTE()) {
            case OP_RET: {
                printValue(pop());
                printf("\n");
                return INTERPRET_OK;
            }

            case OP_CONST: {
                Value constant = READ_CONST();
                push(constant);
                break;
            }

            case OP_NEGATE: {
                if (!IS_NUMBER(peek(0))) {
                    runtime_error("Operand must be a number !");
                    return INTERPRET_RUNTIME_ERR;
                }

                push(NUMBER_VAL(-AS_NUMBER(pop())));
                break;
            }

            case OP_ADD:    BINARY_OP(NUMBER_VAL,+);           break;
            case OP_SUB:    BINARY_OP(NUMBER_VAL,-);           break;
            case OP_MUL:    BINARY_OP(NUMBER_VAL,*);           break;
            case OP_DIV:    BINARY_OP(NUMBER_VAL,/);           break;
            case OP_MOD:    BINARY_BITWISE_OP(%);   break;    // i used bitwise cuz it converts double to i32
            case OP_POW:    push(NUMBER_VAL(FAST_POW()))  ;     break; 

            case OP_BITWISE_AND:        BINARY_BITWISE_OP(&); break;
            case OP_BITWISE_OR:         BINARY_BITWISE_OP(|); break;
            case OP_BITWISE_XOR:        BINARY_BITWISE_OP(^); break;
            case OP_BITWISE_NOT:        push(NUMBER_VAL( ~ (i32)AS_NUMBER(pop()))); break;
            case OP_BITWISE_RIGHTSHIFT: BINARY_BITWISE_OP(>>); break;
            case OP_BITWISE_LEFTSHIFT:  BINARY_BITWISE_OP(<<); break;

            case OP_TRUE:       push(BOOL_VAL(true)); break;
            case OP_FALSE:      push(BOOL_VAL(false)); break;
            case OP_NIL:        push(NIL_VAL); break;

            case OP_NOT:        push(BOOL_VAL(is_falsey(pop()))); break;

            case OP_EQU: {
                Value b = pop();
                Value a = pop();

                push(BOOL_VAL(values_equ(a,b)));
                break;
            }

            case OP_GREATER:    BINARY_OP(BOOL_VAL,>); break;
            case OP_LESS:       BINARY_OP(BOOL_VAL,<); break;

            default:
                fprintf(stderr, "UNKNOWN OPCODE: %d\n", instr);
                return INTERPRET_RUNTIME_ERR;


        }

    }

#undef READ_BYTE
#undef READ_CONST
#undef BINARY_OP
#undef BINARY_BITWISE_OP
#undef FAST_POW

}
