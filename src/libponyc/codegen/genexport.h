#ifndef CODEGEN_GENEXPORT_H
#define CODEGEN_GENEXPORT_H

#include <platform.h>
#include "codegen.h"

PONY_EXTERN_C_BEGIN

bool genexport(compile_t* c, ast_t* program);

PONY_EXTERN_C_END

#endif
