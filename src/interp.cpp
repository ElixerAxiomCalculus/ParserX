#include "interp.h"

#include <sstream>

using namespace std;

namespace {

struct Value {
    enum Kind { INT, FLOAT, PTR };
    Kind kind = INT;
    long long i = 0;
    double f = 0;

    double asDouble() const { return kind == FLOAT ? f : (double)i; }
};

struct RuntimeError {
    int line;
    int col;
    string message;
};

Value intValue(long long v) {
    Value x;
    x.kind = Value::INT;
    x.i = v;
    return x;
}

Value floatValue(double v) {
    Value x;
    x.kind = Value::FLOAT;
    x.f = v;
    return x;
}

Value zeroOf(const VType& t) {
    if (t.ptr > 0) {
        Value p;
        p.kind = Value::PTR;
        return p;
    }
    return t.base == "float" ? floatValue(0) : intValue(0);
}

string show(const Value& v) {
    if (v.kind == Value::INT) return to_string(v.i);
    if (v.kind == Value::PTR) return "&" + to_string(v.i);
    ostringstream ss;
    ss << v.f;
    return ss.str();
}

class Machine {
public:
    Machine(const Ast& ast, const SemaResult& sema, long maxSteps, RunResult& r)
        : ast_(ast), sema_(sema), maxSteps_(maxSteps), r_(r),
          mem_(1, Value()), addr_(sema.decls.size(), 0) {}

    void run() { statements(node(ast_.root).kids); }

private:
    const AstNode& node(int i) const { return ast_.nodes[(size_t)i]; }

    [[noreturn]] void fail(int at, const string& msg) {
        RuntimeError e = {node(at).line, node(at).col, msg};
        throw e;
    }

    void tick(int at) {
        if (++r_.steps > maxSteps_)
            fail(at, "step limit of " + to_string(maxSteps_) + " exceeded (possible infinite loop)");
    }

    void statements(const vector<int>& list) {
        for (size_t k = 0; k < list.size(); ++k) statement(list[k]);
    }

    // Statements
    void statement(int s) {
        const AstNode& n = node(s);
        tick(s);
        switch (n.kind) {
            case AstKind::Block: {
                size_t mark = mem_.size();
                statements(n.kids);
                mem_.resize(mark);
                break;
            }
            case AstKind::Decl: declare(s); break;
            case AstKind::Assign: assign(s); break;
            case AstKind::ExprStmt: eval(n.kids[0]); break;
            case AstKind::Print: r_.output.push_back(show(eval(n.kids[0]))); break;
            case AstKind::If:
                if (cond(n.kids[0])) statement(n.kids[1]);
                else if (n.kids.size() > 2) statement(n.kids[2]);
                break;
            case AstKind::While:
                while (cond(n.kids[0])) {
                    statement(n.kids[1]);
                    tick(s);
                }
                break;
            case AstKind::For:
                for (assign(n.kids[0]); cond(n.kids[1]); assign(n.kids[2])) {
                    statement(n.kids[3]);
                    tick(s);
                }
                break;
            default: break;
        }
    }

    void declare(int s) {
        const AstNode& n = node(s);
        const VType& t = n.type;
        int cells = t.isArray() ? t.arraySize : 1;
        addr_[(size_t)n.decl] = (long long)mem_.size();
        VType element = makeType(t.base, t.ptr);
        for (int k = 0; k < cells; ++k) mem_.push_back(zeroOf(element));
        if (!n.kids.empty()) store(addr_[(size_t)n.decl], t, eval(n.kids[0]));
    }

    void assign(int s) {
        const AstNode& n = node(s);
        long long a = lvalue(n.kids[0]);
        const VType& t = node(n.kids[0]).type;
        Value v = eval(n.kids[1]);
        if (n.op != "=") v = arithmetic(s, n.op == "+=" ? "+" : "-", load(n.kids[0], a), v);
        store(a, t, v);
    }

    void store(long long a, const VType& t, Value v) {
        if (t.ptr == 0 && t.base == "float" && v.kind == Value::INT) v = floatValue((double)v.i);
        mem_[(size_t)a] = v;
    }

    Value load(int at, long long a) {
        if (a <= 0 || a >= (long long)mem_.size())
            fail(at, "invalid memory access at address " + to_string(a));
        return mem_[(size_t)a];
    }

    // Address of an l-value
    long long lvalue(int e) {
        const AstNode& n = node(e);
        if (n.kind == AstKind::Var) return addr_[(size_t)n.decl];
        if (n.kind == AstKind::Index) {
            const AstNode& var = node(n.kids[0]);
            const VType& t = sema_.decls[(size_t)var.decl].type;
            Value idx = eval(n.kids[1]);
            if (idx.i < 0 || idx.i >= t.arraySize)
                fail(e, "array index " + to_string(idx.i) + " out of bounds for " + var.name + "[" +
                            to_string(t.arraySize) + "]");
            return addr_[(size_t)var.decl] + idx.i;
        }
        Value p = eval(n.kids[0]);
        if (p.i == 0) fail(e, "null pointer dereference");
        if (p.i < 0 || p.i >= (long long)mem_.size())
            fail(e, "invalid pointer " + to_string(p.i) + " (dangling or out of range)");
        return p.i;
    }

    // Expressions
    Value eval(int e) {
        const AstNode& n = node(e);
        switch (n.kind) {
            case AstKind::Num: return intValue(n.ival);
            case AstKind::Real: return floatValue(n.fval);
            case AstKind::Var:
            case AstKind::Index:
            case AstKind::Deref: return load(e, lvalue(e));
            case AstKind::AddrOf: {
                Value p;
                p.kind = Value::PTR;
                p.i = lvalue(n.kids[0]);
                return p;
            }
            case AstKind::Neg: {
                Value v = eval(n.kids[0]);
                return v.kind == Value::FLOAT ? floatValue(-v.f) : intValue(-v.i);
            }
            case AstKind::Binary: return arithmetic(e, n.op, eval(n.kids[0]), eval(n.kids[1]));
            default: return intValue(0);
        }
    }

    Value arithmetic(int at, const string& op, const Value& a, const Value& b) {
        bool real = a.kind == Value::FLOAT || b.kind == Value::FLOAT;
        if ((op == "/" || op == "%") && (real ? b.asDouble() == 0.0 : b.i == 0))
            fail(at, "division by zero");
        if (real) {
            double x = a.asDouble();
            double y = b.asDouble();
            if (op == "+") return floatValue(x + y);
            if (op == "-") return floatValue(x - y);
            if (op == "*") return floatValue(x * y);
            return floatValue(x / y);
        }
        if (op == "+") return intValue(a.i + b.i);
        if (op == "-") return intValue(a.i - b.i);
        if (op == "*") return intValue(a.i * b.i);
        if (op == "/") return intValue(a.i / b.i);
        return intValue(a.i % b.i);
    }

    bool cond(int c) {
        const AstNode& n = node(c);
        if (n.kind == AstKind::And) return cond(n.kids[0]) && cond(n.kids[1]);
        if (n.kind == AstKind::Or) return cond(n.kids[0]) || cond(n.kids[1]);
        if (n.kind == AstKind::Not) return !cond(n.kids[0]);
        Value a = eval(n.kids[0]);
        Value b = eval(n.kids[1]);
        bool real = a.kind == Value::FLOAT || b.kind == Value::FLOAT;
        double x = real ? a.asDouble() : (double)a.i;
        double y = real ? b.asDouble() : (double)b.i;
        if (!real) {
            long long p = a.i;
            long long q = b.i;
            if (n.op == "<") return p < q;
            if (n.op == ">") return p > q;
            if (n.op == "<=") return p <= q;
            if (n.op == ">=") return p >= q;
            if (n.op == "==") return p == q;
            return p != q;
        }
        if (n.op == "<") return x < y;
        if (n.op == ">") return x > y;
        if (n.op == "<=") return x <= y;
        if (n.op == ">=") return x >= y;
        if (n.op == "==") return x == y;
        return x != y;
    }

    const Ast& ast_;
    const SemaResult& sema_;
    long maxSteps_;
    RunResult& r_;
    vector<Value> mem_;
    vector<long long> addr_;
};

}

RunResult runProgram(const Ast& ast, const SemaResult& sema, long maxSteps) {
    RunResult r;
    Machine m(ast, sema, maxSteps, r);
    try {
        m.run();
    } catch (const RuntimeError& e) {
        r.ok = false;
        r.error.line = e.line;
        r.error.col = e.col;
        r.error.message = e.message;
    }
    return r;
}
