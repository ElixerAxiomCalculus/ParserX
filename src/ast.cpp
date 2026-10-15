#include "ast.h"

#include <cstdlib>
#include <map>
#include <sstream>

using namespace std;

bool VType::operator==(const VType& o) const {
    return base == o.base && ptr == o.ptr && arraySize == o.arraySize;
}

string VType::str() const {
    string s = base + string((size_t)ptr, '*');
    if (arraySize >= 0) s += "[" + to_string(arraySize) + "]";
    return s;
}

VType makeType(const string& base, int ptr, int arraySize) {
    VType t;
    t.base = base;
    t.ptr = ptr;
    t.arraySize = arraySize;
    return t;
}

string astKindName(AstKind k) {
    static const char* const kNames[] = {
        "Program", "Block", "List", "Decl", "Assign", "ExprStmt", "If", "While", "For", "Print",
        "TypeSpec", "Num", "Real", "Var", "Index", "Deref", "AddrOf", "Neg", "Binary", "Rel",
        "And", "Or", "Not"};
    return kNames[(int)k];
}

namespace {

// What each v2 production builds
enum Build {
    PASS_FIRST, PASS_SECOND, PROGRAM, LIST_NEW, LIST_ADD, STMT_EXPR, IF, IF_ELSE, WHILE, FOR,
    PRINT, BLOCK, BLOCK_EMPTY, DECL, DECL_INIT, DECL_ARRAY, TYPE_BASE, TYPE_PTR, ASSIGN,
    VAR, INDEX, DEREF, OR, AND, NOT, REL, BINARY, NEG, ADDR, NUM, REAL
};

const map<string, Build>& buildTable() {
    static map<string, Build> t;
    if (!t.empty()) return t;
    t["P -> SL"] = PROGRAM;
    t["SL -> SL S"] = LIST_ADD;
    t["SL -> S"] = LIST_NEW;
    t["S -> D ;"] = PASS_FIRST;
    t["S -> A ;"] = PASS_FIRST;
    t["S -> E ;"] = STMT_EXPR;
    t["S -> if ( B ) S"] = IF;
    t["S -> if ( B ) S else S"] = IF_ELSE;
    t["S -> while ( B ) S"] = WHILE;
    t["S -> for ( A ; B ; A ) S"] = FOR;
    t["S -> print ( E ) ;"] = PRINT;
    t["S -> { SL }"] = BLOCK;
    t["S -> { }"] = BLOCK_EMPTY;
    t["D -> TY id"] = DECL;
    t["D -> TY id = E"] = DECL_INIT;
    t["D -> TY id [ num ]"] = DECL_ARRAY;
    t["TY -> int"] = TYPE_BASE;
    t["TY -> float"] = TYPE_BASE;
    t["TY -> TY *"] = TYPE_PTR;
    t["A -> L = E"] = ASSIGN;
    t["A -> L += E"] = ASSIGN;
    t["A -> L -= E"] = ASSIGN;
    t["L -> id"] = VAR;
    t["L -> id [ E ]"] = INDEX;
    t["L -> * U"] = DEREF;
    t["B -> B || BA"] = OR;
    t["B -> BA"] = PASS_FIRST;
    t["BA -> BA && BN"] = AND;
    t["BA -> BN"] = PASS_FIRST;
    t["BN -> ! BN"] = NOT;
    t["BN -> E relop E"] = REL;
    t["BN -> ( B )"] = PASS_SECOND;
    t["E -> E + T"] = BINARY;
    t["E -> E - T"] = BINARY;
    t["E -> T"] = PASS_FIRST;
    t["T -> T * U"] = BINARY;
    t["T -> T / U"] = BINARY;
    t["T -> T % U"] = BINARY;
    t["T -> U"] = PASS_FIRST;
    t["U -> - U"] = NEG;
    t["U -> & L"] = ADDR;
    t["U -> F"] = PASS_FIRST;
    t["F -> L"] = PASS_FIRST;
    t["F -> num"] = NUM;
    t["F -> real"] = REAL;
    t["F -> ( E )"] = PASS_SECOND;
    return t;
}

class Builder {
public:
    Builder(const ParseTree& tree, const vector<Token>& tokens, Ast& ast)
        : tree_(tree), tokens_(tokens), ast_(ast), value_(tree.nodes.size(), -1) {}

    int value(int parseNode) const { return value_[(size_t)parseNode]; }
    void setValue(int parseNode, int astNode) { value_[(size_t)parseNode] = astNode; }

    const Token& token(int parseNode) const {
        return tokens_[(size_t)tree_.nodes[(size_t)parseNode].token];
    }

    int add(AstKind kind, int parseTokenNode) {
        AstNode n;
        n.kind = kind;
        if (parseTokenNode >= 0 && tree_.nodes[(size_t)parseTokenNode].token >= 0) {
            const Token& t = token(parseTokenNode);
            n.line = t.line;
            n.col = t.column;
        }
        ast_.nodes.push_back(n);
        return (int)ast_.nodes.size() - 1;
    }

    AstNode& node(int i) { return ast_.nodes[(size_t)i]; }

    // Position of the first token below an AST node's operands
    void copyPos(int to, int from) {
        if (from < 0) return;
        node(to).line = ast_.nodes[(size_t)from].line;
        node(to).col = ast_.nodes[(size_t)from].col;
    }

private:
    const ParseTree& tree_;
    const vector<Token>& tokens_;
    Ast& ast_;
    vector<int> value_;
};

}

bool buildAst(const Grammar& g, const ParseTree& tree, const vector<Token>& tokens, Ast& ast,
              string& error) {
    ast = Ast();
    if (tree.root < 0) {
        error = "no parse tree";
        return false;
    }
    const map<string, Build>& table = buildTable();
    Builder b(tree, tokens, ast);
    for (size_t k = 0; k <= (size_t)tree.root; ++k) {
        const TreeNode& pn = tree.nodes[k];
        if (pn.prod < 0) continue;
        const Production& p = g.production(pn.prod);
        map<string, Build>::const_iterator it = table.find(p.toString());
        if (it == table.end()) {
            error = "production " + p.toString() + " is not part of the v2 semantics";
            return false;
        }
        const vector<int>& kid = pn.kids;
        int n = -1;
        switch (it->second) {
            case PASS_FIRST: n = b.value(kid[0]); break;
            case PASS_SECOND: n = b.value(kid[1]); break;
            case PROGRAM:
                n = b.value(kid[0]);
                b.node(n).kind = AstKind::Program;
                b.node(n).line = 1;
                b.node(n).col = 1;
                break;
            case LIST_NEW:
                n = b.add(AstKind::List, -1);
                b.node(n).kids.push_back(b.value(kid[0]));
                break;
            case LIST_ADD:
                n = b.value(kid[0]);
                b.node(n).kids.push_back(b.value(kid[1]));
                break;
            case STMT_EXPR:
                n = b.add(AstKind::ExprStmt, -1);
                b.node(n).kids.push_back(b.value(kid[0]));
                b.copyPos(n, b.value(kid[0]));
                break;
            case IF:
            case IF_ELSE:
                n = b.add(AstKind::If, kid[0]);
                b.node(n).kids.push_back(b.value(kid[2]));
                b.node(n).kids.push_back(b.value(kid[4]));
                if (it->second == IF_ELSE) b.node(n).kids.push_back(b.value(kid[6]));
                break;
            case WHILE:
                n = b.add(AstKind::While, kid[0]);
                b.node(n).kids.push_back(b.value(kid[2]));
                b.node(n).kids.push_back(b.value(kid[4]));
                break;
            case FOR:
                n = b.add(AstKind::For, kid[0]);
                b.node(n).kids.push_back(b.value(kid[2]));
                b.node(n).kids.push_back(b.value(kid[4]));
                b.node(n).kids.push_back(b.value(kid[6]));
                b.node(n).kids.push_back(b.value(kid[8]));
                break;
            case PRINT:
                n = b.add(AstKind::Print, kid[0]);
                b.node(n).kids.push_back(b.value(kid[2]));
                break;
            case BLOCK:
                n = b.add(AstKind::Block, kid[0]);
                b.node(n).kids = ast.nodes[(size_t)b.value(kid[1])].kids;
                break;
            case BLOCK_EMPTY: n = b.add(AstKind::Block, kid[0]); break;
            case DECL:
            case DECL_INIT:
            case DECL_ARRAY:
                n = b.add(AstKind::Decl, kid[1]);
                b.node(n).name = b.token(kid[1]).lexeme;
                b.node(n).type = ast.nodes[(size_t)b.value(kid[0])].type;
                if (it->second == DECL_INIT) b.node(n).kids.push_back(b.value(kid[3]));
                if (it->second == DECL_ARRAY)
                    b.node(n).type.arraySize = (int)strtol(b.token(kid[3]).lexeme.c_str(), 0, 10);
                break;
            case TYPE_BASE:
                n = b.add(AstKind::TypeSpec, kid[0]);
                b.node(n).type = makeType(b.token(kid[0]).lexeme);
                break;
            case TYPE_PTR:
                n = b.value(kid[0]);
                b.node(n).type.ptr += 1;
                break;
            case ASSIGN:
                n = b.add(AstKind::Assign, kid[1]);
                b.node(n).op = b.token(kid[1]).lexeme;
                b.node(n).kids.push_back(b.value(kid[0]));
                b.node(n).kids.push_back(b.value(kid[2]));
                break;
            case VAR:
                n = b.add(AstKind::Var, kid[0]);
                b.node(n).name = b.token(kid[0]).lexeme;
                break;
            case INDEX: {
                int var = b.add(AstKind::Var, kid[0]);
                b.node(var).name = b.token(kid[0]).lexeme;
                n = b.add(AstKind::Index, kid[1]);
                b.node(n).kids.push_back(var);
                b.node(n).kids.push_back(b.value(kid[2]));
                break;
            }
            case DEREF:
            case NEG:
            case ADDR:
            case NOT:
                n = b.add(it->second == DEREF ? AstKind::Deref
                          : it->second == NEG ? AstKind::Neg
                          : it->second == ADDR ? AstKind::AddrOf : AstKind::Not, kid[0]);
                b.node(n).kids.push_back(b.value(kid[1]));
                break;
            case OR:
            case AND:
            case REL:
            case BINARY:
                n = b.add(it->second == OR ? AstKind::Or
                          : it->second == AND ? AstKind::And
                          : it->second == REL ? AstKind::Rel : AstKind::Binary, kid[1]);
                b.node(n).op = b.token(kid[1]).lexeme;
                b.node(n).kids.push_back(b.value(kid[0]));
                b.node(n).kids.push_back(b.value(kid[2]));
                break;
            case NUM:
                n = b.add(AstKind::Num, kid[0]);
                b.node(n).text = b.token(kid[0]).lexeme;
                b.node(n).ival = strtoll(b.node(n).text.c_str(), 0, 10);
                break;
            case REAL:
                n = b.add(AstKind::Real, kid[0]);
                b.node(n).text = b.token(kid[0]).lexeme;
                b.node(n).fval = strtod(b.node(n).text.c_str(), 0);
                break;
        }
        b.setValue((int)k, n);
    }
    ast.root = b.value(tree.root);
    return ast.root >= 0;
}
