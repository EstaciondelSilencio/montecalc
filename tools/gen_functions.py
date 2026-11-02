#!/usr/bin/env python3
"""Generate the UNO IDL, C++ declarations/bodies and function-wizard metadata
for every MonteCalc spreadsheet function from one table.

Usage: gen_functions.py <outdir>
Produces: XMonteCalc.idl, functions_decl.inc, functions_impl.inc, functions_meta.inc
"""
import os
import sys

# Parameter types
S, D, A, R = "string", "double", "any", "range"   # range = cell range of any
OPT_BOUNDS = [("Lower", A, "Optional lower bound (truncation)."),
              ("Upper", A, "Optional upper bound (truncation)."),
              ("Count", A, "Optional number of occurrences per iteration; the values are summed (compound distribution).")]
NAME = ("Name", S, "Unique variable name, e.g. \"Price\".")

F = []  # (prog, display, ret, description, params, body)


def dist(prog, label, desc, params, maker):
    args = ", ".join(p[0] for p in params)
    body = (f'return distInput(Name, "{label}", {{{args}}}, '
            f'[=]() {{ return mc::{maker}({args}); }}, Lower, Upper, Count);')
    F.append((prog, "MC_" + prog[2:].upper(), D, desc, [NAME] + params + OPT_BOUNDS, body))


# ---------------------------------------------------------------- continuous
dist("mcUniform", "Uniform", "Input variable: continuous uniform distribution.",
     [("Min", D, "Minimum."), ("Max", D, "Maximum.")], "makeUniform")
dist("mcTriangular", "Triangular", "Input variable: triangular distribution.",
     [("Min", D, "Minimum."), ("Mode", D, "Most likely value."), ("Max", D, "Maximum.")], "makeTriangular")
dist("mcPert", "PERT", "Input variable: PERT (modified beta) distribution.",
     [("Min", D, "Minimum."), ("Mode", D, "Most likely value."), ("Max", D, "Maximum.")], "makePert")
dist("mcNormal", "Normal", "Input variable: normal distribution.",
     [("Mean", D, "Mean."), ("StdDev", D, "Standard deviation.")], "makeNormal")
dist("mcLognormal", "Lognormal", "Input variable: lognormal distribution given the mean and standard deviation of the variable.",
     [("Mean", D, "Mean of the variable (> 0)."), ("StdDev", D, "Standard deviation of the variable.")], "makeLognormal")
dist("mcBeta", "Beta", "Input variable: beta distribution scaled to [Min, Max].",
     [("Alpha", D, "Shape alpha."), ("Beta", D, "Shape beta."), ("Min", D, "Minimum."), ("Max", D, "Maximum.")], "makeBeta")
dist("mcGumbel", "Gumbel", "Input variable: Gumbel (maximum extreme value) distribution.",
     [("Location", D, "Location."), ("Scale", D, "Scale (> 0).")], "makeGumbel")
dist("mcGamma", "Gamma", "Input variable: gamma distribution.",
     [("Shape", D, "Shape k."), ("Scale", D, "Scale theta.")], "makeGamma")
dist("mcExponential", "Exponential", "Input variable: exponential distribution.",
     [("Rate", D, "Rate lambda (mean = 1/lambda).")], "makeExponential")
dist("mcWeibull", "Weibull", "Input variable: Weibull distribution.",
     [("Shape", D, "Shape k."), ("Scale", D, "Scale lambda.")], "makeWeibull")
dist("mcChiSquare", "ChiSquare", "Input variable: chi-square distribution.",
     [("DF", D, "Degrees of freedom.")], "makeChiSquare")
dist("mcStudentT", "StudentT", "Input variable: Student-t distribution.",
     [("DF", D, "Degrees of freedom.")], "makeStudentT")
dist("mcPareto", "Pareto", "Input variable: Pareto distribution.",
     [("Shape", D, "Shape alpha."), ("Scale", D, "Scale (minimum value).")], "makePareto")
dist("mcF", "F", "Input variable: F distribution.",
     [("DF1", D, "Numerator degrees of freedom."), ("DF2", D, "Denominator degrees of freedom.")], "makeF")
dist("mcArcsine", "Arcsine", "Input variable: arcsine distribution on [Min, Max].",
     [("Min", D, "Minimum."), ("Max", D, "Maximum.")], "makeArcsine")
dist("mcCauchy", "Cauchy", "Input variable: Cauchy distribution (shows the median, the mean is undefined).",
     [("Location", D, "Location."), ("Scale", D, "Scale.")], "makeCauchy")
dist("mcFrechet", "Frechet", "Input variable: Frechet distribution.",
     [("Shape", D, "Shape alpha."), ("Scale", D, "Scale."), ("Location", D, "Location (minimum).")], "makeFrechet")
dist("mcLogistic", "Logistic", "Input variable: logistic distribution.",
     [("Location", D, "Location."), ("Scale", D, "Scale.")], "makeLogistic")

F.append(("mcTruncNormal", "MC_TRUNCNORMAL", D,
          "Input variable: truncated normal distribution.",
          [NAME, ("Mean", D, "Mean of the untruncated normal."), ("StdDev", D, "Standard deviation of the untruncated normal."),
           ("Lower", D, "Lower bound."), ("Upper", D, "Upper bound."), OPT_BOUNDS[2]],
          'return distInput(Name, "TruncNormal", {Mean, StdDev, Lower, Upper}, [=]() { return mc::makeNormal(Mean, StdDev); }, '
          'css::uno::Any(Lower), css::uno::Any(Upper), Count);'))

F.append(("mcCustom", "MC_CUSTOM", D,
          "Input variable: user-defined continuous distribution from data (one column), a histogram (columns Min, Max, Number) or a quantile table (columns Percentile, Value).",
          [NAME, ("Data", R, "Cell range with raw data, a histogram or a quantile table."),
           ("Bins", A, "For raw data: number of histogram bins (0 or omitted = empirical distribution).")] + OPT_BOUNDS,
          "return customInput(Name, Data, Bins, Lower, Upper, Count);"))

CONT = {f[0] for f in F}
# ---------------------------------------------------------------- discrete
dist("mcDUniform", "DiscreteUniform", "Input variable: discrete uniform distribution (integers).",
     [("Min", D, "Minimum."), ("Max", D, "Maximum.")], "makeDiscreteUniform")
dist("mcBernoulli", "Bernoulli", "Input variable: Bernoulli distribution (1 with probability P, else 0).",
     [("P", D, "Probability of 1.")], "makeBernoulli")
dist("mcBinomial", "Binomial", "Input variable: binomial distribution.",
     [("Trials", D, "Number of trials."), ("P", D, "Probability of success.")], "makeBinomial")
dist("mcPoisson", "Poisson", "Input variable: Poisson distribution.",
     [("Lambda", D, "Mean number of events.")], "makePoisson")
dist("mcNegBinomial", "NegBinomial", "Input variable: negative binomial (Pascal) distribution: failures before the R-th success.",
     [("R", D, "Number of successes."), ("P", D, "Probability of success.")], "makeNegBinomial")
dist("mcGeometric", "Geometric", "Input variable: geometric distribution: failures before the first success.",
     [("P", D, "Probability of success.")], "makeGeometric")
dist("mcHypergeometric", "Hypergeometric", "Input variable: hypergeometric distribution.",
     [("Population", D, "Population size."), ("Successes", D, "Successes in the population."), ("Draws", D, "Number of draws.")],
     "makeHypergeometric")
F.append(("mcDCustom", "MC_DCUSTOM", D,
          "Input variable: user-defined discrete distribution from values and their probabilities (or weights).",
          [NAME, ("Values", R, "Cell range with the possible values."), ("Probabilities", R, "Cell range with probabilities or weights.")] + OPT_BOUNDS,
          "return discreteCustomInput(Name, Values, Probabilities, Lower, Upper, Count);"))

DISC = {f[0] for f in F} - CONT
# ---------------------------------------------------------------- time series
PER = ("Periods", A, "Optional number of periods to simulate in each iteration (default 1); the value at the last period is returned.")
for prog, label, ts, params in [
    ("mcAR1", "AR1", "AR1", [("Mu", D, "Long-run mean."), ("Phi", D, "Autoregressive coefficient (|Phi| < 1)."), ("Sigma", D, "Noise standard deviation."), ("Y0", D, "Last observed value.")]),
    ("mcMA1", "MA1", "MA1", [("Mu", D, "Mean."), ("Theta", D, "Moving-average coefficient."), ("Sigma", D, "Noise standard deviation.")]),
    ("mcARMA", "ARMA11", "ARMA11", [("Mu", D, "Long-run mean."), ("Phi", D, "Autoregressive coefficient."), ("Theta", D, "Moving-average coefficient."), ("Sigma", D, "Noise standard deviation."), ("Y0", D, "Last observed value.")]),
    ("mcARCH", "ARCH1", "ARCH1", [("Mu", D, "Mean."), ("A0", D, "Constant variance term (> 0)."), ("A1", D, "ARCH coefficient (0 <= A1 < 1)."), ("Y0", D, "Last observed value.")]),
    ("mcGBM", "GBM", "GBM", [("S0", D, "Current value (> 0)."), ("Drift", D, "Drift per unit of time."), ("Volatility", D, "Volatility per unit of time."), ("Dt", D, "Length of one period in units of time.")]),
]:
    args = ", ".join(p[0] for p in params)
    F.append((prog, "MC_" + prog[2:].upper(), D,
              f"Input variable: {label} time-series process" + (" (Wiener process / geometric Brownian motion)." if ts == "GBM" else "."),
              [NAME] + params + [PER],
              f'return tsInput(Name, "{label}", mc::TsType::{ts}, {{{args}}}, Periods);'))

TS = {f[0] for f in F} - CONT - DISC
# ---------------------------------------------------------------- output & correlation
F.append(("mcOutput", "MC_OUTPUT", D, "Output variable: records Value in every iteration and returns it.",
          [NAME, ("Value", D, "The calculated result to analyse, usually a cell reference.")],
          "return guard([&] { return mc::Simulation::instance().output(u8(Name), Value); });"))
F.append(("mcCorrelate", "MC_CORRELATE", D,
          "Defines a rank correlation between two input variables. Returns the coefficient.",
          [("Name1", S, "First input variable."), ("Name2", S, "Second input variable."),
           ("Rho", D, "Rank (Spearman) correlation between -1 and 1."),
           ("Type", A, "0 Gaussian (default), 1 Student-t, 2 Clayton, 3 rotated Clayton, 4 Gumbel, 5 rotated Gumbel.")],
          "return guard([&] { return mc::Simulation::instance().correlate(u8(Name1), u8(Name2), Rho, int(optNum(Type, 0))); });"))

# ---------------------------------------------------------------- result statistics
RN = ("Name", S, "Name of a simulated input or output variable.")


def res(prog, desc, expr, extra=(), ret=D):
    F.append((prog, "MCR_" + prog[3:].upper(), ret, desc + " Returns -1 if no simulation has been run.",
              [RN] + list(extra),
              f"return withSamples(Name, [&](const std::vector<double>& v) {{ return {expr}; }});"))


res("mcrMean", "Mean of the simulated values.", "mc::stats::mean(v)")
res("mcrMin", "Minimum of the simulated values.", "mc::stats::min(v)")
res("mcrMax", "Maximum of the simulated values.", "mc::stats::max(v)")
res("mcrSum", "Sum of the simulated values.", "mc::stats::sum(v)")
res("mcrCount", "Number of valid simulated values.", "mc::stats::count(v)")
res("mcrStdev", "Sample standard deviation.", "mc::stats::stdev(v)")
res("mcrVar", "Sample variance.", "mc::stats::variance(v)")
res("mcrSkew", "Skewness.", "mc::stats::skewness(v)")
res("mcrKurt", "Excess kurtosis.", "mc::stats::kurtosis(v)")
res("mcrCV", "Coefficient of variation (standard deviation / mean).", "mc::stats::cv(v)")
res("mcrPercentile", "Value at percentile P.", "mc::stats::percentile(v, P)", [("P", D, "Percentile between 0 and 1.")])
res("mcrPercentRank", "Share of simulated values less than or equal to Value.", "mc::stats::percentRank(v, Value)", [("Value", D, "Value.")])
res("mcrProbBetween", "Probability that the variable lies between Lower and Upper.", "mc::stats::probBetween(v, Lower, Upper)",
    [("Lower", D, "Lower limit."), ("Upper", D, "Upper limit.")])
res("mcrSemivar", "Semivariance below the mean.", "mc::stats::semivariance(v)")
res("mcrSemivarBelow", "Semivariance of the values below Limit.", "mc::stats::semivarianceBelow(v, Limit)", [("Limit", D, "Limit.")])
res("mcrSemivarAbove", "Semivariance of the values above Limit.", "mc::stats::semivarianceAbove(v, Limit)", [("Limit", D, "Limit.")])
res("mcrExpGain", "Mean of all values above 0.", "mc::stats::expectedGain(v)")
res("mcrExpGainVar", "Variance of all values above 0.", "mc::stats::expectedGainVariance(v)")
res("mcrExpLoss", "Mean of all values below 0.", "mc::stats::expectedLoss(v)")
res("mcrShortfall", "Expected shortfall beyond the value-at-risk at percentile P (P <= 0.5: left tail, otherwise right tail).",
    "mc::stats::expectedShortfall(v, P)", [("P", D, "Percentile defining the value-at-risk.")])
res("mcrMode", "Most frequent value based on a histogram.", "mc::stats::mode(v, int(optNum(Bins, binsDefault())))",
    [("Bins", A, "Optional number of histogram bins.")])
res("mcrDensity", "Kernel density estimate of the simulated distribution at X.", "mc::stats::density(v, X)", [("X", D, "Point.")])
res("mcrValue", "Value of the variable in a given iteration (1-based).", "iterValue(v, Iteration)", [("Iteration", D, "Iteration number, starting at 1.")])
res("mcrSorted", "Value at position Pos of the sorted simulated values (1 = smallest).", "sortedValue(v, Pos)", [("Pos", D, "Position, starting at 1.")])
res("mcrWorst", "Value of the worst-case scenario (configured quantile, or Quantile if given).",
    "mc::stats::percentile(v, optNum(Quantile, mc::Simulation::instance().settings().worst))", [("Quantile", A, "Optional quantile overriding the setting.")])
res("mcrReal", "Value of the real-case scenario (configured quantile, or Quantile if given).",
    "mc::stats::percentile(v, optNum(Quantile, mc::Simulation::instance().settings().real))", [("Quantile", A, "Optional quantile overriding the setting.")])
res("mcrBest", "Value of the best-case scenario (configured quantile, or Quantile if given).",
    "mc::stats::percentile(v, optNum(Quantile, mc::Simulation::instance().settings().best))", [("Quantile", A, "Optional quantile overriding the setting.")])
res("mcrNormalTest", "Anderson-Darling test: 1 if the values are plausibly normal (5% level), else 0.", "normalTest(v)")

F.append(("mcrBootstrap", "MCR_BOOTSTRAP", S,
          "Bootstrap confidence interval of the mean (or of percentile P) of a simulated variable. Returns \"estimate; lower; upper\".",
          [RN, ("Samples", D, "Number of bootstrap samples."), ("Confidence", D, "Confidence level, e.g. 0.95."),
           ("P", A, "Optional percentile instead of the mean.")],
          "return bootstrapText(Name, nullptr, Samples, Confidence, P);"))


def pair(prog, desc, expr, n1="Name1", n2="Name2", extra=()):
    F.append((prog, "MCR_" + prog[3:].upper(), D, desc + " Returns -1 if no simulation has been run.",
              [(n1, S, "First variable."), (n2, S, "Second variable.")] + list(extra),
              f"return withPair({n1}, {n2}, [&](const std::vector<double>& a, const std::vector<double>& b) {{ return {expr}; }});"))


pair("mcrCorrP", "Pearson correlation between two simulated variables.", "mc::stats::pearson(a, b)")
pair("mcrCorrS", "Spearman rank correlation between two simulated variables.", "mc::stats::spearman(a, b)")
pair("mcrRegSlope", "Slope of the linear regression of Y on X.", "mc::stats::regressionSlope(a, b)", "X", "Y")
pair("mcrRegIntercept", "Intercept of the linear regression of Y on X.", "mc::stats::regressionIntercept(a, b)", "X", "Y")
pair("mcrSensitivity", "Dynamic sensitivity: the input is sorted and split into bins; the output mean per bin is computed. Type 0 = range, 1 = minimum, 2 = maximum.",
     "mc::stats::dynamicSensitivity(a, b, int(optNum(Bins, 10)))[std::min(2, std::max(0, int(optNum(Type, 0))))]", "Input", "Output",
     [("Type", A, "0 range (default), 1 minimum, 2 maximum."), ("Bins", A, "Number of bins (default 10).")])
F.append(("mcrBayes", "MCR_BAYES", D,
          "Conditional probability P(A | B) from the simulation, e.g. MCR_BAYES(\"NPV\"; \">=\"; 1000; \"Costs\"; \">=\"; 100). Returns -1 if no simulation has been run.",
          [("Name1", S, "Variable of event A."), ("Op1", S, "Comparison: < <= > >= = <>"), ("Value1", D, "Threshold of event A."),
           ("Name2", S, "Variable of event B."), ("Op2", S, "Comparison: < <= > >= = <>"), ("Value2", D, "Threshold of event B.")],
          "return withPair(Name1, Name2, [&](const std::vector<double>& a, const std::vector<double>& b) "
          "{ return mc::stats::conditionalProbability(a, u8(Op1), Value1, b, u8(Op2), Value2); });"))
F.append(("mcrTopDriver", "MCR_TOPDRIVER", S,
          "Name of the input with the Rank-th strongest absolute rank correlation with the output.",
          [("Output", S, "Output variable."), ("Rank", D, "1 = strongest driver.")],
          "return topDriverName(Output, Rank);"))
F.append(("mcrTopDriverCorr", "MCR_TOPDRIVERCORR", D,
          "Rank correlation of the Rank-th strongest driver of the output. Returns -1 if no simulation has been run.",
          [("Output", S, "Output variable."), ("Rank", D, "1 = strongest driver.")],
          "return topDriverCorr(Output, Rank);"))
F.append(("mcrIterations", "MCR_ITERATIONS", D, "Number of iterations of the last simulation (0 if none).", [],
          "return mc::Simulation::instance().iterations();"))
F.append(("mcrInputName", "MCR_INPUTNAME", S, "Name of the input variable at position Pos (1-based).",
          [("Pos", D, "Position.")], "return variableName(mc::VarKind::Input, Pos);"))
F.append(("mcrOutputName", "MCR_OUTPUTNAME", S, "Name of the output variable at position Pos (1-based).",
          [("Pos", D, "Position.")], "return variableName(mc::VarKind::Output, Pos);"))
F.append(("mcrSamples", "MCR_SAMPLES", "matrix", "Array of all simulated values of a variable (enter as array formula).",
          [RN], "return samplesMatrix(Name);"))
F.append(("mcrHistogram", "MCR_HISTOGRAM", "matrix",
          "Histogram of a simulated variable as an array with columns Min, Max, Number, Percent, Percent cum.",
          [RN, ("Bins", A, "Optional number of bins.")], "return histogramMatrix(Name, Bins);"))

# ---------------------------------------------------------------- data tools (work on ranges)
F.append(("mcPearson", "MC_PEARSON", D, "Pearson correlation of two data ranges.",
          [("Data1", R, "First range."), ("Data2", R, "Second range.")],
          "return guard([&] { return mc::stats::pearson(numbers(Data1), numbers(Data2)); });"))
F.append(("mcSpearman", "MC_SPEARMAN", D, "Spearman rank correlation of two data ranges.",
          [("Data1", R, "First range."), ("Data2", R, "Second range.")],
          "return guard([&] { return mc::stats::spearman(numbers(Data1), numbers(Data2)); });"))
F.append(("mcSortValue", "MC_SORTVALUE", D, "Value at position Pos of the sorted range.",
          [("Data", R, "Range."), ("Pos", D, "Position, starting at 1."), ("Descending", A, "Optional: 1 sorts descending.")],
          "return sortValue(Data, Pos, Descending);"))
F.append(("mcNormalTestData", "MC_NORMALTEST", D, "Anderson-Darling normality test for a data range: 1 if plausibly normal (5% level), else 0.",
          [("Data", R, "Range.")], "return guard([&] { return normalTest(numbers(Data)); });"))
F.append(("mcBootstrapData", "MC_BOOTSTRAP", S,
          "Bootstrap confidence interval of the mean (or of percentile P) of a data range. Returns \"estimate; lower; upper\".",
          [("Data", R, "Range."), ("Samples", D, "Number of bootstrap samples."), ("Confidence", D, "Confidence level, e.g. 0.95."),
           ("P", A, "Optional percentile instead of the mean.")],
          "return bootstrapText(OUString(), &Data, Samples, Confidence, P);"))
F.append(("mcBenford", "MC_BENFORD", "matrix",
          "Newcomb-Benford first-digit test: 9 rows (digit, observed share, expected share) plus a row (chi-square, p-value, 1 if consistent at 5%).",
          [("Data", R, "Range.")], "return benfordMatrix(Data);"))

# ---------------------------------------------------------------- generation
IDL_T = {S: "string", D: "double", A: "any", R: "sequence< sequence< any > >",
         "matrix": "sequence< sequence< double > >"}
CPP_T = {S: "const OUString&", D: "double", A: "const css::uno::Any&",
         R: "const css::uno::Sequence< css::uno::Sequence< css::uno::Any > >&"}
CPP_RET = {S: "OUString", D: "double", "matrix": "css::uno::Sequence< css::uno::Sequence< double > >"}


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main(out):
    os.makedirs(out, exist_ok=True)
    names = set()
    for f in F:
        assert f[0] not in names, f[0]
        names.add(f[0])
    with open(os.path.join(out, "XMonteCalc.idl"), "w") as h:
        h.write("// generated by tools/gen_functions.py - do not edit\n")
        h.write("module org { module montecalc {\n")
        h.write("interface XMonteCalc : com::sun::star::uno::XInterface\n{\n")
        for prog, disp, ret, desc, params, body in F:
            ps = ", ".join(f"[in] {IDL_T[t]} {n}" for n, t, _ in params)
            h.write(f"    {IDL_T[ret]} {prog}({ps});\n")
        h.write("};\n}; };\n")
    with open(os.path.join(out, "functions_decl.inc"), "w") as h:
        for prog, disp, ret, desc, params, body in F:
            ps = ", ".join(f"{CPP_T[t]} {n}" for n, t, _ in params)
            h.write(f"    {CPP_RET[ret]} SAL_CALL {prog}({ps}) override;\n")
    with open(os.path.join(out, "functions_impl.inc"), "w") as h:
        for prog, disp, ret, desc, params, body in F:
            ps = ", ".join(f"{CPP_T[t]} {n}" for n, t, _ in params)
            h.write(f"{CPP_RET[ret]} SAL_CALL MonteCalcAddIn::{prog}({ps})\n{{\n    {body}\n}}\n\n")
    with open(os.path.join(out, "functions_meta.inc"), "w") as h:
        for prog, disp, ret, desc, params, body in F:
            args = ", ".join("{%s, %s}" % (cstr(n), cstr(d)) for n, _, d in params)
            h.write("{ %s, %s, %s, { %s } },\n" % (cstr(prog), cstr(disp), cstr(desc), args))
    write_reference(os.path.join(out, "FUNCTIONS.md"))
    print(f"generated {len(F)} functions")


SECTIONS = [("Input variables: continuous distributions", lambda p: p in CONT),
            ("Input variables: discrete distributions", lambda p: p in DISC),
            ("Input variables: time series", lambda p: p in TS),
            ("Output and correlation", lambda p: p in ("mcOutput", "mcCorrelate")),
            ("Result statistics (after a simulation)", lambda p: p.startswith("mcr")),
            ("Data tools (work on cell ranges)", lambda p: True)]


def write_reference(path):
    done = set()
    with open(path, "w") as h:
        h.write("# MonteCalc function reference\n\n_Generated by tools/gen_functions.py._\n\n"
                "Arguments in [brackets] are optional. Separate arguments with `;` (or `,` depending on your locale).\n")
        for title, pred in SECTIONS:
            items = [f for f in F if f[0] not in done and pred(f[0])]
            if not items:
                continue
            h.write(f"\n## {title}\n\n| Function | Description |\n|---|---|\n")
            for prog, disp, ret, desc, params, body in items:
                done.add(prog)
                args = "; ".join(f"[{n}]" if t == A else n for n, t, _ in params)
                h.write(f"| `{disp}({args})` | {desc} |\n")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/gen")
