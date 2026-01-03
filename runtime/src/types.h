/*
 * types.h - Compatibility header for modular memory management
 *
 * This provides forward declarations needed by the memory modules.
 * The actual Value type is defined in the omnilisp runtime.
 */

#ifndef PURPLE_TYPES_H
#define PURPLE_TYPES_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration - Value is used by some analysis functions */
typedef struct Value Value;

/* For modules that don't need full Value definition, this is enough */

#endif /* PURPLE_TYPES_H */
