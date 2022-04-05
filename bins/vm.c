#include "src/vm.h"

ObjFunction *load_program() {
  return NULL;
}

int main() {
  initVM();
  ObjFunction *function = load_program();
  ObjClosure *closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  callClosure(closure, 0);
  run();
  freeVM();
}
