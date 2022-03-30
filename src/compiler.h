#ifndef clox_compiler_h
#define clox_compiler_h

#include <stdio.h>
#include <sys/types.h>

#include "scanner.h"
#include "vm.h"
#include "common.h"
#include "object.h"

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . ()
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef enum {
  TYPE_FUNCTION,
  TYPE_METHOD,
  TYPE_INITIALIZER,
  TYPE_SCRIPT
} FunctionType;

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef struct Compiler{
  struct Compiler *enclosing;
  ObjFunction *function;
  FunctionType type;
  Local locals[UINT8_COUNT];
  int localCount;
  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;
} Compiler;

typedef struct ClassCompiler {
  struct ClassCompiler *enclosing;
} ClassCompiler;

ObjFunction *compile(const char *src);
void markCompilerRoots();

#endif
