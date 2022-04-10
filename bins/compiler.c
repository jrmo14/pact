#include "src/compiler.h"
#include "src/chunk.h"
#include "src/memory.h"
#include "src/value.h"
#include "src/vm.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

FILE *output_file;

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

void write_string(ObjString *str) {
  int tmp = OBJ_STRING;
  fwrite(&tmp, sizeof(int), 1, output_file);
  fwrite(&str->length, sizeof(int), 1, output_file);
  fwrite(str->chars, sizeof(char), str->length, output_file);
}

void write_function(ObjFunction *f);

void write_value(Value v) {
  int tmp = v.type;
  fwrite(&tmp, sizeof(int), 1, output_file);
  switch (v.type) {
  case VAL_BOOL: {
    bool tmp = (bool)v.as.boolean;
    fwrite(&tmp, sizeof(bool), 1, output_file);
  }
  case VAL_CHARACTER: {
    uint8_t tmp = (uint8_t)v.as.character;
    fwrite(&tmp, sizeof(uint8_t), 1, output_file);
    break;
  }
  case VAL_NIL: {
    uint64_t tmp = (uint64_t)v.as.integer;
    fwrite(&tmp, sizeof(uint64_t), 1, output_file);
    break;
  }
  case VAL_INTEGER: {
    uint64_t tmp = (uint64_t)v.as.integer;
    fwrite(&tmp, sizeof(uint64_t), 1, output_file);
    break;
  }
  case VAL_FLOAT: {
    double tmp = (uint64_t)v.as.floating;
    fwrite(&tmp, sizeof(double), 1, output_file);
    break;
  }
  case VAL_OBJ:
    if (IS_STRING(v)) {
      write_string(AS_STRING(v));
    } else if (IS_FUNCTION(v)) {
      write_function(AS_FUNCTION(v));
    } else {
      fprintf(stderr, "SHIT\n");
    };
    break;
  }
}

void write_function(ObjFunction *f) {
  int tmp = OBJ_FUNCTION;
  fwrite(&tmp, sizeof(int), 1, output_file);
  fwrite(&f->arity, sizeof(int), 1, output_file);
  fwrite(&f->upvalueCount, sizeof(int), 1, output_file);
  fwrite(&f->chunk.count, sizeof(int), 1, output_file);
  fwrite(f->chunk.code, sizeof(uint8_t), f->chunk.count, output_file);
  fwrite(f->chunk.lines, sizeof(int), f->chunk.count, output_file);
  fwrite(&f->chunk.constants.count, sizeof(int), 1, output_file);
  for (int i = 0; i < f->chunk.constants.count; i++) {
    Value v = f->chunk.constants.values[i];
    write_value(v);
  }
  if (f->name) {
    uint8_t marker = 1;
    fwrite(&marker, sizeof(uint8_t), 1, output_file);
    write_string(f->name);
  } else {
    uint8_t marker = 0;
    fwrite(&marker, sizeof(uint8_t), 1, output_file);
  }
}

int main(int arg, char **argv) {
  initVM();

  char *src = readFile(argv[1]);
  ObjFunction *func = compile(src);
  initBuffer();
  output_file = fopen("out.pactb", "wb");
  int tmp = VAL_OBJ;
  fwrite(&tmp, sizeof(int), 1, output_file);
  write_function(func);
  fclose(output_file);
  freeBuffer();
  free(src);
  freeVM();
}
