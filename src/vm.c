#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#ifdef DEBUG_TRACE_EXECUTION
#include "debug.h"
#endif

VM vm;
static Value clockNative(int argCount, Value *args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static Value peek(int distance);
static bool call(ObjClosure *function, int argCount);
static bool callValue(Value callee, int argCount);
static bool invoke(ObjString *name, int argCount);
static bool invokeFromClass(ObjClass *clazz, ObjString *name, int argCount);
static bool bindMethod(ObjClass *clazz, ObjString *name);
static ObjUpvalue *captureUpvalue(Value *local);
static void defineMethod(ObjString *name);
static bool isFalsey(Value val);
static void concatenate();
static void closeUpvalues(Value *last);

static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

static void runtimeError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fputs("\n", stderr);
  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame *frame = &vm.frames[i];
    ObjFunction *function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  CallFrame *frame = &vm.frames[vm.frameCount - 1];
  ObjFunction *function = frame->closure->function;
  size_t instr = frame->ip - function->chunk.code - 1;
  int line = function->chunk.lines[instr];

  fprintf(stderr, "[line %d] in script\n", line);
  resetStack();
}

static void defineNative(const char *name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

static Value appendNative(int argc, Value *args) {
  if (argc != 2) {
    runtimeError("Function 'append' requires 2 arguments, received %d", argc);
    return NIL_VAL;
  }
  if (!IS_LIST(args[0])) {
    runtimeError("Function 'append' requires first argument to be a list.");
    return NIL_VAL;
  }
  ObjList *list = AS_LIST(args[0]);
  appendToList(list, args[1]);
  return NIL_VAL;
}

static Value deleteNative(int argc, Value *args) {
  if (argc != 2) {
    runtimeError("Function 'append' requires 2 arguments, received %d", argc);
    return NIL_VAL;
  }
  if (!IS_LIST(args[0])) {
    runtimeError("Function 'delete' requires first argument to be a list");
    return NIL_VAL;
  }
  if (!IS_NUMBER(args[1])) {
    runtimeError("Function 'delete' requires second argument to be a number");
    return NIL_VAL;
  }

  ObjList *list = AS_LIST(args[0]);
  int idx = AS_NUMBER(args[1]);

  if (deleteFromList(list, idx)) {
    runtimeError("Cannot delete, no element at index %d", idx);
  }
  return NIL_VAL;
}

static Value inputNative(int argc, Value *args) {
  if (argc != 0) {
    runtimeError("Function 'input' takes no arguments.");
    return NIL_VAL;
  }
  ObjString *str = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  str->length = 0;

  char c;
  size_t cnt = 0;
  while (read(1, &c, 1) == 1) {
    if (cnt >= str->length) {
      int oldSize = str->length;
      str->chars = reallocate(str->chars, oldSize, GROW_CAPACITY(oldSize));
    }
    if (c == '\n' || c == '\0') {
      str->chars[cnt] = 0;
      break;
    }
    str->chars[cnt++] = c;
  }
  str->chars[cnt] = 0;
  str->hash = hashString(str->chars, cnt);
  str->length = cnt;
  return OBJ_VAL(str);
}

void initVM() {
  resetStack();
  vm.objects = NULL;
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;
  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;
  initTable(&vm.strings);
  initTable(&vm.globals);
  vm.initString = NULL;
  vm.initString = copyString("init", 4);
  defineNative("clock", clockNative);
  defineNative("append", appendNative);
  defineNative("delete", deleteNative);
  defineNative("input", inputNative);
}

void freeVM() {
  freeTable(&vm.strings);
  freeTable(&vm.globals);
  vm.initString = NULL;
  freeObjects();
}

static InterpretResult run() {
  CallFrame *frame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT()                                                           \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT()                                                        \
  (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op)                                               \
  do {                                                                         \
    if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {                          \
      runtimeError("Operands must be numbers.");                               \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    double b = AS_NUMBER(pop());                                               \
    double a = AS_NUMBER(pop());                                               \
    push(valueType(a op b));                                                   \
  } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(
        &frame->closure->function->chunk,
        (int)(frame->ip - frame->closure->function->chunk.code));
#endif
    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
    case OP_CONSTANT: {
      Value constant = READ_CONSTANT();
      push(constant);
      break;
    }
    case OP_NIL:
      push(NIL_VAL);
      break;
    case OP_TRUE:
      push(BOOL_VAL(true));
      break;
    case OP_FALSE:
      push(BOOL_VAL(false));
      break;
    case OP_POP:
      pop();
      break;
    case OP_GET_LOCAL: {
      uint8_t slot = READ_BYTE();
      push(frame->slots[slot]);
      break;
    }
    case OP_SET_LOCAL: {
      uint8_t slot = READ_BYTE();
      frame->slots[slot] = peek(0);
      break;
    }
    case OP_GET_GLOBAL: {
      ObjString *name = READ_STRING();
      Value value;
      if (!tableGet(&vm.globals, name, &value)) {
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      push(value);
      break;
    }
    case OP_DEFINE_GLOBAL: {
      ObjString *name = READ_STRING();
      tableSet(&vm.globals, name, peek(0));
      pop();
      break;
    }
    case OP_SET_GLOBAL: {
      ObjString *name = READ_STRING();
      if (tableSet(&vm.globals, name, peek(0))) {
        tableDelete(&vm.globals, name);
        runtimeError("Undefined variable '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_GET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      push(*frame->closure->upvalues[slot]->location);
      break;
    }
    case OP_SET_UPVALUE: {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek(0);
      break;
    }
    case OP_GET_PROPERTY: {
      if (!IS_INSTANCE(peek(0))) {
        runtimeError("Only instances have properties.");
        return INTERPRET_RUNTIME_ERROR;
      }
      ObjInstance *inst = AS_INSTANCE(peek(0));
      ObjString *name = READ_STRING();
      Value val;
      if (tableGet(&inst->fields, name, &val)) {
        pop(); // clear the instance
        push(val);
        break;
      }
      if (!bindMethod(inst->klass, name)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SET_PROPERTY: {
      if (!IS_INSTANCE(peek(1))) {
        runtimeError("Only instances have fields.");
        return INTERPRET_RUNTIME_ERROR;
      }
      ObjInstance *inst = AS_INSTANCE(peek(1));
      tableSet(&inst->fields, READ_STRING(), peek(0));
      Value value = pop();
      pop();
      push(value);
      break;
    }
    case OP_GET_SUPER: {
      ObjString *name = READ_STRING();
      ObjClass *superclass = AS_CLASS(pop());
      if (!bindMethod(superclass, name)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_EQUAL: {
      Value b = pop();
      Value a = pop();
      push(BOOL_VAL(valuesEqual(a, b)));
      break;
    }
    case OP_GREATER:
      BINARY_OP(BOOL_VAL, >);
      break;
    case OP_LESS:
      BINARY_OP(BOOL_VAL, <);
      break;
    case OP_ADD: {
      if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
        concatenate();
      } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
        double b = AS_NUMBER(pop());
        double a = AS_NUMBER(pop());
        push(NUMBER_VAL(a + b));
      } else {
        runtimeError("Operands must be two numbers or two strings.");
        return INTERPRET_RUNTIME_ERROR;
      }
      break;
    }
    case OP_SUBTRACT:
      BINARY_OP(NUMBER_VAL, -);
      break;
    case OP_MULTIPLY:
      BINARY_OP(NUMBER_VAL, *);
      break;
    case OP_DIVIDE:
      BINARY_OP(NUMBER_VAL, /);
      break;
    case OP_NOT:
      push(BOOL_VAL(isFalsey(pop())));
      break;
    case OP_NEGATE:
      if (!IS_NUMBER(peek(0))) {
        runtimeError("Operand must be a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      push(NUMBER_VAL(-AS_NUMBER(pop())));
      break;
    case OP_PRINT:
      printValue(pop());
      printf("\n");
      break;
    case OP_JUMP_IF_FALSE: {
      uint16_t offset = READ_SHORT();
      frame->ip += offset * (uint16_t)isFalsey(peek(0));
      break;
    }
    case OP_JUMP: {
      uint16_t offset = READ_SHORT();
      frame->ip += offset;
      break;
    }
    case OP_LOOP: {
      uint16_t offset = READ_SHORT();
      frame->ip -= offset;
      break;
    }
    case OP_CALL: {
      int count = READ_BYTE();
      if (!callValue(peek(count), count)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_SUPER_INVOKE: {
      ObjString *method = READ_STRING();
      int argCount = READ_BYTE();
      ObjClass *superclass = AS_CLASS(pop());
      if (!invokeFromClass(superclass, method, argCount)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLOSURE: {
      ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
      ObjClosure *closure = newClosure(function);
      push(OBJ_VAL(closure));
      for (int i = 0; i < closure->upvalueCount; i++) {
        uint8_t isLocal = READ_BYTE();
        uint8_t index = READ_BYTE();
        if (isLocal) {
          closure->upvalues[i] = captureUpvalue(frame->slots + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      break;
    }
    case OP_INVOKE: {
      ObjString *method = READ_STRING();
      int argc = READ_BYTE();
      if (!invoke(method, argc)) {
        return INTERPRET_RUNTIME_ERROR;
      }
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLOSE_UPVALUE:
      closeUpvalues(vm.stackTop - 1);
      pop();
      break;
    case OP_RETURN: {
      Value result = pop();
      closeUpvalues(frame->slots);
      vm.frameCount--;
      if (vm.frameCount == 0) {
        pop();
        return INTERPRET_OK;
      }
      vm.stackTop = frame->slots;
      push(result);
      frame = &vm.frames[vm.frameCount - 1];
      break;
    }
    case OP_CLASS:
      push(OBJ_VAL(newClass(READ_STRING())));
      break;
    case OP_INHERIT: {
      Value superclass = peek(1);
      if (!IS_CLASS(superclass)) {
        runtimeError("Superclass must be a class.");
        return INTERPRET_RUNTIME_ERROR;
      }
      ObjClass *subclass = AS_CLASS(peek(0));
      tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
      pop();
      break;
    }
    case OP_METHOD:
      defineMethod(READ_STRING());
      break;
    case OP_BUILD_LIST: {
      ObjList *list = newList();
      uint8_t itemCount = READ_BYTE();
      push(OBJ_VAL(list));
      for (int i = itemCount; i > 0; i--) {
        appendToList(list, peek(i));
      }
      pop();
      while (itemCount-- > 0) {
        pop();
      }
      push(OBJ_VAL(list));
      break;
    }
    case OP_INDEX_SUBSCR: {
      Value idx_val = pop();
      Value list_val = pop();
      Value rv;

      if (!IS_LIST(list_val)) {
        runtimeError("Invalid list to index into.");
        return INTERPRET_RUNTIME_ERROR;
      }
      ObjList *list = AS_LIST(list_val);
      if (!IS_NUMBER(idx_val)) {
        runtimeError("List index is not a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      int idx = AS_NUMBER(idx_val);

      if (indexFromList(list, idx, &rv)) {
        runtimeError("List index out of range");
        return INTERPRET_RUNTIME_ERROR;
      }
      push(rv);
      break;
    }
    case OP_STORE_SUBSCR: {
      Value item = pop();
      Value index_val = pop();
      Value list_val = pop();
      if (!IS_LIST(list_val)) {
        runtimeError("Cannot store value in non-list.");
      }
      if (!IS_NUMBER(index_val)) {
        runtimeError("List index is not a number.");
        return INTERPRET_RUNTIME_ERROR;
      }
      ObjList *list = AS_LIST(list_val);
      int index = AS_NUMBER(index_val);

      if (storeToList(list, index, item)) {
        runtimeError("Invalid list index.");
        return INTERPRET_RUNTIME_ERROR;
      }
      push(item);
      break;
    }
    }
  }
#undef READ_CONSTANT
#undef READ_BYTE
#undef READ_SHORT
#undef BINARY_OP
#undef READ_STRING
}

InterpretResult interpret(const char *src) {
  ObjFunction *function = compile(src);
  if (function == NULL) {
    return INTERPRET_COMPILE_ERROR;
  }
  push(OBJ_VAL(function));
  ObjClosure *closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}

static Value peek(int distance) { return vm.stackTop[-1 - distance]; }

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString *b = AS_STRING(peek(0));
  ObjString *a = AS_STRING(peek(1));

  int len = a->length + b->length;
  char *chars = ALLOCATE(char, len + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[len] = 0;

  ObjString *result = takeString(chars, len);
  pop();
  pop();
  push(OBJ_VAL(result));
}

static bool call(ObjClosure *closure, int argCount) {
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.", closure->function->arity,
                 argCount);
    return false;
  }
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }
  CallFrame *frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
    case OBJ_NATIVE: {
      NativeFn native = AS_NATIVE(callee);
      Value result = native(argCount, vm.stackTop - argCount);
      vm.stackTop -= argCount + 1;
      push(result);
      return true;
    }
    case OBJ_CLOSURE:
      return call(AS_CLOSURE(callee), argCount);
    case OBJ_CLASS: {
      ObjClass *klass = AS_CLASS(callee);
      vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
      Value initializer;
      if (tableGet(&klass->methods, vm.initString, &initializer)) {
        return call(AS_CLOSURE(initializer), argCount);
      } else if (argCount != 0) {
        runtimeError("Expected 0 arguments but got %d.", argCount);
        return false;
      }
      return true;
    }
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod *bound = AS_BOUND_METHOD(callee);
      vm.stackTop[-argCount - 1] = bound->receiver;
      return call(bound->method, argCount);
    }
    default:
      break;
    }
  }
  runtimeError("Can only call functions and classes.");
  return false;
}

static bool invokeFromClass(ObjClass *clazz, ObjString *name, int argCount) {
  Value method;
  if (!tableGet(&clazz->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }
  return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString *name, int argCount) {
  Value receiver = peek(argCount);
  if (!IS_INSTANCE(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }
  ObjInstance *inst = AS_INSTANCE(receiver);
  Value value;
  // Handle the case where this could be a closure
  if (tableGet(&inst->fields, name, &value)) {
    vm.stackTop[-argCount - 1] = value;
    return callValue(value, argCount);
  }
  return invokeFromClass(inst->klass, name, argCount);
}

static bool bindMethod(ObjClass *clazz, ObjString *name) {
  Value method;
  if (!tableGet(&clazz->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }
  ObjBoundMethod *bound = newBoundMethod(peek(0), AS_CLOSURE(method));

  pop();
  push(OBJ_VAL(bound));
  return true;
}

static ObjUpvalue *captureUpvalue(Value *local) {
  ObjUpvalue *prev = NULL;
  ObjUpvalue *cur = vm.openUpvalues;
  while (cur != NULL && cur->location > local) {
    prev = cur;
    cur = cur->next;
  }
  if (cur && cur->location == local) {
    return cur;
  }
  ObjUpvalue *createdUpvalue = newUpvalue(local);
  createdUpvalue->next = cur;
  if (!prev) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prev->next = createdUpvalue;
  }
  return createdUpvalue;
}

static void closeUpvalues(Value *last) {
  while (vm.openUpvalues && vm.openUpvalues->location >= last) {
    ObjUpvalue *upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static void defineMethod(ObjString *name) {
  Value method = peek(0);
  ObjClass *clazz = AS_CLASS(peek(1));
  tableSet(&clazz->methods, name, method);
  pop();
}
