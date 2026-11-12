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

Open `examples/business_case.xlsx`, which is also attached to the release. Excel shows an **MC FLO**
tab in the ribbon. Click **Start** in the *Simulation* group. The result window opens and the
results area (rows 34–42) fills in.

- **Model functions:** all `FLOsimula_…` functions, with the same names and argument order as in
  MC FLO and the LibreOffice version, for example `=FLOsimula_PERT(200000,350000,250000,"MarketSize")`.
  Excel separates arguments with `,`, or `;` in German and Spanish Excel.
- **Outputs:** mark a result with `=B29+FLOsimula_output("NPV")`, or select the cell and click
  *Insert output*.
- **Correlations:** `=FLOsimula_correlation("Share","Price",-0.6,0)`, or *Insert correlation*,
  which opens the correlation window. The type codes are those of MC FLO.
- **Results:** all `fMC_…` functions. *Formulas → Insert Function* shows them in the MC FLO
  categories, with a description of each function and argument. The *Functions* menu of the
  ribbon lists them by category as well.
- **Array results:** functions that return a table spill automatically in Microsoft 365 and Excel 2021.
  In older versions, select the output range and confirm with Ctrl+Shift+Enter.

### The MC FLO ribbon tab

The tab has the same groups, commands and icons as the LibreOffice version. The MC FLO logo
is the large *About MonteCalc* button at the end of the tab. It is also the icon of the
MonteCalc windows, dialogs and messages, and of the `.xll` file itself (*Properties → Details*
shows the version).

| Group | Commands |
|---|---|
| Model specification | Continuous, discrete and time-series distribution menus (each opens the wizard), Wizard, Insert correlation, Insert output, Register, Lock, Fit distribution |
| Information | Influence diagram, Functions menu (the `fMC_` functions by MC FLO category, plus the Function Wizard), Model, Show variables, AI mentor |
| Simulation | Best / Real / Worst levels and Iterations (edit boxes), *Use correlations* and *Visualize* check boxes, Multiple simulation (variants), Start, Reset, Settings |
| Statistics | Variable drop-down, Show statistical parameters, Result window, Select variant |
| Correlations | X / Y drop-downs, Calculate correlations, Correlogram |
| Iteration | Iteration number, Replay, Expected values |
| Results | Close and save results (result workbook), Store results, Change histogram |
| Other | Sort ascending / descending, Model check, Forecast tool, Batch, Hide / load formulas, Variables, Write context, Preview, Language (Auto / English / Deutsch / Español), About |

### Windows

The windows are those of the LibreOffice version, drawn natively on Windows:
- result window: histogram, cumulative curve, tornado chart, decision matrix, scatter / regression,
  statistics table, with magic stripe, certainty level and limits;
- distribution wizard with preview, fit window, correlation window and correlogram;
- variable list, influence diagram and AI mentor.

The language of the ribbon, the windows, the messages and the Function Wizard categories follows
the *Language* choice. "Auto" follows the Windows display language. The choice is shared with
the LibreOffice version (`Documents\MC_FLO_language.txt`).

The commands are also macros (`MCFLO.Start`, `MCFLO.Reset`, `MCFLO.Settings`, …). You can call
them from VBA with `Application.Run "MCFLO.Start"`.

### If the MC FLO tab does not appear

The ribbon tab comes from a small COM add-in inside the `.xll`, which registers itself for the
current user when the add-in loads (no administrator rights needed). If the tab is missing, check
*File → Options → Add-ins → Manage: COM Add-ins → Go…* and tick **MonteCalc**. The commands are
also on the **Add-ins** tab (*MC FLO* menu) as a fallback.

## Differences from the LibreOffice version

The functions, windows and commands are the same code with the same results for the same seed.
Saved result files are `.xlsx` instead of `.ods`. Function descriptions in the Function Wizard are
in English. The menu, ribbon, messages, windows and category names follow the chosen language.

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
- `make build/xll/xll_wine_test.exe` builds the ribbon and window test. Run it with Wine or on
  Windows: `xll_wine_test ribbon out.xml`, `xll_wine_test message`, or `xll_wine_test window results|tornado|decision|scatter|wizard|settings 5 [Deutsch]`.
