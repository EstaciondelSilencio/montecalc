# MonteCalc — Monte Carlo simulation for LibreOffice Calc

MonteCalc is a LibreOffice Calc extension, written in C++, for Monte Carlo
simulation inside a spreadsheet. You describe uncertain inputs with spreadsheet
functions, mark the results you care about and run thousands of iterations. Then
you analyse the distributions in a result workbook or with result functions.

Its workflow follows the one described in the *MC FLO* user guide for Excel
(July 2026): inputs as functions, correlations, outputs, scenarios, a result
workbook, tornado charts and replaying single iterations. It is an independent
implementation written from that published description. It contains no MC FLO
code and is not affiliated with MC FLOsim. Function names and signatures are
MonteCalc's own (see [Differences](#differences-from-mc-flo)).

## Quick start

```bash
make            # builds build/MonteCalc.oxt (needs the LibreOffice SDK)
make install    # unopkg add (run as a normal user)
```

Then open `examples/business_case.ods` and choose **MonteCalc → Run simulation…**

1. **Inputs.** Put an uncertain value in a cell as a function, for example
   `=MC_TRIANGULAR("Volume"; 80; 100; 140)`. Between runs the cell shows the
   distribution's expected value, so the model stays readable.
2. **Correlations (optional).** For example, `=MC_CORRELATE("Volume"; "Price"; -0.5)`.
3. **Outputs.** For example, `=MC_OUTPUT("NPV"; B30)`. This returns B30 unchanged
   and records it in every iteration.
4. **Run.** Use **MonteCalc → Run simulation…**. Settings (iterations, seed,
   scenario quantiles, histogram bins) are saved with the document.
5. **Analyse.**
   - The **result workbook** has four sheets: Summary, Charts (histogram and
     cumulative curve per variable), Sensitivity (a tornado chart per output)
     and Data (every iteration, write-protected).
   - **Result functions** work anywhere in the model, for example
     `=MCR_PERCENTILE("NPV"; 0.05)`, `=MCR_TOPDRIVER("NPV"; 1)` or
     `=MCR_BAYES("NPV"; ">"; 0; "Price"; ">="; 25)`.
   - **Analyse iteration…** loads the values of one iteration into the model so
     you can trace the calculation. **Show expected values** switches back.

## Features

| Area | Implemented |
|---|---|
| Continuous distributions | Uniform, Triangular, PERT, Normal, Truncated normal, Lognormal, Beta, Gumbel, Gamma, Exponential, Weibull, Chi-square, Student-t, Pareto, F, Arcsine, Cauchy, Fréchet, Logistic, Custom (raw data, histogram table or quantile table) |
| Discrete distributions | Uniform, Bernoulli, Binomial, Poisson, Negative binomial (Pascal), Geometric, Hypergeometric, Custom (values and probabilities) |
| Distribution options | Lower and upper bounds (exact truncation), compound `Count` (sum of n draws per iteration) |
| Time series | AR(1), MA(1), ARMA(1,1), ARCH(1), Wiener process / geometric Brownian motion, simulated over *n* periods |
| Correlations | Spearman rank correlation through Gaussian, Student-t (4 d.o.f.), Clayton, rotated Clayton, Gumbel and rotated Gumbel copulas. An inconsistent correlation matrix is repaired automatically, with a warning |
| Engine | Registration pass, seeded runs (reproducible), status-bar progress, optional visualised run, lock/unlock an input at its expected value, replay any iteration, random preview mode, reset |
| Scenarios | Worst / real / best quantiles (configurable) |
| Result workbook | Summary (moments, CI of the mean, scenarios, percentiles 1–99 %), histograms with cumulative curves, tornado charts, protected Data sheet. Runs above 1,000,000 iterations go to CSV |
| Result functions | 38 `MCR_` functions: mean, min, max, sum, count, stdev, var, skewness, kurtosis, CV, percentile, percent rank, probability between, semivariances, expected gain and loss, expected shortfall, mode, density, value per iteration, sorted value, worst/real/best, Pearson/Spearman, regression, dynamic sensitivity, conditional probability, top drivers, bootstrap CI, Anderson-Darling, sample and histogram arrays |
| Data tools | Pearson, Spearman, sorted value, Anderson-Darling, bootstrap CI and the Newcomb-Benford test on cell ranges |

The full list of 83 functions is in **[docs/FUNCTIONS.md](docs/FUNCTIONS.md)**.
The function wizard shows the same descriptions.

### Not implemented yet (from the guide)

- **Graphical wizard:** distribution preview with the draggable "magic stripe"
  for quantiles.
- **Interactive result windows:** threshold bars, spider diagram, decision
  matrix, scatter plots and the interactive dynamic-sensitivity view. The result
  workbook covers histograms, cumulative curves and tornado charts.
- **Fitting:** distribution and time-series fitting (KS/AIC, Theil's U),
  Holt-Winters forecasting, the forecast tool and `Batch` functions.
- **Variants:** multiple simulation of variants or options.
- **Bayesian tools:** MCMC posteriors, the Bayes classifier, Bayesian
  regression, kernel density functions, portfolio/matrix functions, Markov
  chains and the last-digit test.
- **Model tools:** influence diagram, AI mentor, model-context sheet,
  hide/load formulas, model check, correlogram, saving results as cell comments.
- **Asymmetric copulas:** asymmetric Gumbel, triangular and Fréchet.
- **Memory Calculation:** the in-memory engine that skips spreadsheet
  recalculation.

## How it works

Every distribution samples by inversion (its quantile function), so any
dependence structure is applied the same way, through correlated uniforms from
a copula. A run has three stages:

1. **Register.** The engine recalculates the document once. Each `MC_` input,
   `MC_OUTPUT` and `MC_CORRELATE` call registers itself by name in a shared
   in-process state (`core/simulation.cpp`).
2. **Prepare.** Correlated uniforms are generated for every input and
   iteration (`core/copula.cpp`).
3. **Simulate.** For each iteration the engine recalculates the document. Each
   input turns its uniform into a value of its distribution, whose parameters
   may themselves reference other uncertain cells, and records it. Each output
   records the value it receives.

Because inputs are identified by name rather than by cell, formulas can be
copied, moved or combined with other spreadsheet functions inside their
arguments.

```
core/   plain C++17, no LibreOffice dependency: distributions, copulas, time series,
        statistics, simulation state (unit-tested in tests/core_tests.cpp)
uno/    Calc add-in (addin.cpp), protocol handler with the menu commands (engine.cpp),
        result workbook (report.cpp), dialogs (ui.cpp)
tools/  gen_functions.py: a single table that generates the UNO IDL, C++ declarations,
        bodies, wizard metadata and docs/FUNCTIONS.md for all functions
ext/    extension descriptors, menu/toolbar (Addons.xcu), protocol handler config
```

## Differences from MC FLO

- **Function names and signatures.** Inputs start with `MC_` and take the
  variable name as their first argument. Result functions start with `MCR_`.
- **Outputs.** `MC_OUTPUT("Name"; value)` wraps the value. MC FLO's form is
  `=cell + FLOsimula_output("Name")`.
- **Correlation type codes.** These are MonteCalc's own: 0 Gaussian,
  1 Student-t, 2 Clayton, 3 rotated Clayton, 4 Gumbel, 5 rotated Gumbel.
- **Scenarios.** Worst, real and best are quantiles of each variable. The
  defaults are 5 %, 50 % and 95 %, which suits "higher is better" outputs; swap
  them in the settings for cost-type outputs.
- **Time series.** `Periods` simulates *n* steps from the last observed value in
  every iteration.
- **Geometric distribution.** It counts failures before the first success
  (support 0, 1, 2, …).
- **Lognormal.** It takes the mean and standard deviation of the variable
  itself.
- **Before the first run.** Result functions return −1, as in the guide. An
  unknown variable name gives a cell error.

## Building

Requirements: the LibreOffice SDK (`libreoffice-dev` on Debian/Ubuntu), g++ ≥ 9,
Python 3 and zip.

```bash
make            # extension: build/MonteCalc.oxt (Linux x86_64)
make test       # core unit tests (1335 checks)
make itest      # end-to-end test in headless LibreOffice (run as a normal user, not root)
```

The current build targets **Linux x86_64**. Windows and macOS need a native
build against the LibreOffice SDK on those platforms, because the C++ ABI
differs (MSVC on Windows). The sources are portable C++17 plus the UNO API,
and the Makefile is the reference for the build steps.

## Performance

The engine recalculates the whole document once per iteration, just as an Excel
add-in recalculates the workbook. Measured in headless LibreOffice 24.2:

- a tiny model: about 0.03 ms per iteration
- a model with 200 ordinary formulas: about 0.2 ms per iteration

Avoid large array formulas such as `MCR_SAMPLES` over thousands of cells on the
simulated sheet. Calc has to re-spread them on every iteration; put them in the
result workbook or another document instead.
