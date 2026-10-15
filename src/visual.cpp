#include "visual.h"
#include "parser.h"
#include "util.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace std;

namespace {

typedef pair<int, int> Core;

string dotEscape(const string& s) {
    string out;
    for (size_t k = 0; k < s.size(); ++k) {
        if (s[k] == '"' || s[k] == '\\') out += '\\';
        out += s[k];
    }
    return out;
}

string jsonString(const string& s) {
    string out = "\"";
    for (size_t k = 0; k < s.size(); ++k) {
        unsigned char c = (unsigned char)s[k];
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '<') out += "\\u003c";
        else if (c == '>') out += "\\u003e";
        else if (c == '&') out += "\\u0026";
        else if (c < 0x20) {
            static const char* const kHex = "0123456789abcdef";
            out += "\\u00";
            out += kHex[c >> 4];
            out += kHex[c & 15];
        } else {
            out += (char)c;
        }
    }
    return out + "\"";
}

string csvCell(const string& s) {
    if (s.find_first_of(",\"\n") == string::npos) return s;
    string out = "\"";
    for (size_t k = 0; k < s.size(); ++k) {
        if (s[k] == '"') out += "\"\"";
        else out += s[k];
    }
    return out + "\"";
}

string shortAction(const Action& a) {
    if (a.type == ActionType::SHIFT) return "s" + to_string(a.value);
    if (a.type == ActionType::REDUCE) return "r" + to_string(a.value);
    if (a.type == ActionType::ACCEPT) return "acc";
    return "";
}

bool openFile(const string& path, ofstream& out, string& error) {
    out.open(path.c_str(), ios::binary);
    if (!out) error = "cannot write " + path;
    return (bool)out;
}

set<int> conflictStates(const ParsingTable& t) {
    set<int> s;
    for (size_t c = 0; c < t.conflicts.size(); ++c) s.insert(t.conflicts[c].state);
    return s;
}

const char* const kPalette[] = {"#cfe2f3", "#d9ead3", "#fff2cc", "#f4cccc", "#d9d2e9", "#fce5cd",
                                "#d0e0e3", "#ead1dc", "#e6b8af", "#c9daf8", "#b6d7a8", "#ffe599"};

}

string withSuffix(const string& path, const string& suffix) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == string::npos || (slash != string::npos && dot < slash)) return path + suffix;
    return path.substr(0, dot) + suffix + path.substr(dot);
}

// DOT exports
bool writeAutomatonDot(const string& path, const Grammar& g, const LRMachine& m,
                       const ParsingTable& t, string& error) {
    ofstream out;
    if (!openFile(path, out, error)) return false;
    set<int> conflicts = conflictStates(t);
    out << "digraph \"" << m.name << "\" {\n  rankdir=LR;\n"
        << "  node [shape=box, fontname=\"monospace\", fontsize=10];\n";
    for (size_t s = 0; s < m.states.size(); ++s) {
        const State& st = m.states[s];
        string label = m.name + " state " + to_string(s) + "\\l";
        for (size_t i = 0; i < st.items.size(); ++i)
            label += dotEscape(itemToString(g, st.items[i], m.kind)) + "\\l";
        out << "  s" << s << " [label=\"" << label << "\"";
        if (conflicts.count((int)s)) out << ", color=red, penwidth=3";
        out << "];\n";
    }
    for (size_t s = 0; s < m.states.size(); ++s) {
        const map<string, int>& tr = m.states[s].transitions;
        for (map<string, int>::const_iterator it = tr.begin(); it != tr.end(); ++it)
            out << "  s" << s << " -> s" << it->second << " [label=\"" << dotEscape(it->first)
                << "\"];\n";
    }
    out << "}\n";
    return true;
}

bool writeLalrMergeDot(const string& path, const Grammar& g, const BuiltTables& bt, string& error) {
    (void)g;
    ofstream out;
    if (!openFile(path, out, error)) return false;
    map<vector<Core>, int> lalrByCore;
    for (size_t s = 0; s < bt.lalrMerge.stateItems.size(); ++s) {
        vector<Core> core;
        for (size_t i = 0; i < bt.lalrMerge.stateItems[s].size(); ++i)
            core.push_back(Core(bt.lalrMerge.stateItems[s][i].prod, bt.lalrMerge.stateItems[s][i].dot));
        sort(core.begin(), core.end());
        lalrByCore[core] = (int)s;
    }
    set<int> lalrConflicts = conflictStates(bt.lalrMerge);
    out << "digraph \"LALR merge\" {\n  rankdir=LR;\n"
        << "  node [shape=box, style=filled, fontname=\"monospace\", fontsize=10];\n";
    const LRMachine& clr = bt.clrMachine;
    for (size_t s = 0; s < clr.states.size(); ++s) {
        vector<Core> core;
        for (size_t i = 0; i < clr.states[s].items.size(); ++i)
            core.push_back(Core(clr.states[s].items[i].prod, clr.states[s].items[i].dot));
        sort(core.begin(), core.end());
        int group = lalrByCore[core];
        out << "  c" << s << " [label=\"CLR " << s << "\\nLALR " << group << "\", fillcolor=\""
            << kPalette[(size_t)group % 12] << "\"";
        if (lalrConflicts.count(group)) out << ", color=red, penwidth=3";
        out << "];\n";
    }
    for (size_t s = 0; s < clr.states.size(); ++s) {
        const map<string, int>& tr = clr.states[s].transitions;
        for (map<string, int>::const_iterator it = tr.begin(); it != tr.end(); ++it)
            out << "  c" << s << " -> c" << it->second << " [label=\"" << dotEscape(it->first)
                << "\"];\n";
    }
    out << "}\n";
    return true;
}

// HTML trace
bool writeHtmlTrace(const string& path, const Grammar& g, const vector<const ParsingTable*>& tables,
                    const vector<Token>& tokens, const string& source, string& error) {
    ofstream out;
    if (!openFile(path, out, error)) return false;
    ostringstream data;
    data << "{\"source\":" << jsonString(source) << ",\"parsers\":[";
    for (size_t k = 0; k < tables.size(); ++k) {
        ParseResult r = Parser(g, *tables[k]).parse(tokens, true);
        if (k) data << ",";
        data << "{\"name\":" << jsonString(tables[k]->name)
             << ",\"accepted\":" << (r.accepted ? "true" : "false") << ",\"steps\":[";
        for (size_t s = 0; s < r.steps.size(); ++s) {
            const ParseStep& st = r.steps[s];
            vector<string> states = utilToStrings(st.stateStack);
            vector<string> input;
            for (size_t i = 0; i < st.remaining.size(); ++i) input.push_back(st.remaining[i].type);
            if (s) data << ",";
            data << "[" << jsonString(utilJoin(states, " ")) << ","
                 << jsonString(utilJoin(st.symbolStack, " ")) << "," << jsonString(utilJoin(input, " "))
                 << "," << jsonString(st.action) << "]";
        }
        data << "]}";
    }
    data << "]}";

    out << "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "<title>ParserX parse trace</title>\n<style>\n"
           ":root { --bg: #fafafa; --fg: #1f2328; --muted: #656d76; --line: #d0d7de; --hl: #fff3b0;"
           " --acc: #1a7f37; --rej: #cf222e; }\n"
           "@media (prefers-color-scheme: dark) { :root { --bg: #0d1117; --fg: #e6edf3; --muted: #8d96a0;"
           " --line: #30363d; --hl: #3b2f00; --acc: #3fb950; --rej: #f85149; } }\n"
           "body { background: var(--bg); color: var(--fg); font: 14px/1.4 system-ui, sans-serif;"
           " margin: 0; padding: 16px; }\n"
           "h1 { font-size: 20px; margin: 0 0 8px; } pre { background: transparent; border: 1px solid"
           " var(--line); padding: 8px; overflow-x: auto; }\n"
           ".controls { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; margin: 12px 0; }\n"
           "button { font: inherit; padding: 4px 10px; } input[type=range] { flex: 1; min-width: 160px; }\n"
           ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(260px, 1fr)); gap: 12px; }\n"
           ".card { border: 1px solid var(--line); border-radius: 6px; padding: 10px; }\n"
           ".card h2 { font-size: 16px; margin: 0 0 6px; } .ok { color: var(--acc); } .bad { color: var(--rej); }\n"
           ".row { font-family: ui-monospace, monospace; font-size: 12px; white-space: pre-wrap;"
           " word-break: break-all; } .label { color: var(--muted); }\n"
           ".action { background: var(--hl); padding: 2px 4px; border-radius: 3px; }\n"
           "</style>\n</head>\n<body>\n<h1>ParserX parse trace</h1>\n<pre id=\"src\"></pre>\n"
           "<div class=\"controls\"><button id=\"first\">|&lt;</button><button id=\"prev\">&lt;</button>"
           "<button id=\"play\">play</button><button id=\"next\">&gt;</button><button id=\"last\">&gt;|"
           "</button><input type=\"range\" id=\"step\" min=\"0\" value=\"0\"><span id=\"pos\"></span></div>\n"
           "<div class=\"grid\" id=\"grid\"></div>\n<script>\nconst DATA = "
        << data.str()
        << ";\n"
           "const grid = document.getElementById('grid');\n"
           "const slider = document.getElementById('step');\n"
           "document.getElementById('src').textContent = DATA.source;\n"
           "const max = Math.max(0, ...DATA.parsers.map(p => p.steps.length - 1));\n"
           "slider.max = max;\n"
           "function field(label, value) {\n"
           "  const d = document.createElement('div'); d.className = 'row';\n"
           "  const l = document.createElement('span'); l.className = 'label'; l.textContent = label + ' ';\n"
           "  d.appendChild(l); d.appendChild(document.createTextNode(value)); return d;\n}\n"
           "function render() {\n"
           "  const i = Number(slider.value);\n"
           "  document.getElementById('pos').textContent = 'step ' + i + ' of ' + max;\n"
           "  grid.textContent = '';\n"
           "  for (const p of DATA.parsers) {\n"
           "    const c = document.createElement('div'); c.className = 'card';\n"
           "    const h = document.createElement('h2'); h.textContent = p.name + ' ';\n"
           "    const r = document.createElement('span'); r.className = p.accepted ? 'ok' : 'bad';\n"
           "    r.textContent = p.accepted ? 'accepted' : 'rejected'; h.appendChild(r); c.appendChild(h);\n"
           "    const s = p.steps[Math.min(i, p.steps.length - 1)];\n"
           "    if (s) {\n"
           "      c.appendChild(field('states', s[0])); c.appendChild(field('symbols', s[1]));\n"
           "      c.appendChild(field('input', s[2]));\n"
           "      const a = field('action', ''); const t = document.createElement('span');\n"
           "      t.className = 'action'; t.textContent = i < p.steps.length ? s[3] : '(finished)';\n"
           "      a.appendChild(t); c.appendChild(a);\n"
           "    }\n"
           "    grid.appendChild(c);\n"
           "  }\n}\n"
           "let timer = null;\n"
           "function go(i) { slider.value = Math.max(0, Math.min(max, i)); render(); }\n"
           "document.getElementById('first').onclick = () => go(0);\n"
           "document.getElementById('last').onclick = () => go(max);\n"
           "document.getElementById('prev').onclick = () => go(Number(slider.value) - 1);\n"
           "document.getElementById('next').onclick = () => go(Number(slider.value) + 1);\n"
           "document.getElementById('play').onclick = () => {\n"
           "  if (timer) { clearInterval(timer); timer = null; return; }\n"
           "  timer = setInterval(() => { if (Number(slider.value) >= max) { clearInterval(timer);"
           " timer = null; } else go(Number(slider.value) + 1); }, 400);\n};\n"
           "slider.oninput = render;\nrender();\n</script>\n</body>\n</html>\n";
    return true;
}

// CSV tables
bool writeCsvTables(const string& dir, const Grammar& g, const vector<const ParsingTable*>& tables,
                    vector<string>& written, string& error) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        error = "cannot create directory " + dir;
        return false;
    }
    for (size_t k = 0; k < tables.size(); ++k) {
        const ParsingTable& t = *tables[k];
        string lower;
        for (size_t c = 0; c < t.name.size(); ++c)
            lower += (char)tolower(static_cast<unsigned char>(t.name[c]));
        string actionPath = dir + "/" + lower + "_action.csv";
        string gotoPath = dir + "/" + lower + "_goto.csv";
        ofstream a;
        ofstream gt;
        if (!openFile(actionPath, a, error) || !openFile(gotoPath, gt, error)) return false;
        const vector<string>& terms = g.terminals();
        a << "state";
        for (size_t c = 0; c < terms.size(); ++c) a << "," << csvCell(terms[c]);
        a << "\n";
        for (size_t s = 0; s < t.stateItems.size(); ++s) {
            a << s;
            for (size_t c = 0; c < terms.size(); ++c) {
                const Action* act = t.findAction((int)s, terms[c]);
                a << "," << (act ? shortAction(*act) : "");
            }
            a << "\n";
        }
        vector<string> nts;
        for (size_t c = 0; c < g.nonterminals().size(); ++c)
            if (g.nonterminals()[c] != g.augmentedStart()) nts.push_back(g.nonterminals()[c]);
        gt << "state";
        for (size_t c = 0; c < nts.size(); ++c) gt << "," << csvCell(nts[c]);
        gt << "\n";
        for (size_t s = 0; s < t.stateItems.size(); ++s) {
            gt << s;
            map<int, map<string, int> >::const_iterator row = t.gotos.find((int)s);
            for (size_t c = 0; c < nts.size(); ++c) {
                gt << ",";
                if (row == t.gotos.end()) continue;
                map<string, int>::const_iterator it = row->second.find(nts[c]);
                if (it != row->second.end()) gt << it->second;
            }
            gt << "\n";
        }
        written.push_back(actionPath);
        written.push_back(gotoPath);
    }
    return true;
}
