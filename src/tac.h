#ifndef TAC_H
#define TAC_H

#include <string>
#include <vector>

#include "ast.h"
#include "sema.h"

// Three-address code for a checked v2 program. Temporaries t1, t2, ... and
// labels L1, L2, ... are numbered in the order they are created; conditions
// use jumping code ("if t goto L" / "goto L") with short-circuit && and ||.
// A variable that shadows an earlier one with the same name is renamed x_1, ...
std::vector<std::string> generateTac(const Ast& ast, const SemaResult& sema);

#endif
