# MonteCalc — Monte Carlo simulation for LibreOffice Calc (MC FLO workflow)

MonteCalc is a LibreOffice Calc extension written in C++. It implements the
workflow, ribbon functions and spreadsheet functions described in the *MC FLO*
user guide (July 2026), using MC FLO's function names (`FLOsimula_…`, `fMC_…`)
and variable conventions (`FLO_i_`, `FLO_c_`, `FLO_o_`, `FLO_r_`).

It is an independent implementation written from the published guide. It
contains no MC FLO code and is not affiliated with MC FLOsim; "MC FLO" is
named only to describe compatibility with the guide.

## Quick start

```bash
make            # builds build/MonteCalc.oxt (needs the LibreOffice SDK)
make install    # unopkg add (run as a normal user)
```

Open `examples/business_case.ods`. MonteCalc adds the MC FLO ribbon to Calc in four places,
all running the same commands:

- the **MC FLO sidebar deck** (sidebar icon on the right, or *View → Sidebar*). It has one
  panel per ribbon group: *Model specification* (Choose continuous / discrete variable / time
  series, Choose Wizard, Insert correlation, Insert output variable, Register variables,
  Lock/Unlock, Fit distribution), *Information*, *Simulation* (Best-case, Real-case,
  Worst-case, Iterations, Use correlations, Visualize simulation, Multiple simulation, Start,
  Reset), *Statistics* (Variable, Show statistical parameters), *Correlations and
  sensitivities* (X-/Y-variable, Calc correlations), *Individual analysis of iteration*,
  *Results* (Close and save results) and *Other*;
- the **Extension** tab of the tabbed interface (*View → User Interface → Tabbed*);
- the **MC FLO** toolbar, with icons and text;
- the **MC FLO** menu, whose submenus follow the ribbon groups and include the distribution
  pickers.

![MC FLO sidebar deck, Extension tab and correlation window](docs/screenshots/mcflo_interface.png)

1. **Choose continuous variable** (or the Wizard) picks a distribution. Read values off the
   draggable **magic stripe**, then *Save into the selected cell*. *Fit distribution* fits
   historical data instead.
2. **Insert correlation** opens the correlation window described in the guide:
   - pick Variable 1 and 2, the coefficient (type it, or use the slider) and the correlation
     type 0–8;
   - the scatter shows up to 1,000 simulated points. Points in the joint 5%/95% tails are red
     and green, and the resulting rank correlation appears in grey under the coefficient;
   - *OK* writes `=FLOsimula_Correlation("Price";"Investment";0.6;4)` into the cell, names it
     `FLO_c_…` and colours it;
   - pairs that are already correlated, and breaches of the rule that Archimedean copulas are
     bivariate, are reported in the window.

   **Insert output variable** writes `=B30+FLOsimula_output("NPV")`.
3. **Register variables** names unnamed inputs (from the label to the left), creates the
   `FLO_i_ / FLO_c_ / FLO_o_` named ranges and colours the cells.
4. Set **Iterations**, **Best/Real/Worst-case** and **Use correlations** in the Simulation
   panel (or *Settings…* for the seed and Memory Calculation), then **Start**.
5. Analyse in the result window, the result workbook (*Store results*), the
   `fMC_` functions, the influence diagram or the AI mentor.

## What is implemented

**Model functions (`FLOsimula_`):**
- Every distribution and process on the guide's specification page: 19
  continuous distributions plus UserDefined, 7 discrete plus
  DiscreteUserDefined, and AR(1), MA(1), ARMA(1,1), ARCH(1) and the Wiener
  process.
- The options Min / Max (truncation), Round and Number (compound sums, or time
  periods for processes).
- `FLOsimula_Bounded`, `FLOsimula_output` and `FLOsimula_Correlation`.

**Correlations:** normal, Student-t, Clayton, r-Clayton, Gumbel, r-Gumbel,
asy. Gumbel, asy. Fréchet and asy. Triangular. Each is calibrated to the
requested rank correlation. Inconsistent matrices are repaired with a warning,
and the guide's rule that Archimedean copulas are bivariate is enforced.

**Registration (as in the guide):**
- `FLO_i_` / `FLO_c_` / `FLO_o_` names and cell colours.
- Unnamed inputs such as `FLOsimula_DiscreteUniform(300;500)`.
- *Register variables*, *Show*, *Lock/Unlock* (red cells) and *Multiple
  simulation* (Variant / Value / Reference rows, grey cells).

**Simulation:**
- *Start*, *Reset*, the `# iterations` and *Seed* settings (seed −1 =
  preview mode), *Visualize simulation* and worst/real/best scenarios.
- **Memory Calculation**: an in-memory formula engine, about 5× faster on the
  test model, that falls back to spreadsheet recalculation when a function
  isn't supported.

**Result window:**
- Histogram with magic stripe (drag it, or enter `x` or `;p`), thresholds and
  confidence, worst/best markers.
- Cumulative curve, tornado (click a bar for details), spider (isolated and
  cumulative), scatter with tail highlighting and regression line, dynamic
  sensitivity.
- Variant comparison, decision matrix (target, ambition level, probability,
  most urgent control effect) and a statistics grid.
- Navigation between variables.
- *Store results*, *Export image*, and *Store as comment*, which saves the
  diagram as the cell comment's background image.

**Result workbook:**
- Data (write-protected), Summary (scenarios, 5% steps, 1% extremes, ±3% around
  the real case), Statistics (histogram, cumulative curve, worst/best and
  0-limit, CI of the mean), Sensitivity (tornado) and Variants sheets.
- *Change histogram* (set bins).
- *Correlation and sensitivities*: an X/Y regression sheet whose new X goes in
  **I4**, giving the estimate in **I5** and the 95% forecast interval in
  **K5/L5**, plus correlation by 10% quantile of Y and LINEST output from
  **D52**.
- *Analyse selected iteration*, which writes an iteration's input values into
  the model and restores the formulas afterwards.
- *Close and save results* (`<model>_dd_mm_yyyy_hh_mm_ss_MC_FLO.ods`).

**Fitting and forecasting:**
- *Fit distribution*: least squares against the empirical CDF, with KS test,
  the AIC window, *prefer higher values* (beta) and *Insert formula into
  current cell*.
- *Fit time series*: AR / MA / ARMA / ARCH / Wiener ranked by AIC, Theil's U,
  a simulated path with forecast band, and a Holt-Winters comparison.
- *Forecast-Tool*, the `fMC_Batch_Time` / `fMC_Batch_HW_D/M/Q_Time` markers and
  *Start Batch*, which writes the estimated history plus forecasts and adds a
  comment with date and U-test.

**Tool functions (`fMC_`):** all tool, regression, Bayesian (`fMC_Bayes`,
`fMC_Bayes4/4V`, MCMC/MCMCA/V/AV, `…V_Robust`, BayesCombine, BayesPrediction,
Kernel/Kernel2, BayesClassifier, BayesFactor, the Bayesian regression family)
and matrix functions listed in the guide. That's 132 functions in total, listed
in **[docs/FUNCTIONS.md](docs/FUNCTIONS.md)**.

**Other tools:** Sort asc/des, Excel-Check (a model check workbook covering
volatile and lookup functions, non-principal FLO formulas, constants in
formulas, empty references, unnamed inputs, duplicate names and unknown
correlation partners), Use DIP (`FLO_r_` arrays), Correlogram and Hide/Load
formulas.

**Influence diagram:**
- Nodes for inputs, relationships and outputs, with arrows derived from the
  formulas (traced through intermediate cells).
- Select, hover tooltip, drag with reflow, double-click to jump to the cell.
- Context menu and the shortcuts F5, Ctrl+R/L/S/E/F/T/G/D/I/H/K/J, Ctrl+Shift+C
  and Ctrl+M.
- Distributions inside nodes, sensitivity-weighted arrows, focus on selection,
  group view (`Event_1…n` → module), orthogonal arrows.
- Export as PNG/JPEG/BMP at twice screen resolution.
- Formula editor in Edit mode: move a reference by columns/rows, or retype the
  formula.
- A Results tab with overlaid histograms and cumulative curves, a probe and a
  statistics grid.

**FLO_Context sheet:** the guide's columns, with Depends On traced through
intermediate cells.

**AI mentor:**
- A chat window that sends Claude the model's structure and statistics (not the
  cells).
- Uses `claude-opus-5-5` through the Anthropic Messages API, with the
  server-side refusal fallback enabled.
- The API key is read from `Documents/MC_FLO_API.txt`.
- Copy the conversation, or export it to HTML, Word or PDF.

## Where the guide is not specific: MonteCalc's choices

| Topic | Guide | MonteCalc |
|---|---|---|
| Names of most distribution functions | only `FLOsimula_DiscreteUniform`, `_UserDefined`, `_Bounded`, `_output` are spelled out | `FLOsimula_` + the spec-page name: `Triangle`, `PERT`, `Chi2`, `StudentT`, `ArcSin`, `Pascal`, `HyperGeometric`, `TruncatedNormal`, `WienerProcess`, `AR1`, `MA1`, `ARMA11`, `ARCH1` |
| Argument order | name, parameters, options Min/Max/Round, Number | `([Name;] parameters…; [Min]; [Max]; [Round]; [Number])`; Name is detected as text, so `FLOsimula_DiscreteUniform(300;500)` works as in the guide. UserDefined keeps the guide's `(Data; NumberOfColumns; Name)` |
| Correlation formula and type codes | names 8 types (0–7) without a formula name | `FLOsimula_Correlation(Name1; Name2; Coefficient; [Type])`, types 0–8 (the r- variants of Clayton and Gumbel get their own codes) |
| Asymmetric copulas | named only | asy. Gumbel and asy. Triangular = Khoudraji asymmetrisations of Gumbel and Clayton; asy. Fréchet = Marshall–Olkin |
| `FLOsimula_Bounded` | "can be resembled by bounds" | limits the value to the barriers; use Min/Max for exact truncation |
| `fMC_BayesV` | referenced, not specified | `(Name1; Name2; Comparison2; Value2; Percentile)` |
| `fMC_BayesPrediction` | Count, tolerable unsuccessful Attempts | negative-binomial predictive per posterior draw; values above Attempts are reported as Attempts+1 |
| `fMC_BayesMCMC*` | prior + "new data of the same type" | target density ∝ product of the two kernel-estimated densities |
| Bayesian regression | prior from a small subsample, bootstrap likelihood | Bayesian bootstrap (Dirichlet-weighted least squares) pooled over `kfolds` random subsets |
| `fMC_PortfolioMatrix` Compare arguments | not described | ignored |
| Volatile `…V` functions | recalculate on every change | aliases (Calc recalculates dependent cells anyway). Deterministic data functions are cached on their arguments, because Calc recalculates every formula in every iteration |
| `_MC` function variants | not listed | Memory Calculation is a run setting instead |
| Fit: "Degree of freedom", "increment" | grid for the fit | not used: all parameters are fitted by Nelder–Mead |
| "Always on top" (influence diagram) | menu toggle | windows always stay in front of their Calc window |
| Ribbon | an Excel ribbon tab "MC FLO" | LibreOffice extensions cannot add a ribbon tab of their own, so the ribbon groups become the MC FLO sidebar deck (with the input fields), the *Extension* tab of the tabbed interface (text buttons), a toolbar and a menu |
| Correlation window | Correlation type 0–7 | 0–8 (r-Clayton and r-Gumbel have their own codes); the scatter is drawn from the inputs' preview samples coupled with the chosen copula |
| Random data tools | — | fixed seed, so results don't change between recalculations |

The guide's `fMC_GetOuputName` typo is kept as written; `fMC_GetOutputName`
is provided too. UNO IDL forbids identifiers that start with a lower-case
letter and contain `_`, so the programmatic names are capitalised internally
(`FMC_Average`). The function wizard shows the guide's spelling, and Calc
matches function names case-insensitively.

## Architecture

```
core/   C++17, no LibreOffice dependency (1400 unit-test checks)
        distributions, copulas, time series, statistics, fitting (Nelder-Mead, KS/AIC,
        Theil's U, Holt-Winters), Bayesian tools, flofuncs (FLOsimula_ semantics shared by
        the add-in and the memory engine), formula (parser/evaluator), simulation state
uno/    addin (132 functions, generated), engine (all commands), model (cell scanning,
        named ranges), report (result workbook), deps (variable dependencies), painter/
        charts/canvaswin (drawing, modeless windows), windows (result window, wizard,
        correlation window, fitting, correlogram, variable list, context), influence (diagram),
        mentor (Claude), sidebar (MC FLO deck: one panel per ribbon group)
tools/  gen_functions.py: one table generating IDL, C++, wizard metadata and docs
        gen_ui.py (+ gen_icons.py): one ribbon table generating the menu, toolbar, Notebookbar
        tab, sidebar deck registration and icons
tests/  core_tests.cpp, integration_test.py (headless LibreOffice, run as a normal user)
```

A run works in four steps:

1. **Register.** One recalculation registers the named inputs, outputs and
   correlations, and the engine scans the cells for `FLOsimula_` formulas.
2. **Prepare.** Correlated uniforms are generated per named input.
3. **Simulate.** Each iteration either recalculates Calc or evaluates the model
   in memory, then records the values of all input and output cells. That is
   why an MC FLO function must be the principal function of its cell, as the
   guide says.
4. **Finish.** The results are stored as a result set, one per variant.

## Building and testing

```bash
make && make test      # extension + core unit tests (Linux, Makefile)
cmake -S . -B build && cmake --build build && ctest --test-dir build   # any platform
make itest             # end-to-end test in headless LibreOffice (as a normal user)
```

Requirements: the LibreOffice SDK (`libreoffice-dev`), g++ ≥ 9, Python 3 and
zip. The AI mentor needs libcurl at run time; it is loaded on demand.

**Windows:** see **[docs/WINDOWS.md](docs/WINDOWS.md)** for installation and
building. `CMakeLists.txt` builds with Visual Studio against the official
LibreOffice SDK for Windows, and the GitHub Actions workflow
(`.github/workflows/build.yml`) produces `MonteCalc-windows_x86_64.oxt` and
`MonteCalc-linux_x86_64.oxt` on every push. On Windows the AI mentor uses
WinHTTP; elsewhere it loads libcurl at run time.
