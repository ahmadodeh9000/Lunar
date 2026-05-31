#include "memory.h"
#include "object.h"
#include "vm.h"
#include <stdlib.h>
#include <stdio.h>

void* reallocate(void* pointer, size_t old_size, size_t new_size) {
    if (new_size == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, new_size);

    if (NULL == result) {
        fprintf(stderr, "OUT OF MEMORY !\n");
        exit(-1);
    }

    return result;
}

static void freeObject(Obj* object) {
  switch (object->type) {
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
  }
}

void freeObjects() {
  Obj* object = lvm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
}