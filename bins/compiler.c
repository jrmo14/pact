#include "src/compiler.h"
#include "src/chunk.h"
#include "src/memory.h"
#include "src/value.h"
#include "src/vm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static char *readFile(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "Couldn't open file \"%s\".\n", path);
    exit(74);
  }
  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buf = (char *)malloc(fileSize + 1);
  if (!buf) {
    fprintf(stderr, "Couldn't allocate enough memory to read \"%s\".\n", path);
    exit(74);
  }
  size_t bytesRead = fread(buf, sizeof(char), fileSize, file);
  if (bytesRead != fileSize) {
    fprintf(stderr, "File changed while reading \"%s\".\n", path);
    exit(74);
  }
  buf[bytesRead] = 0;
  fclose(file);
  return buf;
}

typedef struct {
  uint8_t *buf;
  size_t size;
  size_t capacity;
} Bytecode;

Bytecode bytes;

void initBuffer() {
  bytes.capacity = 16;
  bytes.buf = malloc(sizeof(uint8_t) * 16);
  bytes.size = 0;
}

void writeBuffer(const void *ptr, unsigned long size, unsigned long nelem) {
  size_t req = size * nelem;
  if (bytes.size + req >= bytes.capacity) {
    size_t new_cap = bytes.capacity * 2;
    if (new_cap < bytes.size + req) {
      new_cap = bytes.capacity + req;
    }
    bytes.buf = realloc(bytes.buf, new_cap);
    bytes.capacity = new_cap;
  }
  memcpy(bytes.buf + bytes.size, ptr, req);
  bytes.size += req;
}

void freeBuffer() { free(bytes.buf); }

// Should only need to save functions and strings, the vm can rebuild everything
// else from that

void writeString(ObjString *str) {
  int tmp = OBJ_STRING;
  writeBuffer(&tmp, sizeof(int), 1);
  writeBuffer(&str->length, sizeof(int), 1);
  writeBuffer(str->chars, sizeof(char), str->length);
}

void writeFunction(ObjFunction *f);

void writeValue(Value v) {
  int tmp = v.type;
  writeBuffer(&tmp, sizeof(int), 1);
  switch (v.type) {
  case VAL_BOOL:
  case VAL_CHARACTER:
  case VAL_NIL: {
    uint8_t tmp = (uint8_t)v.as.character;
    writeBuffer(&tmp, sizeof(uint8_t), 1);
    break;
  }
  case VAL_INTEGER:
  case VAL_FLOAT: {
    uint64_t tmp = (uint64_t)v.as.integer;
    writeBuffer(&tmp, sizeof(uint64_t), 1);
    break;
  }
  case VAL_OBJ:
    if (IS_STRING(v)) {
      writeString(AS_STRING(v));
    } else if (IS_FUNCTION(v)) {
      writeFunction(AS_FUNCTION(v));
    } else {
      printf("SHIT\n");
    };
    break;
  }
}

void writeFunction(ObjFunction *f) {
  int tmp = OBJ_FUNCTION;
  writeBuffer(&tmp, sizeof(int), 1);
  writeBuffer(&f->arity,sizeof(int),1);
  writeBuffer(&f->upvalueCount,sizeof(int),1);
  writeBuffer(&f->chunk.count, sizeof(int), 1);
  writeBuffer(f->chunk.code, sizeof(uint8_t), f->chunk.count);
  writeBuffer(f->chunk.lines, sizeof(int), f->chunk.count);
  writeBuffer(&f->chunk.constants.count, sizeof(int), 1);
  for (int i = 0; i < f->chunk.constants.count; i++) {
    Value v = f->chunk.constants.values[i];
    writeValue(v);
  }
}

int main(int arg, char **argv) {
  initVM();

  char *src = readFile(argv[1]);
  ObjFunction *func = compile(src);
  initBuffer();
  writeFunction(func);
  freeBuffer();
  free(src);
  freeVM();
}
