
#ifndef lunar_common_h
#define lunar_common_h


/* DEBUG CONSTANTS */
#define LUNAR_DEBUG_TRACE_EXECUTION
#define LUNAR_DEBUG_PRINT_CODE

/* LUNAR CONSTANTS */
#define LUNAR_NAME          "Lunar"
#define LUNAR_VERSION       "0.0.1"


/* LUNAR ERROR CODES */
#define LUNAR_NOT_ENOUGH_MEMORY                 10
#define LUNAR_READ_FILE_FAILURE         90
#define LUNAR_RUNTIME_ERROR_CODE        100
#define LUNAR_COMPILETIME_ERROR_CODE    110



/* PRIMARY HEADERS */
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* DATATYPES DEFS */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;




#endif