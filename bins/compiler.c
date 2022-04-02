#include "src/chunk.h"
#include "src/compiler.h"
#include "src/memory.h"
#include "src/value.h"
#include "src/vm.h"
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

int main(int arg, char **argv) {
  initVM();
  char *src  = readFile(argv[1]);
  ObjFunction *func = compile(src);
  return func->arity;
}
