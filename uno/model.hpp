// MonteCalc - access to the model document: finding MC FLO variables in cells,
// named ranges (FLO_i_ / FLO_c_ / FLO_o_), selection and cell helpers.
#pragma once

#ifdef MONTECALC_XLL
#include "../xll/uno_shim.hpp"
#else
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/table/CellAddress.hpp>
#include <com/sun/star/table/CellRangeAddress.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#endif

#include "../core/flofuncs.h"
#include "../core/formula.h"
#include "../core/simulation.h"

#include <memory>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace montecalc {

namespace css = com::sun::star;

struct VarCell {
    std::string name;
    mc::VarKind kind = mc::VarKind::Input;
    css::table::CellAddress addr;
    std::string function;     // e.g. FLOSIMULA_TRIANGLE
    std::string description;  // e.g. Triangle(1; 2; 3)
    std::string formula;
    bool named = false;       // name given in the formula
    std::string corrA, corrB; // correlation partners
};

enum class CellType { Empty, Value, Text, Formula };

// One cell of the model. Formulas use LibreOffice syntax in both programs ("; " separators,
// Sheet.A1 references); the Excel version converts to and from Excel's syntax.
std::string u8s(const rtl::OUString& s);   // = u8() (util.hpp)

class Cell {
public:
    Cell() = default;
    std::string formula() const;
    void setFormula(const std::string& f) const;
    double value() const;
    void setValue(double v) const;
    std::string text() const;
    void setText(const std::string& t) const;
    void setTextU(const rtl::OUString& t) const { setText(u8s(t)); }
    void setFormula(const rtl::OUString& f) const { setFormula(u8s(f)); }
    CellType type() const;
    int error() const;   // 0 = no error
#ifdef MONTECALC_XLL
    explicit Cell(std::shared_ptr<struct XlCellImpl> c) : c_(std::move(c)) {}
    std::shared_ptr<struct XlCellImpl> impl() const { return c_; }
private:
    std::shared_ptr<struct XlCellImpl> c_;
#else
    explicit Cell(const css::uno::Reference<css::table::XCell>& c) : c_(c) {}
    css::uno::Reference<css::table::XCell> xcell() const { return c_; }
private:
    css::uno::Reference<css::table::XCell> c_;
#endif
};

class ModelDoc {
public:
#ifdef MONTECALC_XLL
    ModelDoc();   // the active workbook in Excel
    explicit ModelDoc(const css::uno::Reference<css::frame::XModel>&) : ModelDoc() {}
    bool valid() const;
    css::uno::Reference<css::frame::XModel> model() const { return {}; }
    std::shared_ptr<struct XlDocImpl> impl() const { return x_; }
#else
    explicit ModelDoc(const css::uno::Reference<css::frame::XModel>& m);
    bool valid() const { return doc_.is(); }
    css::uno::Reference<css::frame::XModel> model() const { return model_; }
    css::uno::Reference<css::sheet::XSpreadsheetDocument> doc() const { return doc_; }
    css::uno::Reference<css::sheet::XSpreadsheet> sheet(int i) const;
#endif
    // a new, empty document (model check, result workbook)
    static ModelDoc newDocument(const css::uno::Reference<css::uno::XComponentContext>& ctx, bool hidden);

    std::vector<std::string> sheetNames() const;
    int sheetIndex(const std::string& name) const;
    Cell cell(const css::table::CellAddress& a) const;
    std::string cellName(const css::table::CellAddress& a, bool withSheet = true) const;
    bool parseAddress(const std::string& text, css::table::CellAddress& a) const;  // "B4" or "Sheet.B4"

    // All cells with FLOsimula_ formulas, in sheet/row/column order.
    std::vector<VarCell> scan() const;
    std::string defaultName(const css::table::CellAddress& a) const;   // <Sheet>_<Cell>

    // Named ranges
    void setNamedRange(const std::string& name, const css::table::CellAddress& a);
    void removeNamedRangesWithPrefix(const std::string& prefix);
    void removeNamedRange(const std::string& name);
    bool namedRange(const std::string& name, css::table::CellRangeAddress& r) const;
    std::vector<std::pair<std::string, css::table::CellRangeAddress>> namedRanges(const std::string& prefix) const;

    // Selection
    bool currentCell(css::table::CellAddress& a) const;
    bool selectionRange(css::table::CellRangeAddress& r) const;
    void select(const css::table::CellAddress& a) const;
    void selectRange(const css::table::CellRangeAddress& r) const;
    std::vector<std::vector<double>> selectionValues(std::vector<std::string>* firstCellLabel = nullptr) const;

    void setBackground(const css::table::CellAddress& a, sal_Int32 rgb) const;
    void recalc() const;

    // cells with formulas / numeric constants of a sheet
    std::vector<std::pair<css::table::CellAddress, std::string>> formulaCells(int sheet) const;
    std::vector<css::table::CellAddress> valueCells(int sheet) const;
    // sheets and tables
    int ensureSheet(const std::string& name, bool visible = true);   // index of the (new) sheet at the end
    void renameSheet(int sheet, const std::string& name);
    void removeSheet(const std::string& name);
    // rows of numbers / texts (empty Any = empty cell) written from (row, col) on
    void writeTable(int sheet, int row, int col, const std::vector<std::vector<css::uno::Any>>& rows) const;
    void optimalWidth(int sheet, int firstCol, int nCols) const;
    void setComment(const css::table::CellAddress& a, const std::string& text) const;
    // FLO_r_ ranges: name for column col, rows row0..row1 of a sheet
    void setNamedArea(const std::string& name, int sheet, int col, int row0, int row1);
    // settings stored with the document (LibreOffice: user-defined properties, Excel: hidden names)
    double property(const std::string& name, double def) const;
    std::string propertyText(const std::string& name) const;
    void setProperty(const std::string& name, const css::uno::Any& value) const;   // empty Any removes it
    // result workbook helpers
    int insertSheet(const std::string& name, int pos);   // replaces a sheet of that name
    void numberFormat(int sheet, int c0, int r0, int c1, int r1, const std::string& code) const;
    void protectSheet(int sheet) const;
    void setArrayFormula(int sheet, int c0, int r0, int c1, int r1, const std::string& formula) const;   // LibreOffice syntax, no "="
    void activateSheet(int sheet) const;
    std::string url() const;   // where the document is stored ("" = not saved)
    enum class ChartKind { Column, Line, Tornado };
    // ranges: first = categories / x, others = values; position and size in 1/100 mm
    void addChart(int sheet, const std::string& name, ChartKind kind, const std::vector<css::table::CellRangeAddress>& ranges, int x, int y, int w,
                  int h, const std::string& title, bool legend = false) const;
    void saveAs(const std::string& url) const;   // the result workbook (Excel: .xlsx)
    void close() const;
    // a key identifying the document (per-document state, e.g. the replayed iteration)
    const void* key() const;

    // formula engine adapter
    std::unique_ptr<mc::formula::Source> formulaSource() const;
    std::string variableNameFromArgs(const VarCell& v, const std::vector<std::string>& args,
                                     const css::table::CellAddress& a) const;

private:
#ifdef MONTECALC_XLL
    std::shared_ptr<struct XlDocImpl> x_;
#else
    css::uno::Reference<css::frame::XModel> model_;
    css::uno::Reference<css::sheet::XSpreadsheetDocument> doc_;
#endif
};

// Colours used to mark MC FLO cells (as described in the manual)
constexpr sal_Int32 kColorInput = 0xDDEBF7;
constexpr sal_Int32 kColorCorrelation = 0xFFF2CC;
constexpr sal_Int32 kColorOutput = 0xFCE4D6;
constexpr sal_Int32 kColorLocked = 0xFF9999;
constexpr sal_Int32 kColorVariant = 0xD9D9D9;

// readable number for messages
inline std::string fmtNum(double v) {
    char b[64];
    if (std::fabs(v) >= 1e5 && std::fabs(v) < 1e15) {
        std::snprintf(b, sizeof b, "%.0f", v);
        std::string s = b, out;
        size_t start = s[0] == '-' ? 1 : 0;
        for (size_t i = 0; i < s.size(); ++i) {
            out += s[i];
            size_t rest = s.size() - i - 1;
            if (i >= start && rest > 0 && rest % 3 == 0) out += ',';
        }
        return out;
    }
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

// Position of the Variable_name argument of a FLOsimula_ call given as argument texts
// (MC FLO order: parameters first, then the name).

} // namespace montecalc
