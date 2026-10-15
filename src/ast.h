#ifndef AST_H
#define AST_H

#include <ostream>
#include <string>
#include <vector>

#include "grammar.h"
#include "lexer.h"
#include "parser.h"

// Types of the v2 language: int or float, with pointer levels and an
// optional array size ("int*[3]" is an array of 3 pointers to int).
struct VType {
    std::string base;
    int ptr = 0;
    int arraySize = -1;

    bool isError() const { return base == "error"; }
    bool isArray() const { return arraySize >= 0; }
    bool isPointer() const { return !isArray() && ptr > 0; }
    bool isNumeric() const { return !isArray() && ptr == 0 && (base == "int" || base == "float"); }
    bool operator==(const VType& o) const;
    bool operator!=(const VType& o) const { return !(*this == o); }
    std::string str() const;
};

VType makeType(const std::string& base, int ptr = 0, int arraySize = -1);

enum class AstKind {
    Program, Block, List, Decl, Assign, ExprStmt, If, While, For, Print, TypeSpec,
    Num, Real, Var, Index, Deref, AddrOf, Neg, Binary, Rel, And, Or, Not
};

struct AstNode {
    AstKind kind = AstKind::Program;
    std::string op;
    std::string name;
    std::string text;
    long long ival = 0;
    double fval = 0;
    int line = 0;
    int col = 0;
    VType type;
    std::vector<int> kids;
    int decl = -1;
};

struct Ast {
    std::vector<AstNode> nodes;
    int root = -1;
};

// Builds the AST of a v2 program from its concrete parse tree. Works bottom
// up over the tree arena (children are stored before parents), so deep
// programs need no recursion here.
bool buildAst(const Grammar& g, const ParseTree& tree, const std::vector<Token>& tokens, Ast& ast,
              std::string& error);

std::string astKindName(AstKind k);

#endif
