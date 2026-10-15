#include "tac.h"

using namespace std;

namespace {

class TacGen {
public:
    TacGen(const Ast& ast, const SemaResult& sema) : ast_(ast), sema_(sema), temps_(0), labels_(0) {}

    vector<string> run() {
        statements(node(ast_.root).kids);
        return code_;
    }

private:
    const AstNode& node(int i) const { return ast_.nodes[(size_t)i]; }
    string temp() { return "t" + to_string(++temps_); }
    string label() { return "L" + to_string(++labels_); }
    void emit(const string& s) { code_.push_back("    " + s); }
    void mark(const string& l) { code_.push_back(l + ":"); }
    string name(int var) const { return sema_.decls[(size_t)node(var).decl].tacName; }

    void statements(const vector<int>& list) {
        for (size_t k = 0; k < list.size(); ++k) statement(list[k]);
    }

    // Statements
    void statement(int s) {
        const AstNode& n = node(s);
        switch (n.kind) {
            case AstKind::Block: statements(n.kids); break;
            case AstKind::Decl: {
                const Declaration& d = sema_.decls[(size_t)n.decl];
                VType element = makeType(d.type.base, d.type.ptr);
                emit("decl " + element.str() + " " + d.tacName +
                     (d.type.isArray() ? "[" + to_string(d.type.arraySize) + "]" : ""));
                if (!n.kids.empty()) emit(d.tacName + " = " + expr(n.kids[0]));
                break;
            }
            case AstKind::Assign: assign(s); break;
            case AstKind::ExprStmt: expr(n.kids[0]); break;
            case AstKind::Print: emit("print " + expr(n.kids[0])); break;
            case AstKind::If: {
                string yes = label();
                string no = n.kids.size() > 2 ? label() : "";
                string end = label();
                cond(n.kids[0], yes, no.empty() ? end : no);
                mark(yes);
                statement(n.kids[1]);
                if (!no.empty()) {
                    emit("goto " + end);
                    mark(no);
                    statement(n.kids[2]);
                }
                mark(end);
                break;
            }
            case AstKind::While:
            case AstKind::For: {
                bool isFor = n.kind == AstKind::For;
                if (isFor) assign(n.kids[0]);
                string begin = label();
                string body = label();
                string end = label();
                mark(begin);
                cond(n.kids[isFor ? 1 : 0], body, end);
                mark(body);
                statement(n.kids[isFor ? 3 : 1]);
                if (isFor) assign(n.kids[2]);
                emit("goto " + begin);
                mark(end);
                break;
            }
            default: break;
        }
    }

    void assign(int s) {
        const AstNode& n = node(s);
        string value = expr(n.kids[1]);
        string target = place(n.kids[0]);
        if (n.op != "=") {
            string current = temp();
            emit(current + " = " + target);
            string result = temp();
            emit(result + " = " + current + " " + n.op.substr(0, 1) + " " + value);
            value = result;
        }
        emit(target + " = " + value);
    }

    // Where an l-value lives: x, a[i] or *p
    string place(int e) {
        const AstNode& n = node(e);
        if (n.kind == AstKind::Var) return name(e);
        if (n.kind == AstKind::Index) return name(n.kids[0]) + "[" + expr(n.kids[1]) + "]";
        return "*" + expr(n.kids[0]);
    }

    // Expressions; returns a name, literal or temporary
    string expr(int e) {
        const AstNode& n = node(e);
        switch (n.kind) {
            case AstKind::Num:
            case AstKind::Real: return n.text;
            case AstKind::Var: return name(e);
            case AstKind::Index:
            case AstKind::Deref: {
                string from = place(e);
                string t = temp();
                emit(t + " = " + from);
                return t;
            }
            case AstKind::AddrOf: {
                const AstNode& inner = node(n.kids[0]);
                if (inner.kind == AstKind::Deref) return expr(inner.kids[0]);
                string from = place(n.kids[0]);
                string t = temp();
                emit(t + " = &" + from);
                return t;
            }
            case AstKind::Neg: {
                string v = expr(n.kids[0]);
                string t = temp();
                emit(t + " = - " + v);
                return t;
            }
            case AstKind::Binary: {
                string a = expr(n.kids[0]);
                string b = expr(n.kids[1]);
                string t = temp();
                emit(t + " = " + a + " " + n.op + " " + b);
                return t;
            }
            default: return "?";
        }
    }

    // Jumping code for conditions
    void cond(int c, const string& yes, const string& no) {
        const AstNode& n = node(c);
        if (n.kind == AstKind::And || n.kind == AstKind::Or) {
            string mid = label();
            if (n.kind == AstKind::And) cond(n.kids[0], mid, no);
            else cond(n.kids[0], yes, mid);
            mark(mid);
            cond(n.kids[1], yes, no);
        } else if (n.kind == AstKind::Not) {
            cond(n.kids[0], no, yes);
        } else {
            string a = expr(n.kids[0]);
            string b = expr(n.kids[1]);
            string t = temp();
            emit(t + " = " + a + " " + n.op + " " + b);
            emit("if " + t + " goto " + yes);
            emit("goto " + no);
        }
    }

    const Ast& ast_;
    const SemaResult& sema_;
    int temps_;
    int labels_;
    vector<string> code_;
};

}

vector<string> generateTac(const Ast& ast, const SemaResult& sema) {
    TacGen gen(ast, sema);
    return gen.run();
}
