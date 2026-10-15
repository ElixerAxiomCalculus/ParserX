#ifndef INTERP_H
#define INTERP_H

#include <string>
#include <vector>

#include "ast.h"
#include "sema.h"

struct RunResult {
    bool ok = true;
    std::vector<std::string> output;
    Diagnostic error;
    long steps = 0;
};

// Tree-walking interpreter for a checked v2 program. Memory is a vector of
// cells (address 0 is the null pointer); a block releases its variables when
// it ends. Runtime errors: division by zero, array index out of bounds,
// null or invalid pointer, and the step limit (one step per statement).
RunResult runProgram(const Ast& ast, const SemaResult& sema, long maxSteps);

#endif
