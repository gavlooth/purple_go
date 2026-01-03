# Third-Party Dependencies

This directory contains vendored C libraries used by the OmniLisp compiler.

## Libraries

### uthash
- **URL**: https://github.com/troydhanson/uthash
- **License**: BSD-1-Clause
- **Description**: Header-only hash table implementation for C
- **Files**: `uthash/uthash.h`

### sds (Simple Dynamic Strings)
- **URL**: https://github.com/antirez/sds
- **License**: BSD-2-Clause
- **Description**: Dynamic string library for C
- **Files**: `sds/sds.h`, `sds/sds.c`, `sds/sdsalloc.h`

### linenoise
- **URL**: https://github.com/antirez/linenoise
- **License**: BSD-2-Clause
- **Description**: Line editing library (readline alternative)
- **Files**: `linenoise/linenoise.h`, `linenoise/linenoise.c`

### stb_ds
- **URL**: https://github.com/nothings/stb
- **License**: MIT / Public Domain
- **Description**: Header-only dynamic arrays and hash tables
- **Files**: `stb_ds/stb_ds.h`

## Usage

Include the appropriate header in your source:

```c
#include "uthash/uthash.h"    // Hash tables
#include "sds/sds.h"          // Dynamic strings
#include "linenoise/linenoise.h"  // Line editing
#include "stb_ds/stb_ds.h"    // Dynamic arrays/hash maps
```

For sds and linenoise, compile the .c files along with your project.

## Updating

To update a dependency:
1. Download new version from the source URL
2. Update the LICENSE file if changed
3. Test compilation and functionality
