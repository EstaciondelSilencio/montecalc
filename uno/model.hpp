// MonteCalc - access to the model document: finding MC FLO variables in cells,
// named ranges (FLO_i_ / FLO_c_ / FLO_o_), selection and cell helpers.
#pragma once

#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/sheet/XSpreadsheet.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/table/CellAddress.hpp>
#include <com/sun/star/table/CellRangeAddress.hpp>
#include <com/sun/star/table/XCell.hpp>

#include "../core/formula.h"
#include "../core/simulation.h"

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

class ModelDoc {
public:
    explicit ModelDoc(const css::uno::Reference<css::frame::XModel>& m);
    bool valid() const { return doc_.is(); }
    css::uno::Reference<css::frame::XModel> model() const { return model_; }
    css::uno::Reference<css::sheet::XSpreadsheetDocument> doc() const { return doc_; }

    std::vector<std::string> sheetNames() const;
    int sheetIndex(const std::string& name) const;
    css::uno::Reference<css::sheet::XSpreadsheet> sheet(int i) const;
    css::uno::Reference<css::table::XCell> cell(const css::table::CellAddress& a) const;
    std::string cellName(const css::table::CellAddress& a, bool withSheet = true) const;
    bool parseAddress(const std::string& text, css::table::CellAddress& a) const;  // "B4" or "Sheet.B4"

    // All cells with FLOsimula_ formulas, in sheet/row/column order.
    std::vector<VarCell> scan() const;
    std::string defaultName(const css::table::CellAddress& a) const;   // <Sheet>_<Cell>

    // Named ranges
    void setNamedRange(const std::string& name, const css::table::CellAddress& a);
    void removeNamedRangesWithPrefix(const std::string& prefix);
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

    // formula engine adapter
    std::unique_ptr<mc::formula::Source> formulaSource() const;
    std::string variableNameFromArgs(const VarCell& v, const std::vector<std::string>& args,
                                     const css::table::CellAddress& a) const;

private:
    css::uno::Reference<css::frame::XModel> model_;
    css::uno::Reference<css::sheet::XSpreadsheetDocument> doc_;
};

// Colours used to mark MC FLO cells (as described in the manual)
constexpr sal_Int32 kColorInput = 0xDDEBF7;
constexpr sal_Int32 kColorCorrelation = 0xFFF2CC;
constexpr sal_Int32 kColorOutput = 0xFCE4D6;
constexpr sal_Int32 kColorLocked = 0xFF9999;
constexpr sal_Int32 kColorVariant = 0xD9D9D9;

std::string fmtNum(double v);  // readable number for messages

} // namespace montecalc
