#ifndef OMNILISP_H
#define OMNILISP_H

#include "../types.h"

// Initialize the runtime environment
void omni_init(void);

// Read a string and return an AST (Value*)
// Returns T_ERROR on failure.
Value* omni_read(const char* input);

// Print a value to stdout
void omni_print(Value* v);

#endif // OMNILISP_H
