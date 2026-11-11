# MonteCalc for Microsoft Excel (XLL)

The MonteCalc functions are also available as an Excel add-in, built from the same source code
as the LibreOffice extension.

## Install

1. Download the file for your Office version from the
   [latest release](https://github.com/EstaciondelSilencio/montecalc/releases/latest):
   - `MonteCalc-Excel-x64.xll` for 64-bit Office (the default since Office 2019 / Microsoft 365);
   - `MonteCalc-Excel-x86.xll` for 32-bit Office.

   To see which one you have, go to *File → Account → About Excel*. The first line ends with
   "64-bit" or "32-bit".
2. Windows may block downloaded add-ins. Right-click the file, choose *Properties*, tick
   *Unblock* and click *OK*.
3. In Excel go to *File → Options → Add-ins*. At the bottom choose *Manage: Excel Add-ins → Go… →
   Browse…*, select the `.xll` file and confirm. The add-in then loads every time Excel starts.

No Visual C++ runtime or other installation is needed.

## Use

Open `examples/business_case.xlsx`, which is also attached to the release. Go to the **Add-ins** tab and
choose **MC FLO → Start simulation**. The results area (rows 34–42) fills in.

- **Model functions:** all `FLOsimula_…` functions, with the same names and argument order as in
  MC FLO and the LibreOffice version, for example `=FLOsimula_PERT(200000,350000,250000,"MarketSize")`.
  Excel separates arguments with `,`, or `;` in German and Spanish Excel.
- **Outputs:** mark a result with `=B29+FLOsimula_output("NPV")`, or select the cell and choose
  *MC FLO → Insert output variable…*.
- **Correlations:** use `=FLOsimula_correlation("Share","Price",-0.6,0)`. The type codes are those
  of MC FLO.
- **Results:** all `fMC_…` functions. *Formulas → Insert Function* shows them in the MC FLO
  categories: Descriptive Statistics, Relationship functions, Portfolio/Financial Functions,
  Parameter-free statistics, Classical Linear Regression, Generative/Bayesian statistics, Other
  functions, and MC FLO Model functions for the `FLOsimula_` functions. The Function Wizard shows
  a description of each function and of each argument.
- **Array results:** functions that return a table spill automatically in Microsoft 365 and Excel 2021.
  In older versions, select the output range and confirm with Ctrl+Shift+Enter.

### MC FLO menu (Add-ins tab)

| Entry | What it does |
|---|---|
| Start simulation | Runs the simulation. A first calculation finds the input and output cells, then every iteration recalculates the workbook and records the variables. Esc cancels. At the end a summary is shown, and the `fMC_` functions show the results. |
| Reset | Forgets the results. |
| Settings… | Iterations, seed (0 = random, −1 = preview mode), worst/real/best case levels and *use correlations*. The values are stored in the workbook as hidden names `MonteCalc_Iterations`, `MonteCalc_Seed`, … |
| Insert output variable… | Adds `+FLOsimula_output("Name")` to the selected cell. |
| Language: Auto / English / Deutsch / Español | Language of the menu, messages, dialogs and the Function Wizard categories. "Auto" follows the Windows display language. The choice is shared with the LibreOffice version (`Documents\MC_FLO_language.txt`). |
| About MonteCalc | Version information. |

The commands are also macros (`MCFLO.Start`, `MCFLO.Reset`, `MCFLO.Settings`, …). You can call
them from VBA with `Application.Run "MCFLO.Start"`.

## Differences from the LibreOffice version

The spreadsheet functions are identical: same code, same results for the same seed. The
LibreOffice windows are built on LibreOffice's own user interface toolkit, so they are not part of
the Excel add-in:
- the result window (histogram, tornado chart, decision matrix, scatter plots);
- the wizards;
- the influence diagram;
- the correlation window;
- the AI mentor.

The Excel add-in also does not include multiple simulation (variants), Memory Calculation or the
result workbook. In Excel, use the `fMC_` functions and Excel's own charts for the results.

Function descriptions in the Function Wizard are in English. The menu, messages, dialogs and
category names follow the chosen language.

## Build

On Windows, in the *x64* (or *x86*) *Native Tools Command Prompt for VS*:

```
cmake -S xll -B build-xll -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-xll
```

On Linux:
- `make xlltest` runs the add-in against a mock Excel (`tests/xll_test.cpp`). It covers
  registration, the menu, a full simulation, the result functions, arrays, errors, settings and
  the languages.
- `make xll` cross-compiles the 64-bit `.xll` with mingw-w64.
