#ifndef SEMA_H
#define SEMA_H

#include <ostream>
#include <string>
#include <vector>

#include "ast.h"

struct Diagnostic {
    int line = 0;
    int col = 0;
    std::string message;
};

struct Declaration {
    std::string name;
    VType type;
    int line = 0;
    int col = 0;
    std::string tacName;
};

struct SemaResult {
    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> warnings;
    std::vector<Declaration> decls;
    int scopes = 0;
};

// Semantic analysis of a v2 AST: block scopes (shadowing allowed in inner
// blocks), undeclared names, redeclaration in one scope, type checks.
// Coercion rule: an int value may be stored in a float (widening); a float
// may not be stored in an int. Every expression node gets its type and every
// Var / Decl node its declaration index.
SemaResult analyzeProgram(Ast& ast);

void printDiagnostics(std::ostream& os, const SemaResult& r);

#endif
