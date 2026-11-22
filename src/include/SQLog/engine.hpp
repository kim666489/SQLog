#ifndef SQLOG_ENGINE_HPP
#define SQLOG_ENGINE_HPP

#include <iostream>
#include <filesystem>
#include <functional>
#include <algorithm>
#include <thread>
#include <mutex>
#include <string>
#include <format>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <regex>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include "nlohmann/json.hpp"
#include "peglib.h"

using namespace std;
namespace fs = std::filesystem;
using json = nlohmann::json;

// =============================================================================
// Grammar
// =============================================================================

// ภาษา query (style SQL) -- keyword ไม่สนตัวพิมพ์เล็ก/ใหญ่
inline const string SQLogGrammar = R"PEG(
    Program     <- (Statement ';'?)*

    Statement   <- Print / Select / Count / Delete / Show / Head / Tail
                 / Load / Save / Clear / Help

    Print       <- 'print'i Value (',' Value)*
    Select      <- ('search'i / 'select'i) Distinct? (Star / FieldList)? Where? OrderBy? Limit?
    Count       <- 'count'i Where? GroupBy?
    Delete      <- 'delete'i Where
    Show        <- 'show'i ShowTarget
    Head        <- 'head'i Number?
    Tail        <- 'tail'i Number?
    Load        <- 'load'i String
    Save        <- 'save'i String
    Clear       <- 'clear'i
    Help        <- 'help'i

    Distinct    <- 'distinct'i
    Star        <- '*'
    FieldList   <- Field (',' Field)*
    Where       <- 'where'i Expr
    GroupBy     <- 'group'i 'by'i Field
    OrderBy     <- 'order'i 'by'i OrderItem (',' OrderItem)*
    OrderItem   <- Field Dir?
    Limit       <- 'limit'i Number ('offset'i Number)?

    Expr        <- OrExpr
    OrExpr      <- AndExpr ('or'i AndExpr)*
    AndExpr     <- NotExpr ('and'i NotExpr)*
    NotExpr     <- 'not'i NotExpr / Primary
    Primary     <- '(' Expr ')' / Cond
    Cond        <- Field ( Neg? (Between / InList / Like / Contains / Regex)
                         / CompOp Value )

    Neg         <- 'not'i
    Between     <- 'between'i Value 'and'i Value
    InList      <- 'in'i '(' Value (',' Value)* ')'
    Like        <- 'like'i Value
    Contains    <- 'contains'i Value
    Regex       <- ('regex'i / 'matches'i) Value

    CompOp      <- '<=' / '>=' / '<>' / '!=' / '=' / '<' / '>'
    Field       <- 'content'i / 'config'i / 'time'i / 'tag'i / 'id'i
    Dir         <- 'asc'i / 'desc'i
    ShowTarget  <- 'tags'i / 'configs'i / 'stats'i

    Value       <- String / Number
    String      <- < '"' ('\\' . / !'"' .)* '"' >
    Number      <- < '-'? [0-9]+ >

    %whitespace <- ([ \t\r\n] / '--' [^\n]*)*
    %word       <- [a-zA-Z0-9_]+
)PEG";

// ไวยากรณ์ของไฟล์ log:  [Tag] [time] [config] "content"
inline const string LogGrmmar = R"PEG(
    Program <- NewLine* (Log NewLine*)*

    Log <- '[' Type ']' Space
           '[' Timestamp ']' Space
           '[' User ']' Space
           Message

    Type <- [^\] \t\r\n]+

    Timestamp <- [0-9][0-9][0-9][0-9] '-'
                 [0-9][0-9] '-'
                 [0-9][0-9] 'T'
                 [0-9][0-9] ':'
                 [0-9][0-9] ':'
                 [0-9][0-9]

    User <- [^\] \t\r\n]+

    Message <- '"' ('\\' . / !'"' .)* '"'

    Space <- [ \t]+
    NewLine <- '\r'? '\n'
)PEG";

struct LogLine {
    int id = 0;
    string content = "";
    string tag = "";
    string config = "";
    string time = "";
};

enum SQLogEngineError {
    SQLOG_ENGINE_FILE_NOT_FOUND,
    SQLOG_ENGINE_FOLDER_NOT_FOUND,
    SQLOG_ENGINE_ERROR_TYPE,
    SQLOG_ENGINE_NOT_OPEN,
    SQLOG_ENGINE_SUCCESS,
    SQLOG_ENGINE_INVALID_GRAMMAR,
    SQLOG_ENGINE_INVALID_INPUT,
    SQLOG_ENGINE_ERROR_AST,
    SQLOG_ENGINE_RUNTIME_ERROR      // query ผิดตอนรัน เช่น regex พัง, id ไม่ใช่ตัวเลข
};

inline vector<LogLine> LogBuffer;      // log ทั้งหมดที่ load อยู่
inline vector<LogLine> LogStack;
inline vector<LogLine> SQLogResult;    // ผลลัพธ์ของ search / head / tail ล่าสุด (เอาไปใช้ใน C++ ต่อได้)
inline string SQLogLastError = "";     // ข้อความ error ล่าสุดจาก doString

inline string SQLogReadFile(fs::path path) {
    ifstream file(path);
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline string getASTValue(shared_ptr<peg::Ast> node, string name) {
    if (!node) return "";

    if (node->name == name) {
        return string(node->token);
    }

    for (auto child : node->nodes) {
        string result = getASTValue(child, name);

        if (!result.empty()) {
            return result;
        }
    }

    return "";
}

inline std::string unescape(const std::string_view& input) {
    std::string result;

    for (size_t i = 0; i < input.size(); i++) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            i++;

            switch (input[i]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"'; break;
                default:
                    result += '\\';
                    result += input[i];
                    break;
            }
        } else {
            result += input[i];
        }
    }

    return result;
}

inline string sqEscape(const string& s) {
    string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;
        }
    }
    return out;
}

inline vector<string> parseTokenAST(shared_ptr<peg::Ast> ast) {
    vector<string> tokens;
    if (!ast) return tokens;

    for (auto node : ast->nodes) {
        if (node->nodes.empty()) {
            tokens.push_back(string(node->token));
        } else {
            vector<string> childTokens = parseTokenAST(node);
            tokens.insert(tokens.end(), childTokens.begin(), childTokens.end());
        }
    }
    return tokens;
}

// =============================================================================
// Parse log file -> LogBuffer
// =============================================================================

inline int parse(fs::path LogPathTarget) {
    if (!fs::exists(LogPathTarget)) {
        return SQLogEngineError::SQLOG_ENGINE_FILE_NOT_FOUND;
    }

    if (fs::is_directory(LogPathTarget)) {
        return SQLogEngineError::SQLOG_ENGINE_ERROR_TYPE;
    }

    peg::parser parser(LogGrmmar);

    if (!parser) {
        return SQLogEngineError::SQLOG_ENGINE_INVALID_GRAMMAR;
    }

    parser.enable_ast();

    shared_ptr<peg::Ast> ast;
    string source = SQLogReadFile(LogPathTarget);

    if (!parser.parse(source, ast)) {
        return SQLogEngineError::SQLOG_ENGINE_INVALID_INPUT;
    }

    if (!ast || ast->name != "Program") {
        return SQLogEngineError::SQLOG_ENGINE_ERROR_AST;
    }

    vector<LogLine> tempBuffer;
    int id = 0;

    for (auto node : ast->nodes) {
        if (node->name != "Log") continue;

        LogLine log;
        log.id = id++;
        log.tag = getASTValue(node, "Type");
        log.time = getASTValue(node, "Timestamp");
        log.config = getASTValue(node, "User");
        log.content = getASTValue(node, "Message");

        // ตัด " หัวท้าย แล้ว unescape (\" \\ \n)
        if (log.content.size() >= 2) {
            log.content = unescape(log.content.substr(1, log.content.size() - 2));
        }

        // content ว่างได้ (""), แต่ tag/time/config ต้องมี
        if (log.tag.empty() || log.time.empty() || log.config.empty()) {
            return SQLogEngineError::SQLOG_ENGINE_ERROR_AST;
        }

        tempBuffer.push_back(log);
    }

    LogBuffer = std::move(tempBuffer);
    return SQLogEngineError::SQLOG_ENGINE_SUCCESS;
}

inline int clear() {
    LogBuffer.clear();
    return SQLogEngineError::SQLOG_ENGINE_SUCCESS;
}

// =============================================================================
// Query helpers
// =============================================================================

struct SQLogQueryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using SQNode = shared_ptr<peg::Ast>;

struct SQTable {
    vector<string> cols;
    vector<vector<string>> rows;
};

inline string sqLower(string s) {
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

inline string sqTrim(const string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline string sqTok(const SQNode& n) {
    return n ? sqTrim(string(n->token)) : string();
}

inline SQNode sqChild(const SQNode& n, const string& name) {
    for (auto& c : n->nodes) if (c->name == name) return c;
    return nullptr;
}

inline vector<SQNode> sqChildren(const SQNode& n, const string& name) {
    vector<SQNode> out;
    for (auto& c : n->nodes) if (c->name == name) out.push_back(c);
    return out;
}

// "text" -> text (ตัด quote + unescape)
inline string sqString(const SQNode& n) {
    string t = sqTok(n);
    if (t.size() >= 2 && t.front() == '"') t = t.substr(1, t.size() - 2);
    return unescape(t);
}

// Value <- String / Number
inline string sqValue(const SQNode& v) {
    SQNode c = v->nodes.empty() ? v : v->nodes[0];
    if (c->name == "String") return sqString(c);
    return sqTok(c);
}

inline bool sqIsInt(const string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); i++) if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

inline size_t sqNumber(const SQNode& n) {
    string t = sqTok(n);
    if (!sqIsInt(t) || t[0] == '-') throw SQLogQueryError("expected a non-negative number, got: " + t);
    return static_cast<size_t>(stoull(t));
}

inline string sqField(const LogLine& l, const string& f) {
    if (f == "id")      return to_string(l.id);
    if (f == "tag")     return l.tag;
    if (f == "time")    return l.time;
    if (f == "config")  return l.config;
    return l.content;
}

// id เทียบเป็นตัวเลข, field อื่นเทียบเป็น string
inline int sqCompare(const string& field, const string& a, const string& b) {
    if (field == "id") {
        long long x = stoll(a), y = stoll(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    int c = a.compare(b);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

// SQL LIKE: % = อะไรก็ได้ (หลายตัว), _ = 1 ตัว, ไม่สนตัวพิมพ์
inline bool sqLike(const string& text, const string& pattern) {
    string t = sqLower(text), p = sqLower(pattern);
    size_t ti = 0, pi = 0, star = string::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == '_' || p[pi] == t[ti])) { ti++; pi++; }
        else if (pi < p.size() && p[pi] == '%') { star = pi++; mark = ti; }
        else if (star != string::npos) { pi = star + 1; ti = ++mark; }
        else return false;
    }
    while (pi < p.size() && p[pi] == '%') pi++;
    return pi == p.size();
}

// ---- expression tree ----------------------------------------------------

struct SQExpr {
    enum Type { AND, OR, NOT, CMP, LIKE, IN, BETWEEN, CONTAINS, REGEX } type = CMP;
    vector<shared_ptr<SQExpr>> kids;
    string field;
    string op;
    vector<string> values;
    bool negate = false;
    shared_ptr<regex> re;
};

inline shared_ptr<SQExpr> sqBuildExpr(const SQNode& n) {
    const string& nm = n->name;

    if (nm == "Where" || nm == "Expr" || nm == "Primary") {
        return sqBuildExpr(n->nodes[0]);
    }

    if (nm == "OrExpr" || nm == "AndExpr") {
        if (n->nodes.size() == 1) return sqBuildExpr(n->nodes[0]);
        auto e = make_shared<SQExpr>();
        e->type = (nm == "OrExpr") ? SQExpr::OR : SQExpr::AND;
        for (auto& k : n->nodes) e->kids.push_back(sqBuildExpr(k));
        return e;
    }

    if (nm == "NotExpr") {
        if (n->nodes[0]->name == "NotExpr") {
            auto e = make_shared<SQExpr>();
            e->type = SQExpr::NOT;
            e->kids.push_back(sqBuildExpr(n->nodes[0]));
            return e;
        }
        return sqBuildExpr(n->nodes[0]);
    }

    if (nm == "Cond") {
        auto e = make_shared<SQExpr>();
        e->field = sqLower(sqTok(sqChild(n, "Field")));
        e->negate = sqChild(n, "Neg") != nullptr;

        if (auto b = sqChild(n, "Between")) {
            e->type = SQExpr::BETWEEN;
            for (auto& v : sqChildren(b, "Value")) e->values.push_back(sqValue(v));
        } else if (auto in = sqChild(n, "InList")) {
            e->type = SQExpr::IN;
            for (auto& v : sqChildren(in, "Value")) e->values.push_back(sqValue(v));
        } else if (auto lk = sqChild(n, "Like")) {
            e->type = SQExpr::LIKE;
            e->values.push_back(sqValue(sqChild(lk, "Value")));
        } else if (auto ct = sqChild(n, "Contains")) {
            e->type = SQExpr::CONTAINS;
            e->values.push_back(sqLower(sqValue(sqChild(ct, "Value"))));
        } else if (auto rg = sqChild(n, "Regex")) {
            e->type = SQExpr::REGEX;
            e->values.push_back(sqValue(sqChild(rg, "Value")));
            try {
                e->re = make_shared<regex>(e->values[0], regex::ECMAScript | regex::icase);
            } catch (const regex_error& ex) {
                throw SQLogQueryError(string("invalid regex: ") + ex.what());
            }
        } else {
            e->type = SQExpr::CMP;
            e->op = sqTok(sqChild(n, "CompOp"));
            e->values.push_back(sqValue(sqChild(n, "Value")));
        }

        if (e->field == "id") {
            for (auto& v : e->values) {
                if (!sqIsInt(v)) throw SQLogQueryError("id must be compared with a number, got: " + v);
            }
        }
        return e;
    }

    throw SQLogQueryError("internal: unknown expression node " + nm);
}

inline bool sqEval(const SQExpr& e, const LogLine& l) {
    switch (e.type) {
        case SQExpr::AND: for (auto& k : e.kids) if (!sqEval(*k, l)) return false; return true;
        case SQExpr::OR:  for (auto& k : e.kids) if (sqEval(*k, l)) return true;  return false;
        case SQExpr::NOT: return !sqEval(*e.kids[0], l);
        default: break;
    }

    const string v = sqField(l, e.field);
    bool r = false;

    switch (e.type) {
        case SQExpr::CMP: {
            int c = sqCompare(e.field, v, e.values[0]);
            if      (e.op == "=")                  r = (c == 0);
            else if (e.op == "!=" || e.op == "<>") r = (c != 0);
            else if (e.op == "<")                  r = (c <  0);
            else if (e.op == ">")                  r = (c >  0);
            else if (e.op == "<=")                 r = (c <= 0);
            else if (e.op == ">=")                 r = (c >= 0);
            return r;   // CMP ไม่มี negate (ใช้ != แทน)
        }
        case SQExpr::LIKE:
            r = sqLike(v, e.values[0]); break;
        case SQExpr::IN:
            for (auto& x : e.values) if (sqCompare(e.field, v, x) == 0) { r = true; break; }
            break;
        case SQExpr::BETWEEN:
            r = sqCompare(e.field, v, e.values[0]) >= 0 && sqCompare(e.field, v, e.values[1]) <= 0;
            break;
        case SQExpr::CONTAINS:
            r = sqLower(v).find(e.values[0]) != string::npos; break;
        case SQExpr::REGEX:
            r = regex_search(v, *e.re); break;
        default: break;
    }
    return e.negate ? !r : r;
}

// ---- table output ---------------------------------------------------------

inline string sqDisplay(const string& s) {
    string out;
    for (char c : s) {
        if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

// นับความกว้างเป็น code point (รองรับ UTF-8 / ภาษาไทยพอประมาณ)
inline size_t sqWidth(const string& s) {
    size_t w = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) w++;
    return w;
}

inline string sqPad(const string& s, size_t width) {
    size_t w = sqWidth(s);
    return s + string(width > w ? width - w : 0, ' ');
}

inline void sqPrintTable(const SQTable& t) {
    if (t.cols.empty()) return;

    vector<size_t> w(t.cols.size());
    for (size_t i = 0; i < t.cols.size(); i++) w[i] = sqWidth(t.cols[i]);
    vector<vector<string>> shown;
    for (auto& r : t.rows) {
        vector<string> row;
        for (size_t i = 0; i < r.size(); i++) {
            row.push_back(sqDisplay(r[i]));
            w[i] = max(w[i], sqWidth(row.back()));
        }
        shown.push_back(std::move(row));
    }

    auto line = [&](const vector<string>& cells) {
        for (size_t i = 0; i < cells.size(); i++) {
            cout << (i ? " | " : "") << sqPad(cells[i], w[i]);
        }
        cout << "\n";
    };

    line(t.cols);
    for (size_t i = 0; i < w.size(); i++) cout << (i ? "-+-" : "") << string(w[i], '-');
    cout << "\n";
    for (auto& r : shown) line(r);
    cout << "(" << t.rows.size() << (t.rows.size() == 1 ? " row)" : " rows)") << "\n";
}

// =============================================================================
// Query execution
// =============================================================================

inline const vector<string> SQAllCols = {"id", "tag", "time", "config", "content"};

inline vector<const LogLine*> sqFilter(const SQNode& whereNode) {
    shared_ptr<SQExpr> where = whereNode ? sqBuildExpr(whereNode) : nullptr;
    vector<const LogLine*> rows;
    for (auto& l : LogBuffer) {
        if (!where || sqEval(*where, l)) rows.push_back(&l);
    }
    return rows;
}

inline void sqEmitRows(const vector<const LogLine*>& rows,
                       const vector<string>& cols,
                       bool distinct, size_t offset, optional<size_t> limit) {
    SQTable t;
    t.cols = cols;
    SQLogResult.clear();

    set<vector<string>> seen;
    size_t skipped = 0;

    for (auto* l : rows) {
        vector<string> r;
        for (auto& c : cols) r.push_back(sqField(*l, c));

        if (distinct && !seen.insert(r).second) continue;
        if (skipped < offset) { skipped++; continue; }
        if (limit && t.rows.size() >= *limit) break;

        t.rows.push_back(std::move(r));
        SQLogResult.push_back(*l);
    }
    sqPrintTable(t);
}

inline void sqGroupCount(const string& field, const vector<const LogLine*>& rows) {
    map<string, size_t> groups;
    for (auto* l : rows) groups[sqField(*l, field)]++;

    vector<pair<string, size_t>> sorted(groups.begin(), groups.end());
    stable_sort(sorted.begin(), sorted.end(),
                [](auto& a, auto& b) { return a.second > b.second; });

    SQTable t;
    t.cols = {field, "count"};
    for (auto& [k, n] : sorted) t.rows.push_back({k, to_string(n)});
    sqPrintTable(t);
}

inline void sqRunSelect(const SQNode& n) {
    auto rows = sqFilter(sqChild(n, "Where"));

    // ORDER BY
    if (auto o = sqChild(n, "OrderBy")) {
        vector<pair<string, bool>> keys;   // field, desc?
        for (auto& it : sqChildren(o, "OrderItem")) {
            string f = sqLower(sqTok(sqChild(it, "Field")));
            bool desc = false;
            if (auto d = sqChild(it, "Dir")) desc = (sqLower(sqTok(d)) == "desc");
            keys.push_back({f, desc});
        }
        stable_sort(rows.begin(), rows.end(), [&](const LogLine* a, const LogLine* b) {
            for (auto& [f, desc] : keys) {
                int c = sqCompare(f, sqField(*a, f), sqField(*b, f));
                if (c != 0) return desc ? c > 0 : c < 0;
            }
            return false;
        });
    }

    // fields
    vector<string> cols;
    if (auto fl = sqChild(n, "FieldList")) {
        for (auto& f : sqChildren(fl, "Field")) cols.push_back(sqLower(sqTok(f)));
    } else {
        cols = SQAllCols;   // * หรือไม่ระบุ
    }

    // LIMIT / OFFSET
    size_t offset = 0;
    optional<size_t> limit;
    if (auto lm = sqChild(n, "Limit")) {
        auto nums = sqChildren(lm, "Number");
        limit = sqNumber(nums[0]);
        if (nums.size() > 1) offset = sqNumber(nums[1]);
    }

    sqEmitRows(rows, cols, sqChild(n, "Distinct") != nullptr, offset, limit);
}

inline void sqRunCount(const SQNode& n) {
    auto rows = sqFilter(sqChild(n, "Where"));
    if (auto g = sqChild(n, "GroupBy")) {
        sqGroupCount(sqLower(sqTok(sqChild(g, "Field"))), rows);
        return;
    }
    SQTable t;
    t.cols = {"count"};
    t.rows.push_back({to_string(rows.size())});
    sqPrintTable(t);
}

inline void sqRunDelete(const SQNode& n) {
    shared_ptr<SQExpr> where = sqBuildExpr(sqChild(n, "Where"));
    size_t before = LogBuffer.size();
    LogBuffer.erase(
        remove_if(LogBuffer.begin(), LogBuffer.end(),
                  [&](const LogLine& l) { return sqEval(*where, l); }),
        LogBuffer.end());
    cout << "deleted " << (before - LogBuffer.size()) << " row(s) (in memory, use `save` to write)\n";
}

inline void sqRunShow(const SQNode& n) {
    string target = sqLower(sqTok(sqChild(n, "ShowTarget")));
    vector<const LogLine*> all;
    for (auto& l : LogBuffer) all.push_back(&l);

    if (target == "tags")    { sqGroupCount("tag", all);    return; }
    if (target == "configs") { sqGroupCount("config", all); return; }

    // stats
    SQTable t;
    t.cols = {"key", "value"};
    t.rows.push_back({"total", to_string(LogBuffer.size())});
    if (!LogBuffer.empty()) {
        auto [mn, mx] = minmax_element(LogBuffer.begin(), LogBuffer.end(),
            [](const LogLine& a, const LogLine& b) { return a.time < b.time; });
        t.rows.push_back({"first", mn->time});
        t.rows.push_back({"last",  mx->time});
        set<string> tags, configs;
        for (auto& l : LogBuffer) { tags.insert(l.tag); configs.insert(l.config); }
        t.rows.push_back({"tags", to_string(tags.size())});
        t.rows.push_back({"configs", to_string(configs.size())});
    }
    sqPrintTable(t);
}

inline string sqErrorName(int code) {
    switch (code) {
        case SQLOG_ENGINE_FILE_NOT_FOUND:   return "file not found";
        case SQLOG_ENGINE_FOLDER_NOT_FOUND: return "folder not found";
        case SQLOG_ENGINE_ERROR_TYPE:       return "path is a directory";
        case SQLOG_ENGINE_NOT_OPEN:         return "not open";
        case SQLOG_ENGINE_INVALID_GRAMMAR:  return "invalid grammar";
        case SQLOG_ENGINE_INVALID_INPUT:    return "file is not in [Tag] [time] [config] \"content\" format";
        case SQLOG_ENGINE_ERROR_AST:        return "bad log line (missing tag/time/config)";
        case SQLOG_ENGINE_RUNTIME_ERROR:    return "runtime error";
        default:                            return "unknown error";
    }
}

inline void sqPrintHelp() {
    cout <<
R"(Commands (keywords are case-insensitive, `;` is optional, `-- comment` supported)

  search [distinct] [* | field, ...] [where <cond>] [order by f [asc|desc], ...] [limit N [offset M]]
  select ...                        same as search
  count [where <cond>] [group by field]
  delete where <cond>               remove rows from memory (use `save` to persist)
  head [N]    tail [N]              first / last N rows (default 10)
  show tags | configs | stats
  load "file.log"                   load a log file into memory
  save "file.log"                   write rows in memory to a file
  clear                             empty memory
  print "text", ...                 print text
  help

Fields : id, tag, time, config, content

Conditions
  field = v   !=   <>   <   >   <=   >=
  field [not] like "net%"           % = any text, _ = one char, case-insensitive
  field [not] contains "timeout"    case-insensitive
  field [not] regex "err(or)?\\d+"  ECMAScript regex, case-insensitive
  field [not] in ("a", "b")
  field [not] between "x" and "y"
  not <cond>   <cond> and <cond>   <cond> or <cond>   ( ... )

Examples
  search * where tag = "ERROR" order by time desc limit 20
  search tag, config where content contains "timeout" and not config like "test%"
  search distinct config where tag in ("WARN", "ERROR")
  search where time between "2026-11-22T00:00:00" and "2026-11-22T23:59:59"
  search where time like "2026-11-22%" and id >= 100
  count group by tag
)";
}

inline void sqRunStatement(const SQNode& st) {
    if (st->nodes.empty()) return;
    SQNode n = st->nodes[0];
    const string& name = n->name;

    if (name == "Print") {
        for (auto& v : sqChildren(n, "Value")) cout << sqValue(v) << "\n";
    }
    else if (name == "Select") sqRunSelect(n);
    else if (name == "Count")  sqRunCount(n);
    else if (name == "Delete") sqRunDelete(n);
    else if (name == "Show")   sqRunShow(n);
    else if (name == "Head" || name == "Tail") {
        size_t k = 10;
        if (auto num = sqChild(n, "Number")) k = sqNumber(num);
        k = min(k, LogBuffer.size());
        vector<const LogLine*> rows;
        size_t start = (name == "Head") ? 0 : LogBuffer.size() - k;
        for (size_t i = start; i < start + k; i++) rows.push_back(&LogBuffer[i]);
        sqEmitRows(rows, SQAllCols, false, 0, nullopt);
    }
    else if (name == "Load") {
        string path = sqString(sqChild(n, "String"));
        int r = parse(path);
        if (r != SQLOG_ENGINE_SUCCESS) throw SQLogQueryError("load \"" + path + "\" failed: " + sqErrorName(r));
        cout << "loaded " << LogBuffer.size() << " row(s) from " << path << "\n";
    }
    else if (name == "Save") {
        string path = sqString(sqChild(n, "String"));
        ofstream f(path, ios::trunc);
        if (!f) throw SQLogQueryError("cannot write file: " + path);
        for (auto& l : LogBuffer) {
            f << "[" << l.tag << "] [" << l.time << "] [" << l.config << "] \""
              << sqEscape(l.content) << "\"\n";
        }
        cout << "saved " << LogBuffer.size() << " row(s) to " << path << "\n";
    }
    else if (name == "Clear") {
        clear();
        cout << "cleared\n";
    }
    else if (name == "Help") sqPrintHelp();
}

inline int runtime(shared_ptr<peg::Ast> ast) {
    if (!ast || ast->name != "Program") return SQLogEngineError::SQLOG_ENGINE_ERROR_AST;

    try {
        for (auto& st : ast->nodes) sqRunStatement(st);
    } catch (const SQLogQueryError& e) {
        SQLogLastError = e.what();
        cerr << "error: " << e.what() << "\n";
        return SQLogEngineError::SQLOG_ENGINE_RUNTIME_ERROR;
    } catch (const std::exception& e) {
        SQLogLastError = e.what();
        cerr << "error: " << e.what() << "\n";
        return SQLogEngineError::SQLOG_ENGINE_RUNTIME_ERROR;
    }
    return SQLogEngineError::SQLOG_ENGINE_SUCCESS;
}

// =============================================================================
// Entry point:  doString("search * where tag = \"ERROR\" limit 10")
// =============================================================================

inline int doString(const string& Code) {
    SQLogLastError.clear();

    string parseError;
    peg::parser parser;
    parser.set_logger([&](size_t line, size_t col, const string& msg) {
        if (parseError.empty())
            parseError = to_string(line) + ":" + to_string(col) + ": " + msg;
    });

    if (!parser.load_grammar(SQLogGrammar)) {
        SQLogLastError = "invalid grammar: " + parseError;
        return SQLogEngineError::SQLOG_ENGINE_INVALID_GRAMMAR;
    }
    parser.enable_ast();

    shared_ptr<peg::Ast> ast;
    if (!parser.parse(Code, ast)) {
        SQLogLastError = "syntax error at " + parseError;
        cerr << "error: " << SQLogLastError << "\n";
        return SQLogEngineError::SQLOG_ENGINE_INVALID_INPUT;
    }
    return runtime(ast);
}

#endif