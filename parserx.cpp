#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "src/comparison.h"
#include "src/compress.h"
#include "src/context.h"
#include "src/ast.h"
#include "src/bench.h"
#include "src/classify.h"
#include "src/explain.h"
#include "src/glr.h"
#include "src/visual.h"
#include "src/precision.h"
#include "src/randgram.h"
#include "src/interp.h"
#include "src/sema.h"
#include "src/tac.h"
#include "src/fuzz.h"
#include "src/first_follow.h"
#include "src/grammar.h"
#include "src/lalr_dp.h"
#include "src/lexer.h"
#include "src/lexgen.h"
#include "src/parser.h"
#include "src/table.h"
#include "src/util.h"

using namespace std;

namespace {

const size_t kMaxTracedTokens = 2000;
const size_t kMaxPrintedTokens = 200;

// Exit codes
const int kExitAccepted = 0;
const int kExitRejected = 1;
const int kExitGrammar = 2;
const int kExitLexical = 3;
const int kExitUsage = 4;
const int kExitSemantic = 5;
const int kExitCheckFailed = 6;

struct Options {
    string grammarPath = "grammar/language.txt";
    string inputPath;
    string code;
    bool haveCode = false;
    string parser = "all";
    string trace = "brief";
    bool tokens = false;
    bool states = false;
    bool tables = false;
    bool dumpDfa = false;
    string lalrMethod = "merge";
    bool verifyLalr = false;
    bool verifyPgm = false;
    bool verifyCompress = false;
    bool recover = false;
    int maxErrors = 50;
    bool compressed = false;
    int fuzz = -1;
    int seed = 20261015;
    int maxDepth = 12;
    string csvPath;
    bool explain = false;
    bool precision = false;
    bool classify = false;
    int randomGrammars = -1;
    bool run = false;
    bool tac = false;
    int maxSteps = 1000000;
    string dotMode;
    string dotPath;
    string htmlPath;
    string csvDir;
    bool bench = false;
    bool glr = false;
    int repeat = 5;

    bool batch() const {
        return fuzz >= 0 || classify || verifyLalr || verifyPgm || verifyCompress;
    }
};

// One parser of the comparison: its table, automaton and build time
struct ParserEntry {
    const ParsingTable* table;
    const LRMachine* machine;
    long buildUs;
};

void usage(ostream& os) {
    os << "Usage: parserx [--grammar FILE] [--input FILE | --code TEXT] [input-file] [options]\n"
          "Output:      --parser slr|lalr|clr|pgm|all   --trace brief|full|none\n"
          "             --tokens  --states  --tables  --dump-dfa\n"
          "Tables:      --lalr-method merge|dp  --compressed\n"
          "Checks:      --verify-lalr  --verify-pgm  --verify-compress  --verify\n"
          "Analysis:    --explain  --precision  --classify  --glr\n"
          "Recovery:    --recover  --max-errors N\n"
          "Fuzzing:     --fuzz N  --seed S  --max-depth D  --csv FILE\n"
          "Study:       --random-grammars N  --seed S  --csv FILE\n"
          "Semantics:   --run  --tac  --max-steps N   (grammars with %semantics v2)\n"
          "Exports:     --dot automaton|lalr-merge FILE  --html-trace FILE  --csv-tables DIR\n"
          "Benchmark:   --bench  --repeat R\n"
          "Without --input or --code the program is typed in; end it with a line END.\n"
          "Exit codes: 0 accepted, 1 rejected, 2 grammar or lexer-spec error, 3 lexical error,\n"
          "            4 bad options or input file, 5 semantic or runtime error, 6 a check failed.\n";
}

bool oneOf(const string& v, const char* const* allowed) {
    for (size_t k = 0; allowed[k]; ++k)
        if (v == allowed[k]) return true;
    return false;
}

bool parseArgs(int argc, char** argv, Options& o, string& err) {
    static const char* const kParsers[] = {"slr", "lalr", "clr", "pgm", "all", 0};
    static const char* const kTraces[] = {"brief", "full", "none", 0};
    static const char* const kMethods[] = {"merge", "dp", 0};
    for (int k = 1; k < argc; ++k) {
        string a = argv[k];
        bool hasValue = k + 1 < argc;
        if (a == "--help" || a == "-h") {
            usage(cout);
            exit(0);
        } else if (a == "--grammar" && hasValue) {
            o.grammarPath = argv[++k];
        } else if (a == "--input" && hasValue) {
            o.inputPath = argv[++k];
        } else if (a == "--code" && hasValue) {
            o.code = argv[++k];
            o.haveCode = true;
        } else if (a == "--parser" && hasValue) {
            o.parser = argv[++k];
            if (!oneOf(o.parser, kParsers)) {
                err = "unknown parser '" + o.parser + "' (use slr, lalr, clr, pgm or all)";
                return false;
            }
        } else if (a == "--trace" && hasValue) {
            o.trace = argv[++k];
            if (!oneOf(o.trace, kTraces)) {
                err = "unknown trace mode '" + o.trace + "' (use brief, full or none)";
                return false;
            }
        } else if (a == "--lalr-method" && hasValue) {
            o.lalrMethod = argv[++k];
            if (!oneOf(o.lalrMethod, kMethods)) {
                err = "unknown LALR method '" + o.lalrMethod + "' (use merge or dp)";
                return false;
            }
        } else if (a == "--max-errors" && hasValue) {
            if (!utilParseInt(argv[++k], o.maxErrors) || o.maxErrors < 1) {
                err = "--max-errors needs a positive number";
                return false;
            }
        } else if (a == "--fuzz" && hasValue) {
            if (!utilParseInt(argv[++k], o.fuzz) || o.fuzz < 0) {
                err = "--fuzz needs a number of sentences (0 or more)";
                return false;
            }
        } else if (a == "--seed" && hasValue) {
            if (!utilParseInt(argv[++k], o.seed) || o.seed < 0) {
                err = "--seed needs a non-negative number";
                return false;
            }
        } else if (a == "--max-depth" && hasValue) {
            if (!utilParseInt(argv[++k], o.maxDepth) || o.maxDepth < 1) {
                err = "--max-depth needs a positive number";
                return false;
            }
        } else if (a == "--csv" && hasValue) {
            o.csvPath = argv[++k];
        } else if (a == "--dot" && k + 2 < argc) {
            o.dotMode = argv[++k];
            o.dotPath = argv[++k];
            if (o.dotMode != "automaton" && o.dotMode != "lalr-merge") {
                err = "unknown --dot view '" + o.dotMode + "' (use automaton or lalr-merge)";
                return false;
            }
        } else if (a == "--html-trace" && hasValue) {
            o.htmlPath = argv[++k];
        } else if (a == "--csv-tables" && hasValue) {
            o.csvDir = argv[++k];
        } else if (a == "--glr") {
            o.glr = true;
        } else if (a == "--bench") {
            o.bench = true;
        } else if (a == "--repeat" && hasValue) {
            if (!utilParseInt(argv[++k], o.repeat) || o.repeat < 1) {
                err = "--repeat needs a positive number";
                return false;
            }
        } else if (a == "--run") {
            o.run = true;
        } else if (a == "--tac") {
            o.tac = true;
        } else if (a == "--max-steps" && hasValue) {
            if (!utilParseInt(argv[++k], o.maxSteps) || o.maxSteps < 1) {
                err = "--max-steps needs a positive number";
                return false;
            }
        } else if (a == "--classify") {
            o.classify = true;
        } else if (a == "--random-grammars" && hasValue) {
            if (!utilParseInt(argv[++k], o.randomGrammars) || o.randomGrammars < 0) {
                err = "--random-grammars needs a number of grammars (0 or more)";
                return false;
            }
        } else if (a == "--precision") {
            o.precision = true;
        } else if (a == "--explain") {
            o.explain = true;
        } else if (a == "--recover") {
            o.recover = true;
        } else if (a == "--compressed") {
            o.compressed = true;
        } else if (a == "--tokens") {
            o.tokens = true;
        } else if (a == "--states") {
            o.states = true;
        } else if (a == "--tables") {
            o.tables = true;
        } else if (a == "--dump-dfa") {
            o.dumpDfa = true;
        } else if (a == "--verify-lalr") {
            o.verifyLalr = true;
        } else if (a == "--verify-pgm") {
            o.verifyPgm = true;
        } else if (a == "--verify-compress") {
            o.verifyCompress = true;
        } else if (a == "--verify") {
            o.verifyLalr = true;
            o.verifyPgm = true;
            o.verifyCompress = true;
        } else if (!a.empty() && a[0] != '-' && o.inputPath.empty()) {
            o.inputPath = a;
        } else {
            err = "unknown or incomplete option '" + a + "'";
            return false;
        }
    }
    return true;
}

void step(const string& title) {
    cout << "\n" << utilRepeat('=', 70) << "\n" << title << "\n"
         << utilRepeat('=', 70) << "\n";
}

void resultLine(const string& text) {
    cout << "\n" << utilRepeat('-', 70) << "\n" << text << "\n";
}

int retypeForGrammar(vector<Token>& tokens, const Grammar& g) {
    int changed = 0;
    for (size_t k = 0; k < tokens.size(); ++k) {
        Token& t = tokens[k];
        if (!g.isTerminal(t.type) && g.isTerminal(t.lexeme)) {
            t.type = t.lexeme;
            ++changed;
        }
    }
    return changed;
}

bool selected(const Options& o, const string& tableName) {
    if (o.parser == "all") return true;
    string lower;
    for (size_t k = 0; k < tableName.size(); ++k)
        lower += (char)tolower(static_cast<unsigned char>(tableName[k]));
    return lower == o.parser;
}

string tokenDesc(const string& type, const string& lexeme) {
    if (type == "$") return "end of input";
    if (type == lexeme) return "'" + lexeme + "'";
    return "'" + lexeme + "' (" + type + ")";
}

// Read the program
bool readSource(const Options& opt, string& source, string& sourceName) {
    string err;
    if (opt.haveCode) {
        source = opt.code;
        sourceName = "(command line)";
    } else if (!opt.inputPath.empty()) {
        if (!utilReadFile(opt.inputPath, source, err)) {
            cerr << "Error: " << err << "\n";
            return false;
        }
        sourceName = opt.inputPath;
    } else {
        cerr << "Enter the program. Type END on a new line to finish:\n";
        string line;
        while (getline(cin, line)) {
            size_t eof = line.find_first_of("\x1A\x04");
            if (eof != string::npos) {
                source += line.substr(0, eof) + "\n";
                break;
            }
            if (utilTrim(line) == "END") break;
            source += line + "\n";
        }
        sourceName = "(standard input)";
    }
    return true;
}

// Step 1: the built-in v1 lexer, or one generated from the grammar's %lexer spec
bool runLexer(const Options& opt, const Grammar& g, bool grammarOk,
              const string& source, LexResult& lex) {
    step("STEP 1  LEXER");
    if (grammarOk && !g.lexerSpec().empty()) {
        GeneratedLexer gen;
        string err;
        if (!gen.loadSpec(g.lexerSpec(), err)) {
            cout << "Lexer spec error: " << err << "\n";
            resultLine("RESULT LEXER-SPEC-ERROR " + err);
            return false;
        }
        cout << "Lexer: generated from " << g.lexerSpec() << " (" << gen.rules().size()
             << " rules, NFA " << gen.nfaStates() << " states, DFA " << gen.dfaStates()
             << " states, minimised " << gen.minDfaStates() << " states)\n";
        if (opt.dumpDfa) gen.dumpDfa(cout);
        lex = gen.tokenize(source);
    } else {
        lex = Lexer::tokenize(source);
    }
    cout << "Tokens: " << lex.tokens.size() << " (including the end marker $)\n";
    if (opt.tokens || lex.tokens.size() <= kMaxPrintedTokens) printTokens(cout, lex);
    else cout << "(token table not printed for large input; use --tokens)\n";
    return true;
}

void printLexicalErrors(const Options& opt, const LexResult& lex) {
    if (!opt.tokens && lex.tokens.size() > kMaxPrintedTokens) printLexErrors(cout, lex);
    cout << "\nParsing skipped because of lexical errors.\n";
    cout << "\n" << utilRepeat('-', 70) << "\n";
    for (size_t k = 0; k < lex.errors.size(); ++k) {
        cout << "RESULT LEXICAL-ERROR symbol='" << lex.errors[k].character
             << "' line=" << lex.errors[k].line
             << " col=" << lex.errors[k].column << "\n";
    }
}

void printFirstFollow(const Grammar& g, const FirstFollow& ff) {
    step("STEP 3  FIRST SETS");
    vector<string> nullables;
    for (size_t k = 0; k < g.nonterminals().size(); ++k)
        if (ff.nullable(g.nonterminals()[k])) nullables.push_back(g.nonterminals()[k]);
    cout << "Nullable non-terminals: "
         << (nullables.empty() ? "none" : utilJoin(nullables, ", ")) << "\n";
    ff.printFirst(cout);
    step("STEP 4  FOLLOW SETS");
    ff.printFollow(cout);
}

// Steps 5-7
void printTables(const Options& opt, const Grammar& g, const BuiltTables& bt,
                 const vector<ParserEntry>& parsers) {
    step("STEPS 5-7  SLR (LR(0)), CLR (LR(1)), LALR AND PGM TABLES");
    for (size_t k = 0; k < parsers.size(); ++k) {
        cout << left << setw(5) << parsers[k].table->name << ": "
             << setw(4) << parsers[k].table->stateItems.size() << " states, "
             << parsers[k].table->actionEntries() << " ACTION + "
             << parsers[k].table->gotoEntries() << " GOTO entries, built in "
             << parsers[k].buildUs << " us\n";
    }
    int mergedGroups = 0;
    for (size_t s = 0; s < bt.lalrMergeMachine.states.size(); ++s)
        if (bt.lalrMergeMachine.states[s].note.compare(0, 6, "merged") == 0) ++mergedGroups;
    cout << "LALR merged " << bt.clrMachine.states.size() << " CLR states into "
         << bt.lalrMergeMachine.states.size() << " states (" << mergedGroups
         << " states came from two or more CLR states with the same core)\n";
    cout << "LALR table method: " << bt.lalrMethod << ". Build times: LALR-merge "
         << bt.lalrMergeMicroseconds << " us (includes the CLR automaton), LALR-dp "
         << bt.lalrDpMicroseconds << " us (LR(0) automaton + DeRemer-Pennello relations)\n";
    cout << "DeRemer-Pennello: " << bt.dpStats.ntTransitions << " non-terminal transitions, "
         << bt.dpStats.readsEdges << " reads edges, " << bt.dpStats.includesEdges
         << " includes edges, " << bt.dpStats.lookbackEdges << " lookback edges, "
         << bt.dpStats.nontrivialSccs << " non-trivial SCCs\n";
    for (size_t k = 0; k < parsers.size(); ++k) {
        if (!selected(opt, parsers[k].table->name)) continue;
        if (opt.states) {
            cout << "\n";
            printMachineStates(cout, g, *parsers[k].machine);
        }
        if (opt.tables) {
            cout << "\n" << parsers[k].table->name << " ACTION table\n";
            printActionTable(cout, g, *parsers[k].table, false);
            cout << "\n" << parsers[k].table->name << " GOTO table\n";
            printGotoTable(cout, g, *parsers[k].table, false);
        }
    }
    if (!opt.states && !opt.tables)
        cout << "(use --states to print item sets and --tables for ACTION/GOTO)\n";
}

// Step 8
void printConflictCheck(const vector<ParserEntry>& parsers) {
    step("STEP 8  CONFLICT CHECK");
    for (size_t k = 0; k < parsers.size(); ++k) {
        const ParsingTable& t = *parsers[k].table;
        printConflicts(cout, t);
        if (t.kind != MachineKind::LALR) continue;
        set<int> noted;
        for (size_t c = 0; c < t.conflicts.size(); ++c) {
            int st = t.conflicts[c].state;
            if (!noted.insert(st).second) continue;
            if (t.stateNotes[(size_t)st].compare(0, 6, "merged") != 0) continue;
            cout << "      LALR state " << st << " is " << t.stateNotes[(size_t)st] << "\n";
        }
    }
}

// Steps 9-10; returns false if a parser rejected
bool parseAll(const Options& opt, const Grammar& g, const BuiltTables& bt,
              const vector<ParserEntry>& parsers, const LexResult& lex, const string& source,
              vector<string>& treeLines) {
    ParseOptions po;
    po.recordTrace = opt.trace != "none" && lex.tokens.size() <= kMaxTracedTokens;
    po.buildTree = po.recordTrace;
    po.recover = opt.recover;
    po.maxErrors = opt.maxErrors;
    bool allAccepted = true;
    for (size_t k = 0; k < parsers.size(); ++k) {
        const ParsingTable& t = *parsers[k].table;
        if (!selected(opt, t.name)) continue;
        step("STEPS 9-10  PARSE WITH " + t.name + (opt.compressed ? " (compressed table)" : ""));
        Parser p(g, t, opt.compressed ? &packedFor(bt, t.name) : 0);
        ParseResult r = p.parse(lex.tokens, po);
        if (po.recordTrace) printTrace(cout, g, r, opt.trace == "full");
        else if (opt.trace != "none")
            cout << "(trace not recorded: input has more than " << kMaxTracedTokens
                 << " tokens)\n";
        if (r.accepted) {
            cout << "\n" << t.name << ": ACCEPTED in " << r.stepCount << " steps\n";
            if (po.buildTree) {
                string tree = treeToCompactString(r.tree, lex.tokens);
                cout << "Structure: " << tree << "\n";
                treeLines.push_back("TREE " + t.name + ": " + tree);
            }
        } else {
            allAccepted = false;
            cout << "\n" << t.name << ": REJECTED at step " << r.errorStep
                 << " on " << tokenDesc(r.error.unexpectedType, r.error.unexpectedLexeme)
                 << " after " << r.reductionsBeforeError
                 << " reduce(s) on that token\n";
            r.error.context = describeErrorContext(g, t, r.error);
            printParseError(cout, source, r);
            if (opt.recover) printRecoveredErrors(cout, r);
        }
    }
    return allAccepted;
}

// PGM invariants: LALR <= PGM <= CLR states, and no unresolved PGM conflict
// when canonical LR(1) has none
bool verifyPgm(const BuiltTables& bt) {
    size_t lalr = bt.lalrMerge.stateItems.size();
    size_t pgm = bt.pgm.stateItems.size();
    size_t clr = bt.clr.stateItems.size();
    bool sizeOk = lalr <= pgm && pgm <= clr;
    cout << "VERIFY PGM states LALR=" << lalr << " <= PGM=" << pgm << " <= CLR=" << clr
         << (sizeOk ? " OK" : " FAILED") << "\n";
    int clrUnresolved = bt.clr.srConflicts() + bt.clr.rrConflicts();
    int pgmUnresolved = bt.pgm.srConflicts() + bt.pgm.rrConflicts();
    bool conflictOk = clrUnresolved > 0 || pgmUnresolved == 0;
    cout << "VERIFY PGM conflicts CLR unresolved=" << clrUnresolved << " PGM unresolved="
         << pgmUnresolved;
    if (clrUnresolved > 0) cout << " OK (not applicable: CLR has conflicts)\n";
    else cout << (conflictOk ? " OK" : " FAILED") << "\n";
    return sizeOk && conflictOk;
}

// --dot, --html-trace and --csv-tables; false when a file cannot be written
bool writeExports(const Options& opt, const Grammar& g, const BuiltTables& bt,
                  const vector<ParserEntry>& parsers, const LexResult& lex, const string& source) {
    if (opt.dotMode.empty() && opt.htmlPath.empty() && opt.csvDir.empty()) return true;
    step("STEP 7b  EXPORTS");
    vector<const ParsingTable*> chosen;
    for (size_t k = 0; k < parsers.size(); ++k)
        if (selected(opt, parsers[k].table->name)) chosen.push_back(parsers[k].table);
    vector<string> written;
    string err;
    bool ok = true;
    if (opt.dotMode == "lalr-merge") {
        ok = writeLalrMergeDot(opt.dotPath, g, bt, err);
        if (ok) written.push_back(opt.dotPath);
    } else if (opt.dotMode == "automaton") {
        for (size_t k = 0; k < parsers.size() && ok; ++k) {
            if (!selected(opt, parsers[k].table->name)) continue;
            string lower;
            for (size_t c = 0; c < parsers[k].table->name.size(); ++c)
                lower += (char)tolower(static_cast<unsigned char>(parsers[k].table->name[c]));
            string path = opt.parser == "all" ? withSuffix(opt.dotPath, "_" + lower) : opt.dotPath;
            ok = writeAutomatonDot(path, g, *parsers[k].machine, *parsers[k].table, err);
            if (ok) written.push_back(path);
        }
    }
    if (ok && !opt.htmlPath.empty()) {
        if (lex.tokens.size() > kMaxTracedTokens) {
            cout << "HTML trace skipped: input has more than " << kMaxTracedTokens << " tokens\n";
        } else {
            ok = writeHtmlTrace(opt.htmlPath, g, chosen, lex.tokens, source, err);
            if (ok) written.push_back(opt.htmlPath);
        }
    }
    if (ok && !opt.csvDir.empty()) ok = writeCsvTables(opt.csvDir, g, chosen, written, err);
    for (size_t k = 0; k < written.size(); ++k) cout << "EXPORT " << written[k] << "\n";
    if (!ok) cout << "Export error: " << err << "\n";
    return ok;
}

// Steps 12-14 for grammars with %semantics v2; returns false on a semantic
// or runtime error
bool runSemantics(const Options& opt, const Grammar& g, const BuiltTables& bt,
                  const LexResult& lex, vector<string>& summary) {
    step("STEP 12  SEMANTIC ANALYSIS (%semantics " + g.semantics() + ")");
    if (g.semantics() != "v2") {
        cout << "Unknown semantics '" << g.semantics() << "'; only v2 is implemented.\n";
        return true;
    }
    ParseOptions po;
    po.recordTrace = false;
    po.buildTree = true;
    ParseResult pr = Parser(g, bt.clr).parse(lex.tokens, po);
    if (!pr.accepted) {
        cout << "Skipped: the program has syntax errors.\n";
        summary.push_back("SEMANTICS skipped (syntax errors)");
        return true;
    }
    Ast ast;
    string err;
    if (!buildAst(g, pr.tree, lex.tokens, ast, err)) {
        cout << "Cannot build the AST: " << err << "\n";
        summary.push_back("SEMANTICS skipped (" + err + ")");
        return true;
    }
    SemaResult sema = analyzeProgram(ast);
    cout << "AST: " << ast.nodes.size() << " nodes; " << sema.decls.size() << " declaration(s) in "
         << sema.scopes << " scope(s)\n";
    printDiagnostics(cout, sema);
    if (!sema.errors.empty()) {
        summary.push_back("RESULT SEMANTIC-ERROR count=" + to_string(sema.errors.size()) +
                          " warnings=" + to_string(sema.warnings.size()));
        return false;
    }
    summary.push_back("RESULT SEMANTIC-OK warnings=" + to_string(sema.warnings.size()) +
                      " declarations=" + to_string(sema.decls.size()));
    if (opt.tac) {
        step("STEP 13  THREE-ADDRESS CODE");
        vector<string> code = generateTac(ast, sema);
        for (size_t k = 0; k < code.size(); ++k) cout << code[k] << "\n";
        summary.push_back("TAC lines=" + to_string(code.size()));
    }
    if (opt.run) {
        step("STEP 14  RUN (TREE-WALKING INTERPRETER)");
        RunResult rr = runProgram(ast, sema, opt.maxSteps);
        for (size_t k = 0; k < rr.output.size(); ++k) cout << "OUTPUT " << rr.output[k] << "\n";
        if (!rr.ok) {
            cout << "RUNTIME ERROR line " << rr.error.line << " col " << rr.error.col << ": "
                 << rr.error.message << "\n";
            summary.push_back("RESULT RUNTIME-ERROR line=" + to_string(rr.error.line) + " col=" +
                              to_string(rr.error.col) + " message=\"" + rr.error.message + "\"");
            return false;
        }
        summary.push_back("RUN ok steps=" + to_string(rr.steps) + " outputs=" +
                          to_string(rr.output.size()));
    }
    return true;
}

// Machine-readable lines at the end of the output
void printSummary(const Grammar& g, const BuiltTables& bt, const vector<ParserEntry>& parsers,
                  const ComparisonResult& cmp, const vector<string>& treeLines) {
    cout << "\n" << utilRepeat('-', 70) << "\n";
    for (size_t k = 0; k < parsers.size(); ++k) {
        const ParsingTable& t = *parsers[k].table;
        cout << "TABLE " << t.name << " states=" << t.stateItems.size()
             << " action=" << t.actionEntries() << " goto=" << t.gotoEntries()
             << " sr=" << t.srConflicts() << " rr=" << t.rrConflicts();
        if (g.hasPrecedence())
            cout << " sr_prec=" << t.srPrecConflicts() << " nonassoc=" << t.nonassocErrors;
        cout << "\n";
    }
    for (size_t k = 0; k < parsers.size(); ++k) {
        const ParsingTable& t = *parsers[k].table;
        for (size_t c = 0; c < t.conflicts.size(); ++c) {
            const Conflict& cf = t.conflicts[c];
            cout << "CONFLICT " << t.name << " " << cf.kind << " on '" << cf.symbol
                 << "' state=" << cf.state << " -> " << cf.resolution << "\n";
        }
    }
    for (size_t k = 0; k < cmp.parsers.size(); ++k) {
        const ParserReport& pr = cmp.parsers[k];
        cout << "RESULT " << pr.name << " ";
        if (pr.accepted) {
            cout << "ACCEPTED steps=" << pr.steps << "\n";
        } else {
            cout << "REJECTED token=" << pr.errorToken << " line=" << pr.errorLine
                 << " col=" << pr.errorColumn << " step=" << pr.errorStep
                 << " reduces=" << pr.reductionsBeforeError
                 << " expected=" << utilJoin(pr.expected, " ") << "\n";
        }
    }
    for (size_t k = 0; k < treeLines.size(); ++k) cout << treeLines[k] << "\n";
    for (size_t k = 0; k < cmp.parsers.size(); ++k) {
        const ParserReport& pr = cmp.parsers[k];
        if (!pr.recoverRan) continue;
        cout << "RECOVERY " << pr.name << " errors=" << pr.recoverErrors
             << " skipped=" << pr.recoverSkipped << " first=";
        if (pr.recoverErrors > 0) cout << pr.recoverFirstLine << ":" << pr.recoverFirstColumn;
        else cout << "-";
        if (!pr.recoverAbort.empty()) cout << " stopped=\"" << pr.recoverAbort << "\"";
        cout << "\n";
    }
    for (size_t k = 0; k < parsers.size(); ++k) {
        const PackedTable& pt = packedFor(bt, parsers[k].table->name);
        cout << "COMPRESS " << parsers[k].table->name << " raw_bytes=" << pt.rawBytes()
             << " packed_bytes=" << pt.packedBytes() << " ratio="
             << utilFixed3((double)pt.packedBytes() / (double)pt.rawBytes()) << "\n";
    }
}

}

int main(int argc, char** argv) {
    Options opt;
    string err;
    if (!parseArgs(argc, argv, opt, err)) {
        cerr << "Error: " << err << "\n";
        usage(cerr);
        return kExitUsage;
    }

    if (opt.randomGrammars >= 0) {
        utilBanner(cout, "PARSERX - RANDOM GRAMMAR STUDY");
        RandomGrammarOptions ro;
        ro.count = opt.randomGrammars;
        ro.seed = (unsigned)opt.seed;
        ro.csvPath = opt.csvPath;
        runRandomGrammarStudy(ro, cout);
        return kExitAccepted;
    }

    string source;
    string sourceName;
    if (opt.batch() && opt.inputPath.empty() && !opt.haveCode) sourceName = "(none)";
    else if (!readSource(opt, source, sourceName)) return kExitUsage;

    utilBanner(cout, "PARSERX - SLR / LALR / CLR PARSER");
    cout << "Grammar file : " << opt.grammarPath << "\n";
    cout << "Input        : " << sourceName << " (" << source.size() << " bytes)\n";

    // The grammar is read first so that a %lexer directive can pick the lexer;
    // its errors are still reported in STEP 2, after any lexical errors.
    Grammar g;
    string grammarErr;
    bool grammarOk = g.load(opt.grammarPath, grammarErr);

    LexResult lex;
    if (!runLexer(opt, g, grammarOk, source, lex)) return kExitGrammar;
    if (!lex.errors.empty()) {
        printLexicalErrors(opt, lex);
        return kExitLexical;
    }

    step("STEP 2  GRAMMAR LOADER");
    if (!grammarOk) {
        cout << "Grammar error: " << grammarErr << "\n";
        resultLine("RESULT GRAMMAR-ERROR " + grammarErr);
        return kExitGrammar;
    }
    g.print(cout);
    int retyped = retypeForGrammar(lex.tokens, g);
    if (retyped > 0) {
        cout << "Note: " << retyped << " token(s) retyped to this grammar's terminals: ";
        vector<string> types;
        for (size_t k = 0; k < lex.tokens.size(); ++k) types.push_back(lex.tokens[k].type);
        cout << utilJoin(types, " ") << "\n";
    }

    FirstFollow ff(g);
    printFirstFollow(g, ff);

    BuiltTables bt = buildAllTables(g, ff, opt.lalrMethod);
    vector<ParserEntry> parsers;
    ParserEntry e;
    e.table = &bt.slr; e.machine = &bt.slrMachine; e.buildUs = bt.slrMicroseconds;
    parsers.push_back(e);
    e.table = &bt.lalr; e.machine = &bt.lalrMachine; e.buildUs = bt.lalrMicroseconds;
    parsers.push_back(e);
    e.table = &bt.clr; e.machine = &bt.clrMachine; e.buildUs = bt.clrMicroseconds;
    parsers.push_back(e);
    e.table = &bt.pgm; e.machine = &bt.pgmMachine; e.buildUs = bt.pgmMicroseconds;
    parsers.push_back(e);

    printTables(opt, g, bt, parsers);
    printConflictCheck(parsers);
    if (!writeExports(opt, g, bt, parsers, lex, source)) return kExitUsage;
    vector<string> extraSummary;
    if (opt.explain) {
        step("STEP 8b  CONFLICT EXPLANATION (FOLLOW / MERGE PROVENANCE, SHORTEST WITNESS)");
        explainConflicts(cout, g, ff, bt, extraSummary);
    }

    if (opt.classify) {
        step("STEP 8c  GRAMMAR CLASS (RAW CONFLICTS BEFORE ANY RESOLUTION)");
        GrammarClass gc = classifyGrammar(g, ff, bt);
        printClassification(cout, gc);
        extraSummary.push_back(gc.line);
        extraSummary.push_back("CLASS raw lr0=" + to_string(gc.lr0) + " slr=" + to_string(gc.slr) +
                               " lalr=" + to_string(gc.lalr) + " clr=" + to_string(gc.clr) +
                               " pgm=" + to_string(gc.pgm));
    }

    if (opt.bench) {
        step("STEP B  BENCHMARK");
        GeneratedLexer gen;
        bool generated = !g.lexerSpec().empty() && gen.loadSpec(g.lexerSpec(), err);
        const Grammar& gr = g;
        BenchOptions bo;
        bo.repeat = opt.repeat;
        runBenchmark(g, ff, source, [&](const string& text) {
            LexResult r = generated ? gen.tokenize(text) : Lexer::tokenize(text);
            retypeForGrammar(r.tokens, gr);
            return r;
        }, bo, cout);
        return kExitAccepted;
    }

    if (opt.fuzz >= 0) {
        step("STEP F  DIFFERENTIAL FUZZING OF SLR, LALR, CLR AND PGM");
        FuzzOptions fo;
        fo.count = opt.fuzz;
        fo.seed = (unsigned)opt.seed;
        fo.maxDepth = opt.maxDepth;
        fo.csvPath = opt.csvPath;
        fo.recover = opt.recover;
        fo.compressed = opt.compressed;
        return runFuzz(g, bt, fo, cout) ? kExitAccepted : kExitCheckFailed;
    }

    vector<string> treeLines;
    bool allAccepted = parseAll(opt, g, bt, parsers, lex, source, treeLines);

    step("STEP 11  COMPARISON OF SLR, LALR, CLR AND PGM ON THIS INPUT");
    CompareOptions co;
    co.recover = opt.recover;
    co.maxErrors = opt.maxErrors;
    co.compressed = opt.compressed;
    ComparisonResult cmp = compareParsers(g, lex.tokens, bt, co);
    printComparison(cout, cmp);
    cout << "LALR build us above: " << bt.lalrMethod << " method. LALR-merge "
         << bt.lalrMergeMicroseconds << " us (includes CLR construction), LALR-dp "
         << bt.lalrDpMicroseconds << " us (LR(0) + relations, no CLR)\n";

    if (opt.glr) {
        step("STEP 10b  GLR ON THE UNRESOLVED LALR TABLE");
        GlrResult gr = glrParse(g, bt.lalrMerge, lex.tokens);
        printGlr(cout, gr);
        string line = "GLR ";
        if (gr.aborted) line += "aborted reason=\"" + gr.abortReason + "\"";
        else if (!gr.accepted) line += "rejected token=" + to_string(gr.errorToken);
        else if (gr.infinite) line += "accepted parses=infinite";
        else line += "accepted parses=" + string(gr.saturated ? ">=" : "") + to_string(gr.parses);
        line += " gss_nodes=" + to_string(gr.gssNodes) + " sppf_nodes=" + to_string(gr.sppfNodes);
        extraSummary.push_back(line);
    }

    if (opt.precision) {
        step("STEP 11b  PRECISION INDEX (SLR, LALR AND PGM AGAINST CLR)");
        PrecisionReport pr = computePrecision(g, ff, bt);
        printPrecision(cout, pr);
        extraSummary.push_back(precisionLine(pr));
    }

    bool semanticsOk = true;
    if (!g.semantics().empty()) semanticsOk = runSemantics(opt, g, bt, lex, extraSummary);

    printSummary(g, bt, parsers, cmp, treeLines);
    for (size_t k = 0; k < extraSummary.size(); ++k) cout << extraSummary[k] << "\n";

    int code = allAccepted ? kExitAccepted : kExitRejected;
    if (allAccepted && !semanticsOk) code = kExitSemantic;
    if (opt.verifyLalr && !verifyLalrTables(g, bt.lalrMerge, bt.lalrDp, cout))
        code = kExitCheckFailed;
    if (opt.verifyPgm && !verifyPgm(bt)) code = kExitCheckFailed;
    if (opt.verifyCompress) {
        for (size_t k = 0; k < parsers.size(); ++k)
            if (!packedFor(bt, parsers[k].table->name).verify(*parsers[k].table, cout))
                code = kExitCheckFailed;
    }
    return code;
}
