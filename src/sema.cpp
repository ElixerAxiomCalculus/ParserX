#include "sema.h"

#include <map>

using namespace std;

namespace {

const VType kError = makeType("error");
const VType kBool = makeType("bool");

class Checker {
public:
    Checker(Ast& ast, SemaResult& r) : ast_(ast), r_(r) {}

    void program(int root) {
        pushScope();
        statements(ast_.nodes[(size_t)root].kids);
        popScope();
    }

private:
    AstNode& node(int i) { return ast_.nodes[(size_t)i]; }

    void error(int at, const string& msg) {
        Diagnostic d;
        d.line = node(at).line;
        d.col = node(at).col;
        d.message = msg;
        r_.errors.push_back(d);
    }

    void warning(int at, const string& msg) {
        Diagnostic d;
        d.line = node(at).line;
        d.col = node(at).col;
        d.message = msg;
        r_.warnings.push_back(d);
    }

    void pushScope() {
        scopes_.push_back(map<string, int>());
        ++r_.scopes;
    }
    void popScope() { scopes_.pop_back(); }

    int lookup(const string& name) const {
        for (size_t k = scopes_.size(); k > 0; --k) {
            map<string, int>::const_iterator it = scopes_[k - 1].find(name);
            if (it != scopes_[k - 1].end()) return it->second;
        }
        return -1;
    }

    void statements(const vector<int>& list) {
        for (size_t k = 0; k < list.size(); ++k) statement(list[k]);
    }

    // Statements
    void statement(int s) {
        AstNode& n = node(s);
        switch (n.kind) {
            case AstKind::Block:
                pushScope();
                statements(n.kids);
                popScope();
                break;
            case AstKind::Decl: declaration(s); break;
            case AstKind::Assign: assignment(s); break;
            case AstKind::ExprStmt: expr(n.kids[0]); break;
            case AstKind::Print: {
                VType t = expr(n.kids[0]);
                if (!t.isError() && !t.isNumeric())
                    error(s, "print needs an int or float, got " + t.str());
                break;
            }
            case AstKind::If:
                condition(n.kids[0]);
                for (size_t k = 1; k < n.kids.size(); ++k) statement(n.kids[k]);
                break;
            case AstKind::While:
                condition(n.kids[0]);
                statement(n.kids[1]);
                break;
            case AstKind::For:
                assignment(n.kids[0]);
                condition(n.kids[1]);
                assignment(n.kids[2]);
                statement(n.kids[3]);
                break;
            default: break;
        }
    }

    void declaration(int s) {
        AstNode& n = node(s);
        if (n.type.isArray() && n.type.arraySize <= 0)
            error(s, "array '" + n.name + "' must have a positive size");
        if (!n.kids.empty()) {
            VType init = expr(n.kids[0]);
            checkAssignable(s, n.type, init, "initialise");
        }
        map<string, int>& scope = scopes_.back();
        map<string, int>::const_iterator old = scope.find(n.name);
        if (old != scope.end()) {
            error(s, "redeclaration of '" + n.name + "' in the same scope (first declared on line " +
                         to_string(r_.decls[(size_t)old->second].line) + ")");
            return;
        }
        Declaration d;
        d.name = n.name;
        d.type = n.type;
        d.line = n.line;
        d.col = n.col;
        int sameName = 0;
        for (size_t k = 0; k < r_.decls.size(); ++k)
            if (r_.decls[k].name == n.name) ++sameName;
        d.tacName = sameName == 0 ? n.name : n.name + "_" + to_string(sameName);
        n.decl = (int)r_.decls.size();
        scope[n.name] = n.decl;
        r_.decls.push_back(d);
    }

    void assignment(int s) {
        AstNode& n = node(s);
        VType left = expr(n.kids[0]);
        VType right = expr(n.kids[1]);
        if (left.isError() || right.isError()) return;
        if (left.isArray()) {
            error(s, "cannot assign to an array (" + left.str() + ")");
            return;
        }
        if (n.op != "=") {
            if (!left.isNumeric() || !right.isNumeric()) {
                error(s, "'" + n.op + "' needs numeric operands, got " + left.str() + " and " +
                             right.str());
                return;
            }
            VType result = (left.base == "float" || right.base == "float") ? makeType("float")
                                                                             : makeType("int");
            checkAssignable(s, left, result, "assign");
            return;
        }
        checkAssignable(s, left, right, "assign");
    }

    void checkAssignable(int at, const VType& dst, const VType& src, const string& verb) {
        if (dst.isError() || src.isError() || dst == src) return;
        if (dst == makeType("float") && src == makeType("int")) return;
        if (verb == "assign")
            error(at, "type mismatch: cannot assign " + src.str() + " to " + dst.str());
        else
            error(at, "type mismatch: cannot initialise " + dst.str() + " with " + src.str());
    }

    void condition(int c) {
        AstNode& n = node(c);
        if (n.kind == AstKind::And || n.kind == AstKind::Or) {
            condition(n.kids[0]);
            condition(n.kids[1]);
        } else if (n.kind == AstKind::Not) {
            condition(n.kids[0]);
        } else {
            expr(c);
        }
        n.type = kBool;
    }

    // Expressions; returns the type and stores it in the node
    VType expr(int e) {
        VType t = exprType(e);
        node(e).type = t;
        return t;
    }

    VType exprType(int e) {
        AstNode& n = node(e);
        switch (n.kind) {
            case AstKind::Num: return makeType("int");
            case AstKind::Real: return makeType("float");
            case AstKind::Var: {
                n.decl = lookup(n.name);
                if (n.decl < 0) {
                    error(e, "undeclared variable '" + n.name + "'");
                    return kError;
                }
                return r_.decls[(size_t)n.decl].type;
            }
            case AstKind::Index: {
                VType base = expr(n.kids[0]);
                VType index = expr(n.kids[1]);
                if (!index.isError() && index != makeType("int"))
                    error(n.kids[1], "array index must be an int, got " + index.str());
                if (base.isError()) return kError;
                if (!base.isArray()) {
                    error(e, "indexing a non-array '" + node(n.kids[0]).name + "' of type " + base.str());
                    return kError;
                }
                return makeType(base.base, base.ptr);
            }
            case AstKind::Deref: {
                VType inner = expr(n.kids[0]);
                if (inner.isError()) return kError;
                if (!inner.isPointer()) {
                    error(e, "dereferencing a non-pointer of type " + inner.str());
                    return kError;
                }
                return makeType(inner.base, inner.ptr - 1);
            }
            case AstKind::AddrOf: {
                VType inner = expr(n.kids[0]);
                if (inner.isError()) return kError;
                if (inner.isArray()) {
                    error(e, "cannot take the address of an array (" + inner.str() + ")");
                    return kError;
                }
                return makeType(inner.base, inner.ptr + 1);
            }
            case AstKind::Neg: {
                VType inner = expr(n.kids[0]);
                if (inner.isError()) return kError;
                if (!inner.isNumeric()) {
                    error(e, "unary '-' needs a number, got " + inner.str());
                    return kError;
                }
                return inner;
            }
            case AstKind::Binary: return binary(e);
            case AstKind::Rel: return relation(e);
            default: return kError;
        }
    }

    VType binary(int e) {
        AstNode& n = node(e);
        VType a = expr(n.kids[0]);
        VType b = expr(n.kids[1]);
        if (a.isError() || b.isError()) return kError;
        if (!a.isNumeric() || !b.isNumeric()) {
            error(e, "'" + n.op + "' needs numeric operands, got " + a.str() + " and " + b.str());
            return kError;
        }
        if (n.op == "%" && (a.base != "int" || b.base != "int")) {
            error(e, "'%' needs int operands, got " + a.str() + " and " + b.str());
            return kError;
        }
        const AstNode& right = node(n.kids[1]);
        if ((n.op == "/" || n.op == "%") &&
            ((right.kind == AstKind::Num && right.ival == 0) ||
             (right.kind == AstKind::Real && right.fval == 0.0)))
            warning(e, "division by the constant zero");
        return (a.base == "float" || b.base == "float") ? makeType("float") : makeType("int");
    }

    VType relation(int e) {
        AstNode& n = node(e);
        VType a = expr(n.kids[0]);
        VType b = expr(n.kids[1]);
        if (a.isError() || b.isError()) return kBool;
        bool numbers = a.isNumeric() && b.isNumeric();
        bool pointers = a.isPointer() && a == b && (n.op == "==" || n.op == "!=");
        if (!numbers && !pointers)
            error(e, "cannot compare " + a.str() + " and " + b.str() + " with '" + n.op + "'");
        return kBool;
    }

    Ast& ast_;
    SemaResult& r_;
    vector<map<string, int> > scopes_;
};

}

SemaResult analyzeProgram(Ast& ast) {
    SemaResult r;
    if (ast.root < 0) return r;
    Checker c(ast, r);
    c.program(ast.root);
    return r;
}

void printDiagnostics(ostream& os, const SemaResult& r) {
    const size_t kShow = 100;
    for (size_t k = 0; k < r.errors.size() && k < kShow; ++k)
        os << "SEMANTIC ERROR line " << r.errors[k].line << " col " << r.errors[k].col << ": "
           << r.errors[k].message << "\n";
    if (r.errors.size() > kShow) os << "... " << (r.errors.size() - kShow) << " more error(s)\n";
    for (size_t k = 0; k < r.warnings.size() && k < kShow; ++k)
        os << "SEMANTIC WARNING line " << r.warnings[k].line << " col " << r.warnings[k].col << ": "
           << r.warnings[k].message << "\n";
    if (r.warnings.size() > kShow) os << "... " << (r.warnings.size() - kShow) << " more warning(s)\n";
}
