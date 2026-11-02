// MonteCalc - spreadsheet formula parser and in-memory evaluator ("Memory Calculation").
//
// Formulas are read in LibreOffice's API syntax (English function names, ';'
// separators, '.' between sheet and cell, e.g. "=SUM($Model.B2:B9)*2"). The model
// is the dependency closure of the cells the engine needs (inputs and outputs);
// it is evaluated without Calc, which is much faster for large simulations. A model
// that uses a function the evaluator does not know is reported as unsupported so
// that the engine can fall back to Calc's own recalculation.
#pragma once

#include "value.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace mc::formula {

struct CellAddr {
    int sheet = 0, col = 0, row = 0;
    bool operator<(const CellAddr& o) const {
        return sheet != o.sheet ? sheet < o.sheet : col != o.col ? col < o.col : row < o.row;
    }
    bool operator==(const CellAddr& o) const { return sheet == o.sheet && col == o.col && row == o.row; }
};

struct RangeAddr {
    CellAddr a, b;  // a = top-left, b = bottom-right
};

struct CellContent {
    enum Type { Empty, Number, Text, Formula } type = Empty;
    double value = 0;
    std::string text;      // text or formula (with leading '=')
};

class Source {
public:
    virtual ~Source() = default;
    virtual CellContent cell(const CellAddr& a) = 0;
    virtual int sheetIndex(const std::string& name) = 0;           // -1 if unknown
    virtual bool namedRange(const std::string& name, RangeAddr& r) = 0;
};

struct Node {
    enum Kind { Num, Str, Bool, Err, Ref, Range, Unary, Binary, Postfix, Func, Missing } kind;
    double num = 0;
    std::string str;       // string literal, error code, operator or function name (upper case)
    RangeAddr range;       // Ref uses range.a
    std::vector<std::shared_ptr<Node>> args;
};
using NodePtr = std::shared_ptr<Node>;

// Parses a formula (with or without the leading '='). Throws std::invalid_argument.
NodePtr parse(const std::string& formula, int currentSheet, Source* src);

// Column letters <-> index (A = 0)
std::string columnName(int col);
std::string cellName(const CellAddr& a, bool withSheet, const std::function<std::string(int)>& sheetName);
bool parseCellName(const std::string& s, int& col, int& row);

// All cell/range references in a formula tree.
void collectRanges(const NodePtr& n, std::vector<RangeAddr>& out);
// All function calls (upper-case names, prefix stripped) in a formula tree.
void collectCalls(const NodePtr& n, std::vector<const Node*>& out);
std::string baseName(const std::string& funcName);   // strips "ORG.MONTECALC.MONTECALC." etc.

// Names of the built-in functions the evaluator supports.
const std::set<std::string>& supportedFunctions();

class Model {
public:
    // Reads the dependency closure of `roots`. Returns false (with a reason) when a
    // formula uses something the evaluator does not support.
    bool build(Source& src, const std::vector<CellAddr>& roots, std::string& why);
    void beginIteration() { ++stamp_; }
    Value value(const CellAddr& a);
    size_t formulaCount() const { return formulas_.size(); }

private:
    struct Cell {
        CellContent content;
        NodePtr tree;
        Value cache;
        unsigned long stamp = 0;
        bool evaluating = false;
    };
    Value eval(const Node& n, int sheet);
    Value evalFunc(const Node& n, int sheet);
    Value rangeValue(const RangeAddr& r);
    Cell* cellAt(const CellAddr& a);
    std::map<CellAddr, Cell> cells_;
    std::set<CellAddr> formulas_;
    std::map<CellAddr, int> sheetOf_;
    unsigned long stamp_ = 1;
    Source* src_ = nullptr;
};

} // namespace mc::formula
