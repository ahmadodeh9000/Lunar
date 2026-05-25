

#include "vm.h"
#include "debug.h"
#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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

    compiler(src);
    return INTERPRET_OK;

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


InterpretResult run() {

#define READ_BYTE()     (*lvm.ip++)
#define READ_CONST()    (lvm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op) \
    do { \
        double b = pop(); \
        double a = pop(); \
        push(a op b); \
    } while (false) 


#define BINARY_BITWISE_OP(op) \
    do { \
        i32 b = (i32) pop(); \
        i32 a = (i32) pop(); \
        push(a op b); \
    } while (false) 


/* MIGHT BE BUGGY */
#define FAST_POW() ({                          \
    double _e = pop();                         \
    double _b = pop();                          \
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

            case OP_NEGATE: push(-pop());           break;
            case OP_ADD:    BINARY_OP(+);           break;
            case OP_SUB:    BINARY_OP(-);           break;
            case OP_MUL:    BINARY_OP(*);           break;
            case OP_DIV:    BINARY_OP(/);           break;
            case OP_MOD:    BINARY_BITWISE_OP(%);   break;    // i used bitwise cuz it converts double to i32
            case OP_POW:    push(FAST_POW())  ;     break; 

            case OP_BITWISE_AND:        BINARY_BITWISE_OP(&); break;
            case OP_BITWISE_OR:         BINARY_BITWISE_OP(|); break;
            case OP_BITWISE_XOR:        BINARY_BITWISE_OP(^); break;
            case OP_BITWISE_NOT:        push(~((i32)pop())); break;
            case OP_BITWISE_RIGHTSHIFT: BINARY_BITWISE_OP(>>); break;
            case OP_BITWISE_LEFTSHIFT:  BINARY_BITWISE_OP(<<); break;

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
