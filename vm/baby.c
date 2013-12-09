// http://journal.stuffwithstuff.com/2013/12/08/babys-first-garbage-collector/
// https://github.com/munificent/mark-sweep

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
  OBJ_INT,
  OBJ_PAIR
} ObjectType;

typedef struct sObject {
  /* The next object in the list of all objects. */
  struct sObject* next;

  unsigned char marked;
  
  ObjectType type;

  union {
    /* OBJ_INT */
    int value;

    /* OBJ_PAIR */
    struct {
      struct sObject* head;
      struct sObject* tail;
    };
  };
} Object;

#define STACK_MAX 256

typedef struct {
  /* The total number of currently allocated objects. */
  int numObjects;

  /* The number of objects required to trigger a GC. */
  int maxObjects;

  /* The first object in the list of all objects. */
  Object* firstObject;

  Object* stack[STACK_MAX];
  int stackSize;
} VM;

VM* newVM() {
  VM* vm = malloc(sizeof(VM));
  vm->numObjects = 0;
  vm->maxObjects = 1;
  vm->firstObject = 0;
  vm->stackSize = 0;
  return vm;
}

void freeVM(VM* vm) {
  Object* object = vm->firstObject;
  while (object) {
    free(object);
    object = object->next;
  }
  free(vm);
}

void push(VM* vm, Object* value) {
  assert(vm->stackSize < STACK_MAX && "Stack overflow!");
  vm->stack[vm->stackSize++] = value;
}

Object* pop(VM* vm) {
  assert(vm->stackSize > 0 && "Stack underflow!");
  return vm->stack[--vm->stackSize];
}

void mark(Object* object) {
  /* If already marked, we're done. Check this first
     to avoid recursing on cycles in the object graph. */
  if (object->marked) return;

  object->marked = 1;

  if (object->type == OBJ_PAIR) {
    mark(object->head);
    mark(object->tail);
  }
}

void markAll(VM* vm)
{
  for (int i = 0; i < vm->stackSize; i++) {
    mark(vm->stack[i]);
  }
}

void sweep(VM* vm)
{
  Object** object = &vm->firstObject;
  while (*object) {
    if (!(*object)->marked) {
      /* This object wasn't reached, so remove it from the list
         and free it. */
      Object* unreached = *object;

      *object = unreached->next;
      free(unreached);
      vm->numObjects--;
    } else {
      /* This object was reached, so unmark it (for the next GC)
         and move on to the next. */
      (*object)->marked = 0;
      object = &(*object)->next;
    }
  }
}

void gc(VM* vm) {
  int numObjects = vm->numObjects;
  
  markAll(vm);
  sweep(vm);

  vm->maxObjects = vm->numObjects * 2;
}


Object* newObject(VM* vm, ObjectType type) {
  if (vm->numObjects == vm->maxObjects) gc(vm);
  
  Object* object = malloc(sizeof(Object));
  object->type = type;
  object->marked = 0;

  /* Insert it into the list of allocated objects. */
  object->next = vm->firstObject;
  vm->firstObject = object;
  
  vm->numObjects++;
  return object;
}

void pushInt(VM* vm, int intValue) {
  Object* object = newObject(vm, OBJ_INT);
  object->value = intValue;
  push(vm, object);
}

Object* pushPair(VM* vm) {
  Object* object = newObject(vm, OBJ_PAIR);
  object->tail = pop(vm);
  object->head = pop(vm);

  push(vm, object);
  return object;
}

void test1() {
  printf("Test 1: Objects on stack are preserved.\n");
  VM* vm = newVM();
  pushInt(vm, 1);
  pushInt(vm, 2);

  gc(vm);
  assert(vm->numObjects == 2 && "Should have preserved objects.");
  freeVM(vm);
}

void test2() {
  printf("Test 2: Unreached objects are collected.\n");
  VM* vm = newVM();
  pushInt(vm, 1);
  pushInt(vm, 2);
  pop(vm);
  pop(vm);

  gc(vm);
  assert(vm->numObjects == 0 && "Should have collected objects.");
  freeVM(vm);
}

void test3() {
  printf("Test 3: Reach nested objects.\n");
  VM* vm = newVM();
  pushInt(vm, 1);
  pushInt(vm, 2);
  pushPair(vm);
  pushInt(vm, 3);
  pushInt(vm, 4);
  pushPair(vm);
  pushPair(vm);

  gc(vm);
  assert(vm->numObjects == 7 && "Should have reached objects.");
  freeVM(vm);
}

void test4() {
  printf("Test 4: Handle cycles.\n");
  VM* vm = newVM();
  pushInt(vm, 1);
  pushInt(vm, 2);
  Object* a = pushPair(vm);
  pushInt(vm, 3);
  pushInt(vm, 4);
  Object* b = pushPair(vm);

  a->tail = b;
  b->tail = a;

  gc(vm);
  assert(vm->numObjects == 4 && "Should have collected objects.");
  freeVM(vm);
}

void perfTest() {
  printf("Performance Test.\n");
  VM* vm = newVM();

  for (int i = 0; i < 1000; i++) {
    for (int j = 0; j < 20; j++) {
      pushInt(vm, i);
    }

    for (int k = 0; k < 20; k++) {
      pop(vm);
    }
  }
  freeVM(vm);
}

int main(int argc, const char * argv[]) {
  test1();
  test2();
  test3();
  test4();

  perfTest();

  return 0;
}
