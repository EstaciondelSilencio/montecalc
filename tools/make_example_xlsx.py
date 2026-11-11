#!/usr/bin/env python3
"""Writes examples/business_case.xlsx: the business case of examples/business_case.ods for Microsoft
Excel with the MonteCalc XLL (same rows, taken from tools/make_example.py; formulas with "," as
separator; simulation settings as hidden workbook names MonteCalc_*).

Usage: make_example_xlsx.py [examples/business_case.xlsx]   (needs openpyxl)
"""
import ast
import os
import sys

from openpyxl import Workbook
from openpyxl.styles import Font
from openpyxl.workbook.defined_name import DefinedName

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "examples", "business_case.xlsx")


def rows_of_ods_example():
    tree = ast.parse(open(os.path.join(HERE, "make_example.py"), encoding="utf-8").read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "ROWS":
            return ast.literal_eval(node.value)
    raise SystemExit("ROWS not found in make_example.py")


def xl(formula):
    """LibreOffice argument separator ; -> Excel ,  (outside of strings)"""
    out, in_str = [], False
    for ch in formula:
        if ch == '"':
            in_str = not in_str
        out.append("," if ch == ";" and not in_str else ch)
    return "".join(out)


def main():
    wb = Workbook()
    sh = wb.active
    sh.title = "Model"
    bold = Font(bold=True)
    for r, (label, f, note) in enumerate(rows_of_ods_example(), start=1):
        sh.cell(r, 1, label)
        if isinstance(f, str):
            sh.cell(r, 2, xl(f))
        elif f is not None:
            sh.cell(r, 2, f)
        if note:
            sh.cell(r, 3, note)
    top = 21   # 1-based row of "Cash flow" (row 20 0-based in make_example.py)
    sh.cell(top, 1, "Cash flow")
    for j, h in enumerate(["Year", "Units", "Revenue", "Variable cost", "Fixed cost", "Cash flow"], start=1):
        sh.cell(top + 1, j, h)
    for y in range(1, 6):
        row = top + 1 + y
        sh.cell(row, 1, y)
        sh.cell(row, 2, f"=IF(AND(A{row}=1,$B$9=1),0,$B$4*(1+$B$10)^(A{row}-1)*$B$5)")
        sh.cell(row, 3, f"=B{row}*$B$6")
        sh.cell(row, 4, f"=B{row}*$B$7")
        sh.cell(row, 5, "=$B$17")
        sh.cell(row, 6, f"=C{row}-D{row}-E{row}")
    first, last = top + 2, top + 6
    # same rows as examples/business_case.ods: NPV in B29, outputs in B30:B31, results from row 34
    npv = last + 2
    sh.cell(npv, 1, "Net present value")
    sh.cell(npv, 2, f"=NPV($B$18,F{first}:F{last})-$B$8")
    sh.cell(npv + 1, 1, "Output: NPV")
    sh.cell(npv + 1, 2, f'=B{npv}+FLOsimula_output("NPV")')
    sh.cell(npv + 2, 1, "Output: total cash flow")
    sh.cell(npv + 2, 2, f'=SUM(F{first}:F{last})+FLOsimula_output("TotalCash")')
    res = npv + 4
    sh.cell(res, 1, "Results (filled after MC FLO > Start simulation)")
    results = [
        ("Expected NPV", '=fMC_Average("NPV")'),
        ("Standard deviation", '=fMC_StdS("NPV")'),
        ("Worst case (5%)", '=fMC_Percentile("NPV",fMC_Worst())'),
        ("Best case (95%)", '=fMC_Percentile("NPV",fMC_Best())'),
        ("Probability NPV > 0", '=IF(fMC_Iterations()>0,1-fMC_PercentileValue("NPV",0),"")'),
        ("Expected shortfall (5%)", '=fMC_ExpectedShortfall("NPV",0.05)'),
        ("Main driver", '=fMC_CorrRankN("NPV",1)'),
        ("Its correlation", '=fMC_CorrRank("NPV",1)'),
        ("P(NPV > 0 | price >= 25)", '=fMC_Bayes("NPV",">",0,"Price",">=",25)'),
    ]
    for i, (label, f) in enumerate(results, start=1):
        sh.cell(res + i, 1, label)
        sh.cell(res + i, 2, f)
    formats = [("B4", "#,##0"), ("B5", "0.0%"), ("B6", "0.00"), ("B7", "0.00"), ("B8", "#,##0"), ("B10", "0.0%"), ("B17", "#,##0"),
               ("B18", "0.0%")]
    for addr, fmt in formats:
        sh[addr].number_format = fmt
    for r in range(first, last + 1):
        for c in range(2, 7):
            sh.cell(r, c).number_format = "#,##0"
    for r in range(npv, res + 7):
        sh.cell(r, 2).number_format = "#,##0"
    sh.cell(res + 5, 2).number_format = "0.0%"
    sh.cell(res + 8, 2).number_format = "0.00"
    sh.cell(res + 9, 2).number_format = "0.0%"
    for r in (1, 3, 12, 16, top, res):
        sh.cell(r, 1).font = bold
    sh.column_dimensions["A"].width = 34
    sh.column_dimensions["B"].width = 16
    sh.column_dimensions["C"].width = 60
    for col in "DEF":
        sh.column_dimensions[col].width = 14
    # simulation settings (read by MC FLO > Start simulation; changed with MC FLO > Settings)
    for k, v in [("Iterations", 5000), ("Seed", 2026), ("Worst", 0.05), ("Real", 0.5), ("Best", 0.95), ("Bins", 20), ("UseCorrelations", 1)]:
        wb.defined_names["MonteCalc_" + k] = DefinedName("MonteCalc_" + k, attr_text=str(v), hidden=True)
    wb.save(OUT)
    print("wrote", os.path.relpath(OUT))


if __name__ == "__main__":
    main()
