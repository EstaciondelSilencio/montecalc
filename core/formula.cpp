// MonteCalc - spreadsheet formula parser and in-memory evaluator
#include "formula.h"
#include "flofuncs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>
#include <stdexcept>

namespace mc::formula {

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper((unsigned char)c));
    return s;
}

struct Token {
    enum Type { Number, String, Error, Ident, Op, LParen, RParen, Sep, Colon, End } type;
    std::string text;
    double num = 0;
};

const char* kErrors[] = {"#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#N/A"};

std::vector<Token> tokenize(const std::string& s) {
    std::vector<Token> t;
    size_t i = 0, n = s.size();
    if (n && s[0] == '=') i = 1;
    auto isIdent = [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '$'; };
    while (i < n) {
        char c = s[i];
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1]))) {
            // a number, unless it is the start of an identifier such as a row range (unsupported anyway)
            size_t j = i;
            while (j < n && (std::isdigit((unsigned char)s[j]) || s[j] == '.')) ++j;
            if (j < n && (s[j] == 'e' || s[j] == 'E') && j + 1 < n &&
                (std::isdigit((unsigned char)s[j + 1]) || ((s[j + 1] == '+' || s[j + 1] == '-') && j + 2 < n && std::isdigit((unsigned char)s[j + 2])))) {
                j += 2;
                while (j < n && std::isdigit((unsigned char)s[j])) ++j;
            }
            Token k{Token::Number, s.substr(i, j - i)};
            k.num = std::strtod(k.text.c_str(), nullptr);
            t.push_back(k);
            i = j;
            continue;
        }
        if (c == '"') {
            std::string v;
            ++i;
            while (i < n) {
                if (s[i] == '"') {
                    if (i + 1 < n && s[i + 1] == '"') { v += '"'; i += 2; continue; }
                    break;
                }
                v += s[i++];
            }
            if (i >= n) throw std::invalid_argument("unterminated string");
            ++i;
            t.push_back({Token::String, v});
            continue;
        }
        if (c == '#') {
            bool found = false;
            for (const char* e : kErrors) {
                std::string es = e;
                if (upper(s.substr(i, es.size())) == es) { t.push_back({Token::Error, es}); i += es.size(); found = true; break; }
            }
            if (!found) throw std::invalid_argument("unknown error literal");
            continue;
        }
        if (isIdent(c) || c == '\'') {
            std::string v;
            while (i < n && (isIdent(s[i]) || s[i] == '\'')) {
                if (s[i] == '\'') {  // quoted sheet name
                    v += s[i++];
                    while (i < n) {
                        if (s[i] == '\'') {
                            if (i + 1 < n && s[i + 1] == '\'') { v += "''"; i += 2; continue; }
                            break;
                        }
                        v += s[i++];
                    }
                    if (i < n) v += s[i++];
                } else {
                    v += s[i++];
                }
            }
            t.push_back({Token::Ident, v});
            continue;
        }
        if (c == '(') { t.push_back({Token::LParen, "("}); ++i; continue; }
        if (c == ')') { t.push_back({Token::RParen, ")"}); ++i; continue; }
        if (c == ';' || c == ',') { t.push_back({Token::Sep, ";"}); ++i; continue; }
        if (c == ':') { t.push_back({Token::Colon, ":"}); ++i; continue; }
        if (c == '<' || c == '>') {
            if (i + 1 < n && (s[i + 1] == '=' || (c == '<' && s[i + 1] == '>'))) {
                t.push_back({Token::Op, s.substr(i, 2)});
                i += 2;
            } else {
                t.push_back({Token::Op, std::string(1, c)});
                ++i;
            }
            continue;
        }
        if (std::string("+-*/^&=%").find(c) != std::string::npos) { t.push_back({Token::Op, std::string(1, c)}); ++i; continue; }
        if (c == '{') throw std::invalid_argument("inline arrays are not supported");
        throw std::invalid_argument(std::string("unexpected character '") + c + "'");
    }
    t.push_back({Token::End, ""});
    return t;
}

bool splitRef(const std::string& id, std::string& sheet, std::string& cell) {
    // last '.' outside quotes separates sheet and cell
    int q = 0;
    size_t dot = std::string::npos;
    for (size_t i = 0; i < id.size(); ++i) {
        if (id[i] == '\'') q ^= 1;
        else if (id[i] == '.' && !q) dot = i;
    }
    if (dot == std::string::npos) { sheet.clear(); cell = id; }
    else { sheet = id.substr(0, dot); cell = id.substr(dot + 1); }
    std::string s;
    for (char ch : sheet) if (ch != '$') s += ch;
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        std::string inner = s.substr(1, s.size() - 2), un;
        for (size_t i = 0; i < inner.size(); ++i) {
            un += inner[i];
            if (inner[i] == '\'' && i + 1 < inner.size() && inner[i + 1] == '\'') ++i;
        }
        s = un;
    }
    sheet = s;
    int col, row;
    return parseCellName(cell, col, row);
}

class Parser {
public:
    Parser(std::vector<Token> t, int sheet, Source* src) : t_(std::move(t)), sheet_(sheet), src_(src) {}
    NodePtr parseAll() {
        auto n = comparison();
        if (peek().type != Token::End) throw std::invalid_argument("unexpected '" + peek().text + "'");
        return n;
    }

private:
    std::vector<Token> t_;
    size_t p_ = 0;
    int sheet_;
    Source* src_;
    const Token& peek() const { return t_[p_]; }
    Token next() { return t_[p_++]; }
    bool isOp(const char* o) const { return peek().type == Token::Op && peek().text == o; }

    NodePtr bin(const std::string& op, NodePtr a, NodePtr b) {
        auto n = std::make_shared<Node>();
        n->kind = Node::Binary;
        n->str = op;
        n->args = {a, b};
        return n;
    }
    NodePtr comparison() {
        auto a = concat();
        while (peek().type == Token::Op &&
               (peek().text == "=" || peek().text == "<>" || peek().text == "<" || peek().text == ">" || peek().text == "<=" || peek().text == ">=")) {
            std::string op = next().text;
            a = bin(op, a, concat());
        }
        return a;
    }
    NodePtr concat() {
        auto a = additive();
        while (isOp("&")) { next(); a = bin("&", a, additive()); }
        return a;
    }
    NodePtr additive() {
        auto a = multiplicative();
        while (isOp("+") || isOp("-")) { std::string op = next().text; a = bin(op, a, multiplicative()); }
        return a;
    }
    NodePtr multiplicative() {
        auto a = power();
        while (isOp("*") || isOp("/")) { std::string op = next().text; a = bin(op, a, power()); }
        return a;
    }
    NodePtr power() {
        auto a = unary();
        while (isOp("^")) { next(); a = bin("^", a, unary()); }
        return a;
    }
    NodePtr unary() {
        if (isOp("-") || isOp("+")) {
            std::string op = next().text;
            auto n = std::make_shared<Node>();
            n->kind = Node::Unary;
            n->str = op;
            n->args = {unary()};
            return n;
        }
        return postfix();
    }
    NodePtr postfix() {
        auto a = primary();
        while (isOp("%")) {
            next();
            auto n = std::make_shared<Node>();
            n->kind = Node::Postfix;
            n->str = "%";
            n->args = {a};
            a = n;
        }
        return a;
    }
    NodePtr refNode(const std::string& id) {
        std::string sh, cell;
        if (!splitRef(id, sh, cell)) return nullptr;
        int sheet = sheet_;
        if (!sh.empty()) {
            sheet = src_ ? src_->sheetIndex(sh) : -1;
            if (sheet < 0) throw std::invalid_argument("unknown sheet '" + sh + "'");
        }
        auto n = std::make_shared<Node>();
        n->kind = Node::Ref;
        parseCellName(cell, n->range.a.col, n->range.a.row);
        n->range.a.sheet = sheet;
        n->range.b = n->range.a;
        return n;
    }
    NodePtr primary() {
        Token k = next();
        auto n = std::make_shared<Node>();
        switch (k.type) {
        case Token::Number: n->kind = Node::Num; n->num = k.num; return n;
        case Token::String: n->kind = Node::Str; n->str = k.text; return n;
        case Token::Error: n->kind = Node::Err; n->str = k.text; return n;
        case Token::LParen: {
            auto e = comparison();
            if (next().type != Token::RParen) throw std::invalid_argument("missing ')'");
            return e;
        }
        case Token::Ident: {
            if (peek().type == Token::LParen) {
                next();
                n->kind = Node::Func;
                n->str = upper(k.text);
                if (peek().type == Token::RParen) { next(); return n; }
                while (true) {
                    if (peek().type == Token::Sep || peek().type == Token::RParen) {
                        auto m = std::make_shared<Node>();
                        m->kind = Node::Missing;
                        n->args.push_back(m);
                    } else {
                        n->args.push_back(comparison());
                    }
                    Token s = next();
                    if (s.type == Token::RParen) break;
                    if (s.type != Token::Sep) throw std::invalid_argument("expected ';' or ')'");
                }
                return n;
            }
            if (auto r = refNode(k.text)) {
                if (peek().type == Token::Colon) {
                    next();
                    Token k2 = next();
                    if (k2.type != Token::Ident) throw std::invalid_argument("bad range");
                    std::string sh2, cell2;
                    if (!splitRef(k2.text, sh2, cell2)) throw std::invalid_argument("bad range end");
                    int c2, r2;
                    parseCellName(cell2, c2, r2);
                    r->kind = Node::Range;
                    RangeAddr& ra = r->range;
                    ra.b.col = c2; ra.b.row = r2;
                    if (ra.b.col < ra.a.col) std::swap(ra.a.col, ra.b.col);
                    if (ra.b.row < ra.a.row) std::swap(ra.a.row, ra.b.row);
                }
                return r;
            }
            std::string u = upper(k.text);
            if (u == "TRUE" || u == "FALSE") { n->kind = Node::Bool; n->num = u == "TRUE"; return n; }
            RangeAddr ra;
            if (src_ && src_->namedRange(k.text, ra)) {
                n->kind = (ra.a == ra.b) ? Node::Ref : Node::Range;
                n->range = ra;
                return n;
            }
            throw std::invalid_argument("unknown name '" + k.text + "'");
        }
        default:
            throw std::invalid_argument("unexpected '" + k.text + "'");
        }
    }
};

double toNumber(const Value& v, bool& err, std::string& code) {
    switch (v.kind) {
    case Value::Number:
    case Value::Bool: return v.num;
    case Value::Empty: return 0;
    case Value::String:
        if (v.str.empty()) return 0;
        if (v.isNumberText()) return std::strtod(v.str.c_str(), nullptr);
        err = true; code = "#VALUE!"; return 0;
    case Value::Error: err = true; code = v.str; return 0;
    case Value::Array:
        if (v.arr && !v.arr->empty() && !(*v.arr)[0].empty()) return toNumber((*v.arr)[0][0], err, code);
        err = true; code = "#VALUE!"; return 0;
    }
    return 0;
}

std::string toText(const Value& v) {
    if (v.kind == Value::String) return v.str;
    if (v.kind == Value::Bool) return v.num ? "TRUE" : "FALSE";
    if (v.kind == Value::Number) {
        char b[64];
        std::snprintf(b, sizeof b, "%.15g", v.num);
        return b;
    }
    return "";
}

} // namespace

// ============================================================ helpers

std::string columnName(int col) {
    std::string s;
    ++col;
    while (col > 0) { int r = (col - 1) % 26; s.insert(s.begin(), char('A' + r)); col = (col - 1) / 26; }
    return s;
}

bool parseCellName(const std::string& s, int& col, int& row) {
    size_t i = 0;
    if (i < s.size() && s[i] == '$') ++i;
    size_t c0 = i;
    while (i < s.size() && std::isalpha((unsigned char)s[i])) ++i;
    if (i == c0 || i - c0 > 3) return false;
    std::string letters = upper(s.substr(c0, i - c0));
    if (i < s.size() && s[i] == '$') ++i;
    size_t r0 = i;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
    if (i == r0 || i != s.size()) return false;
    col = 0;
    for (char ch : letters) col = col * 26 + (ch - 'A' + 1);
    col -= 1;
    row = std::atoi(s.substr(r0).c_str()) - 1;
    return row >= 0 && col < 16384;
}

std::string cellName(const CellAddr& a, bool withSheet, const std::function<std::string(int)>& sheetName) {
    std::string s = columnName(a.col) + std::to_string(a.row + 1);
    if (withSheet && sheetName) s = sheetName(a.sheet) + "." + s;
    return s;
}

NodePtr parse(const std::string& f, int sheet, Source* src) {
    Parser p(tokenize(f), sheet, src);
    return p.parseAll();
}

void collectRanges(const NodePtr& n, std::vector<RangeAddr>& out) {
    if (!n) return;
    if (n->kind == Node::Ref || n->kind == Node::Range) out.push_back(n->range);
    for (auto& a : n->args) collectRanges(a, out);
}

void collectCalls(const NodePtr& n, std::vector<const Node*>& out) {
    if (!n) return;
    if (n->kind == Node::Func) out.push_back(n.get());
    for (auto& a : n->args) collectCalls(a, out);
}

std::string baseName(const std::string& f) {
    std::string u = upper(f);
    size_t p = u.rfind('.');
    if (p != std::string::npos && (u.rfind("ORG.MONTECALC", 0) == 0 || u.rfind("COM.", 0) == 0 || u.rfind("_XLFN.", 0) == 0))
        return u.substr(p + 1);
    return u;
}

bool findCall(const std::string& f, const std::string& prefix, std::string& func, std::vector<std::string>& args) {
    std::string u = upper(f), pre = upper(prefix);
    size_t pos = 0;
    while ((pos = u.find(pre, pos)) != std::string::npos) {
        // must not be inside a string literal: count quotes before pos
        size_t q = std::count(u.begin(), u.begin() + pos, '"');
        if (q % 2) { pos += pre.size(); continue; }
        size_t start = pos;
        while (start > 0 && (std::isalnum((unsigned char)u[start - 1]) || u[start - 1] == '_' || u[start - 1] == '.')) --start;
        size_t end = pos;
        while (end < u.size() && (std::isalnum((unsigned char)u[end]) || u[end] == '_')) ++end;
        size_t i = end;
        while (i < u.size() && u[i] == ' ') ++i;
        if (i >= u.size() || u[i] != '(') { pos = end; continue; }
        func = baseName(u.substr(start, end - start));
        args.clear();
        int depth = 0;
        bool str = false;
        std::string cur;
        for (size_t k = i + 1; k < f.size(); ++k) {
            char c = f[k];
            if (str) {
                cur += c;
                if (c == '"') str = false;
                continue;
            }
            if (c == '"') { str = true; cur += c; continue; }
            if (c == '(' || c == '{') ++depth;
            if ((c == ')' || c == '}') && depth == 0) { args.push_back(cur); return true; }
            if (c == ')' || c == '}') --depth;
            if ((c == ';' || c == ',') && depth == 0) { args.push_back(cur); cur.clear(); continue; }
            cur += c;
        }
        return false;
    }
    return false;
}

std::string unquote(const std::string& s0, bool& isString) {
    size_t a = s0.find_first_not_of(' '), b = s0.find_last_not_of(' ');
    std::string s = a == std::string::npos ? std::string() : s0.substr(a, b - a + 1);
    isString = s.size() >= 2 && s.front() == '"' && s.back() == '"';
    if (!isString) return s;
    std::string r;
    for (size_t i = 1; i + 1 < s.size(); ++i) {
        r += s[i];
        if (s[i] == '"' && i + 2 < s.size() && s[i + 1] == '"') ++i;
    }
    return r;
}

std::string sanitizeName(const std::string& s) {
    std::string r;
    for (unsigned char c : s) r += (std::isalnum(c) || c == '_') ? char(c) : '_';
    if (r.empty() || std::isdigit((unsigned char)r[0])) r = "_" + r;
    return r;
}

const std::set<std::string>& supportedFunctions() {
    static const std::set<std::string> s = {
        "IF", "IFERROR", "AND", "OR", "NOT", "TRUE", "FALSE", "SUM", "AVERAGE", "MIN", "MAX", "COUNT", "PRODUCT",
        "ABS", "SQRT", "EXP", "LN", "LOG", "LOG10", "POWER", "MOD", "INT", "ROUND", "ROUNDUP", "ROUNDDOWN",
        "SIGN", "PI", "NPV", "SUMPRODUCT", "CHOOSE", "ISERROR", "ISNUMBER", "CEILING", "FLOOR", "TRUNC"};
    return s;
}

// ============================================================ model

Model::Cell* Model::cellAt(const CellAddr& a) {
    auto it = cells_.find(a);
    return it == cells_.end() ? nullptr : &it->second;
}

bool Model::build(Source& src, const std::vector<CellAddr>& roots, std::string& why) {
    src_ = &src;
    cells_.clear();
    formulas_.clear();
    std::deque<CellAddr> queue(roots.begin(), roots.end());
    const size_t maxCells = 500000;
    while (!queue.empty()) {
        CellAddr a = queue.front();
        queue.pop_front();
        if (cells_.count(a)) continue;
        if (cells_.size() > maxCells) { why = "the model references too many cells"; return false; }
        Cell c;
        c.content = src.cell(a);
        if (c.content.type == CellContent::Formula) {
            try {
                c.tree = parse(c.content.text, a.sheet, &src);
            } catch (const std::exception& e) {
                why = cellName(a, false, nullptr) + ": " + e.what();
                return false;
            }
            std::vector<const Node*> calls;
            collectCalls(c.tree, calls);
            for (const Node* call : calls) {
                std::string b = baseName(call->str);
                if (!supportedFunctions().count(b) && !findFlo(b)) {
                    why = "function " + b + " (cell " + columnName(a.col) + std::to_string(a.row + 1) + ") is not supported by the memory calculation";
                    return false;
                }
            }
            std::vector<RangeAddr> refs;
            collectRanges(c.tree, refs);
            for (const auto& r : refs) {
                size_t count = size_t(r.b.col - r.a.col + 1) * size_t(r.b.row - r.a.row + 1);
                if (count > maxCells) { why = "a referenced range is too large"; return false; }
                for (int col = r.a.col; col <= r.b.col; ++col)
                    for (int row = r.a.row; row <= r.b.row; ++row) queue.push_back({r.a.sheet, col, row});
            }
            formulas_.insert(a);
        }
        cells_[a] = c;
    }
    return true;
}

Value Model::value(const CellAddr& a) {
    Cell* c = cellAt(a);
    if (!c) {
        // not part of the closure: read it once (constant)
        Cell n;
        n.content = src_ ? src_->cell(a) : CellContent{};
        if (n.content.type == CellContent::Formula) return Value::error("#REF!");
        cells_[a] = n;
        c = cellAt(a);
    }
    switch (c->content.type) {
    case CellContent::Empty: return Value();
    case CellContent::Number: return Value::number(c->content.value);
    case CellContent::Text: return Value::text(c->content.text);
    case CellContent::Formula: break;
    }
    if (c->stamp == stamp_) return c->cache;
    if (c->evaluating) return Value::error("#REF!");  // circular reference
    c->evaluating = true;
    Value v;
    try {
        v = eval(*c->tree, a.sheet);
    } catch (const std::exception&) {
        v = Value::error("#VALUE!");
    }
    if (v.kind == Value::Array) {  // implicit intersection: take the first element
        if (v.arr && !v.arr->empty() && !(*v.arr)[0].empty()) v = (*v.arr)[0][0];
        else v = Value::error("#VALUE!");
    }
    c = cellAt(a);  // map may have grown
    c->evaluating = false;
    c->cache = v;
    c->stamp = stamp_;
    return v;
}

Value Model::rangeValue(const RangeAddr& r) {
    std::vector<std::vector<Value>> rows;
    for (int row = r.a.row; row <= r.b.row; ++row) {
        std::vector<Value> line;
        for (int col = r.a.col; col <= r.b.col; ++col) line.push_back(value({r.a.sheet, col, row}));
        rows.push_back(std::move(line));
    }
    return Value::array(std::move(rows));
}

Value Model::eval(const Node& n, int sheet) {
    switch (n.kind) {
    case Node::Num: return Value::number(n.num);
    case Node::Str: return Value::text(n.str);
    case Node::Bool: return Value::boolean(n.num != 0);
    case Node::Err: return Value::error(n.str);
    case Node::Missing: return Value();
    case Node::Ref: return value(n.range.a);
    case Node::Range: return rangeValue(n.range);
    case Node::Unary: {
        Value v = eval(*n.args[0], sheet);
        bool err = false;
        std::string code;
        double x = toNumber(v, err, code);
        if (err) return Value::error(code);
        return Value::number(n.str == "-" ? -x : x);
    }
    case Node::Postfix: {
        Value v = eval(*n.args[0], sheet);
        bool err = false;
        std::string code;
        double x = toNumber(v, err, code);
        if (err) return Value::error(code);
        return Value::number(x / 100);
    }
    case Node::Binary: {
        Value a = eval(*n.args[0], sheet), b = eval(*n.args[1], sheet);
        if (a.kind == Value::Error) return a;
        if (b.kind == Value::Error) return b;
        const std::string& op = n.str;
        if (op == "&") return Value::text(toText(a) + toText(b));
        if (op == "=" || op == "<>" || op == "<" || op == ">" || op == "<=" || op == ">=") {
            int cmp;
            bool aText = a.kind == Value::String && !a.isEmpty(), bText = b.kind == Value::String && !b.isEmpty();
            if (aText && bText) {
                std::string x = upper(a.str), y = upper(b.str);
                cmp = x < y ? -1 : x > y ? 1 : 0;
            } else if (aText != bText) {
                cmp = aText ? 1 : -1;  // text sorts after numbers
            } else {
                bool e = false;
                std::string code;
                double x = toNumber(a, e, code), y = toNumber(b, e, code);
                cmp = x < y ? -1 : x > y ? 1 : 0;
            }
            bool r = op == "=" ? cmp == 0 : op == "<>" ? cmp != 0 : op == "<" ? cmp < 0 : op == ">" ? cmp > 0 : op == "<=" ? cmp <= 0 : cmp >= 0;
            return Value::boolean(r);
        }
        bool err = false;
        std::string code;
        double x = toNumber(a, err, code), y = toNumber(b, err, code);
        if (err) return Value::error(code);
        double r;
        if (op == "+") r = x + y;
        else if (op == "-") r = x - y;
        else if (op == "*") r = x * y;
        else if (op == "/") { if (y == 0) return Value::error("#DIV/0!"); r = x / y; }
        else if (op == "^") r = std::pow(x, y);
        else return Value::error("#VALUE!");
        if (!std::isfinite(r)) return Value::error("#NUM!");
        return Value::number(r);
    }
    case Node::Func: return evalFunc(n, sheet);
    }
    return Value::error("#VALUE!");
}

namespace {

// numbers from arguments: ranges contribute their numeric cells only, scalars are converted
bool gather(const std::vector<Value>& args, std::vector<double>& out, std::string& code) {
    for (const auto& a : args) {
        if (a.kind == Value::Array) {
            for (const auto& row : *a.arr)
                for (const auto& c : row) {
                    if (c.kind == Value::Error) { code = c.str; return false; }
                    if (c.kind == Value::Number) out.push_back(c.num);
                }
        } else if (a.kind == Value::Error) {
            code = a.str;
            return false;
        } else if (a.kind != Value::Empty) {
            bool err = false;
            double v = toNumber(a, err, code);
            if (err) return false;
            out.push_back(v);
        }
    }
    return true;
}

double roundDigits(double x, double d, int mode) {
    double f = std::pow(10.0, std::trunc(d));
    double y = x * f;
    if (mode == 0) y = std::round(y);
    else if (mode == 1) y = y >= 0 ? std::ceil(y - 1e-9) : std::floor(y + 1e-9);
    else y = std::trunc(y);
    return y / f;
}

} // namespace

Value Model::evalFunc(const Node& n, int sheet) {
    std::string f = baseName(n.str);
    auto arg = [&](size_t i) { return i < n.args.size() ? eval(*n.args[i], sheet) : Value(); };
    auto num = [&](size_t i, double& v, std::string& code) {
        bool err = false;
        v = toNumber(arg(i), err, code);
        return !err;
    };
    auto truthy = [&](const Value& v, bool& err, std::string& code) { return toNumber(v, err, code) != 0; };

    if (const FloFunction* fl = findFlo(f)) {
        std::vector<Value> args;
        for (size_t i = 0; i < n.args.size(); ++i) {
            Value v = arg(i);
            if (v.kind == Value::Error && fl->kind != FloFunction::Output) return v;
            args.push_back(v);
        }
        try {
            return Value::number(evalFlo(*fl, args));
        } catch (const std::exception&) {
            return Value::error("#VALUE!");
        }
    }
    if (f == "IF") {
        bool err = false;
        std::string code;
        Value c = arg(0);
        if (c.kind == Value::Error) return c;
        bool t = truthy(c, err, code);
        if (err) return Value::error(code);
        if (t) return n.args.size() > 1 ? arg(1) : Value::boolean(true);
        return n.args.size() > 2 ? arg(2) : Value::boolean(false);
    }
    if (f == "IFERROR") {
        Value v = arg(0);
        return v.kind == Value::Error ? arg(1) : v;
    }
    if (f == "ISERROR") return Value::boolean(arg(0).kind == Value::Error);
    if (f == "ISNUMBER") return Value::boolean(arg(0).kind == Value::Number);
    if (f == "TRUE") return Value::boolean(true);
    if (f == "FALSE") return Value::boolean(false);
    if (f == "PI") return Value::number(3.14159265358979323846);
    if (f == "CHOOSE") {
        double i;
        std::string code;
        if (!num(0, i, code)) return Value::error(code);
        size_t k = size_t(std::trunc(i));
        if (k < 1 || k >= n.args.size()) return Value::error("#VALUE!");
        return arg(k);
    }

    std::vector<Value> args;
    for (size_t i = 0; i < n.args.size(); ++i) args.push_back(arg(i));
    std::string code;
    if (f == "AND" || f == "OR") {
        std::vector<double> v;
        if (!gather(args, v, code)) return Value::error(code);
        if (v.empty()) return Value::error("#VALUE!");
        bool r = f == "AND";
        for (double x : v) r = f == "AND" ? (r && x != 0) : (r || x != 0);
        return Value::boolean(r);
    }
    if (f == "SUM" || f == "AVERAGE" || f == "MIN" || f == "MAX" || f == "COUNT" || f == "PRODUCT") {
        std::vector<double> v;
        if (f == "COUNT") {
            double c = 0;
            for (const auto& a : args) {
                if (a.kind == Value::Array) { for (const auto& r : *a.arr) for (const auto& x : r) c += x.kind == Value::Number; }
                else if (a.isNumeric()) c += 1;
            }
            return Value::number(c);
        }
        if (!gather(args, v, code)) return Value::error(code);
        if (f == "SUM") { double s = 0; for (double x : v) s += x; return Value::number(s); }
        if (f == "PRODUCT") { double s = 1; for (double x : v) s *= x; return Value::number(v.empty() ? 0 : s); }
        if (f == "AVERAGE") {
            if (v.empty()) return Value::error("#DIV/0!");
            double s = 0; for (double x : v) s += x; return Value::number(s / v.size());
        }
        if (v.empty()) return Value::number(0);
        return Value::number(f == "MIN" ? *std::min_element(v.begin(), v.end()) : *std::max_element(v.begin(), v.end()));
    }
    if (f == "NPV") {
        double r;
        if (!num(0, r, code)) return Value::error(code);
        std::vector<double> v;
        std::vector<Value> rest(args.begin() + 1, args.end());
        if (!gather(rest, v, code)) return Value::error(code);
        double s = 0;
        for (size_t i = 0; i < v.size(); ++i) s += v[i] / std::pow(1 + r, double(i + 1));
        return Value::number(s);
    }
    if (f == "SUMPRODUCT") {
        if (args.empty()) return Value::error("#VALUE!");
        std::vector<std::vector<double>> cols;
        size_t len = 0;
        for (const auto& a : args) {
            std::vector<double> v;
            if (a.kind == Value::Array) {
                for (const auto& r : *a.arr) for (const auto& x : r) {
                    if (x.kind == Value::Error) return x;
                    v.push_back(x.kind == Value::Number ? x.num : 0);
                }
            } else {
                bool err = false;
                v.push_back(toNumber(a, err, code));
                if (err) return Value::error(code);
            }
            if (!cols.empty() && v.size() != len) return Value::error("#VALUE!");
            len = v.size();
            cols.push_back(v);
        }
        double s = 0;
        for (size_t i = 0; i < len; ++i) { double p = 1; for (auto& c : cols) p *= c[i]; s += p; }
        return Value::number(s);
    }
    // scalar math
    std::vector<double> x(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        bool err = false;
        x[i] = args[i].kind == Value::Empty ? 0 : toNumber(args[i], err, code);
        if (err) return Value::error(code);
    }
    auto need = [&](size_t k) { return x.size() >= k; };
    double r;
    if (f == "NOT" && need(1)) return Value::boolean(x[0] == 0);
    else if (f == "ABS" && need(1)) r = std::fabs(x[0]);
    else if (f == "SQRT" && need(1)) { if (x[0] < 0) return Value::error("#NUM!"); r = std::sqrt(x[0]); }
    else if (f == "EXP" && need(1)) r = std::exp(x[0]);
    else if (f == "LN" && need(1)) { if (x[0] <= 0) return Value::error("#NUM!"); r = std::log(x[0]); }
    else if (f == "LOG10" && need(1)) { if (x[0] <= 0) return Value::error("#NUM!"); r = std::log10(x[0]); }
    else if (f == "LOG" && need(1)) {
        double base = x.size() > 1 ? x[1] : 10;
        if (x[0] <= 0 || base <= 0 || base == 1) return Value::error("#NUM!");
        r = std::log(x[0]) / std::log(base);
    }
    else if (f == "POWER" && need(2)) r = std::pow(x[0], x[1]);
    else if (f == "MOD" && need(2)) { if (x[1] == 0) return Value::error("#DIV/0!"); r = x[0] - x[1] * std::floor(x[0] / x[1]); }
    else if (f == "INT" && need(1)) r = std::floor(x[0]);
    else if (f == "TRUNC" && need(1)) r = roundDigits(x[0], x.size() > 1 ? x[1] : 0, 2);
    else if (f == "SIGN" && need(1)) r = (x[0] > 0) - (x[0] < 0);
    else if (f == "ROUND" && need(1)) r = roundDigits(x[0], x.size() > 1 ? x[1] : 0, 0);
    else if (f == "ROUNDUP" && need(1)) r = roundDigits(x[0], x.size() > 1 ? x[1] : 0, 1);
    else if (f == "ROUNDDOWN" && need(1)) r = roundDigits(x[0], x.size() > 1 ? x[1] : 0, 2);
    else if ((f == "CEILING" || f == "FLOOR") && need(1)) {
        double s = x.size() > 1 ? x[1] : 1;
        if (s == 0) return Value::number(0);
        r = f == "CEILING" ? std::ceil(x[0] / s) * s : std::floor(x[0] / s) * s;
    }
    else return Value::error("#NAME?");
    if (!std::isfinite(r)) return Value::error("#NUM!");
    return Value::number(r);
}

} // namespace mc::formula
