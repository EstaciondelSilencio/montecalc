#!/usr/bin/env python3
"""Generate the UNO IDL, C++ declarations/bodies and function-wizard metadata for
every spreadsheet function from one table, using the MC FLO function names of the
user guide (FLOsimula_* model functions, fMC_* tool functions).

Usage: gen_functions.py <outdir>
Produces: XMonteCalc.idl, functions_decl.inc, functions_impl.inc, functions_meta.inc, FUNCTIONS.md
"""
import os
import sys

S, D, A, R, C = "string", "double", "any", "range", "cell"
F = []  # (name, ret, description, params[(name, type, desc)], body, section, note)


def fn(name, ret, desc, params, body, section, note=""):
    F.append((name, ret, desc, params, body, section, note))


# ============================================================ FLOsimula_* model functions
OPT = [("Min", A, "Optional lower bound (truncation)."), ("Max", A, "Optional upper bound (truncation)."),
       ("Round", A, "Optional number of decimals (TRUE = whole numbers)."),
       ("Number", A, "Optional number of occurrences per iteration; the draws are summed (compound distribution).")]
NAMEP = ("Name", A, "Optional variable name (text). If omitted, the first argument is the first parameter.")

CONT = [
    ("Uniform", "Continuous uniform distribution.", ["Minimum", "Maximum"]),
    ("Triangle", "Triangle distribution.", ["Minimum", "Mode", "Maximum"]),
    ("PERT", "PERT distribution.", ["Minimum", "Mode", "Maximum"]),
    ("Normal", "Normal distribution.", ["Mean", "StdDev"]),
    ("Lognormal", "Lognormal distribution (mean and standard deviation of the variable).", ["Mean", "StdDev"]),
    ("Beta", "Beta distribution scaled to [Minimum, Maximum].", ["Alpha", "Beta", "Minimum", "Maximum"]),
    ("Gumbel", "Gumbel (extreme value) distribution.", ["Location", "Scale"]),
    ("Gamma", "Gamma distribution.", ["Shape", "Scale"]),
    ("Exponential", "Exponential distribution (Rate = 1/mean).", ["Rate"]),
    ("TruncatedNormal", "Truncated normal distribution.", ["Mean", "StdDev", "LowerLimit", "UpperLimit"]),
    ("Weibull", "Weibull distribution.", ["Shape", "Scale"]),
    ("Chi2", "Chi-square distribution.", ["DegreesOfFreedom"]),
    ("StudentT", "Student-t distribution.", ["DegreesOfFreedom"]),
    ("Pareto", "Pareto distribution.", ["Shape", "Scale"]),
    ("F", "F distribution.", ["DegreesOfFreedom1", "DegreesOfFreedom2"]),
    ("ArcSin", "Arcsine distribution.", ["Minimum", "Maximum"]),
    ("Cauchy", "Cauchy distribution (the cell shows the median).", ["Location", "Scale"]),
    ("Frechet", "Frechet distribution.", ["Shape", "Scale", "Location"]),
    ("Logistic", "Logistic distribution.", ["Location", "Scale"]),
]
DISC = [
    ("DiscreteUniform", "Discrete uniform distribution (integers).", ["Minimum", "Maximum"]),
    ("Bernoulli", "Bernoulli distribution: 1 with the given probability, else 0.", ["Probability"]),
    ("Binomial", "Binomial distribution.", ["Trials", "Probability"]),
    ("Poisson", "Poisson distribution.", ["Lambda"]),
    ("Pascal", "Pascal (negative binomial) distribution: failures before the R-th success.", ["Successes", "Probability"]),
    ("Geometric", "Geometric distribution: failures before the first success.", ["Probability"]),
    ("HyperGeometric", "Hypergeometric distribution.", ["Population", "Successes", "Draws"]),
]
TS = [
    ("AR1", "AR(1) autoregressive process.", ["Mu", "Phi", "Sigma", "LastValue"]),
    ("MA1", "MA(1) moving-average process.", ["Mu", "Theta", "Sigma"]),
    ("ARMA11", "ARMA(1,1) process.", ["Mu", "Phi", "Theta", "Sigma", "LastValue"]),
    ("ARCH1", "ARCH(1) process (non-constant volatility).", ["Mu", "A0", "A1", "LastValue"]),
    ("WienerProcess", "Wiener process (geometric Brownian motion) with drift.", ["StartValue", "Drift", "Volatility", "Dt"]),
]


def flo_body(name, params):
    return f'return flo("{name}", {{{", ".join(p[0] for p in params)}}});'


for lab, desc, ps in CONT:
    params = [NAMEP] + [(p, A, p + ".") for p in ps] + OPT
    fn("FLOsimula_" + lab, D, "Input variable: " + desc, params, flo_body("FLOsimula_" + lab, params), "cont")
for lab, desc, ps in DISC:
    params = [NAMEP] + [(p, A, p + ".") for p in ps] + OPT
    fn("FLOsimula_" + lab, D, "Input variable: " + desc, params, flo_body("FLOsimula_" + lab, params), "disc")
for lab, desc, ps in TS:
    params = [NAMEP] + [(p, A, p + ".") for p in ps] + [("Number", A, "Optional number of periods simulated per iteration (default 1).")]
    fn("FLOsimula_" + lab, D, "Input variable: " + desc, params, flo_body("FLOsimula_" + lab, params), "ts",
       "With Number = n each iteration simulates n periods from the last value and returns the value of period n.")
p = [("Data", A, "Data area: raw data, a histogram (Min, Max, Number, ...) or a quantile table."),
     ("NumberOfColumns", A, "Number of histogram columns for raw data (0 = empirical distribution)."), ("Name", A, "Variable name.")] + OPT
fn("FLOsimula_UserDefined", D, "Input variable: user-defined distribution from a data area.", p, flo_body("FLOsimula_UserDefined", p), "cont",
   "Two-column areas are read as (quantile, value) rows; if the quantiles are not all in [0,1] the last row is (minimum, maximum), as in the manual.")
p = [("Values", A, "Possible values."), ("Probabilities", A, "Probabilities or weights."), ("Name", A, "Variable name.")] + OPT
fn("FLOsimula_DiscreteUserDefined", D, "Input variable: user-defined discrete distribution.", p, flo_body("FLOsimula_DiscreteUserDefined", p), "disc")
p = [("Reference", A, "The MC FLO variable (cell) to bound."), ("LowerLimit", A, "Lower barrier."), ("UpperLimit", A, "Upper barrier.")]
fn("FLOsimula_Bounded", D, "Limits an MC FLO variable to [LowerLimit, UpperLimit].", p, flo_body("FLOsimula_Bounded", p), "other",
   "Values outside the barriers are set to the barrier. For exact truncation use the Min/Max arguments of the distribution.")
p = [("Name", A, "Name of the output variable."), ("Value", A, "Optional value; if omitted the function returns 0 so that it can be added to a cell: =B30+FLOsimula_output(\"NPV\").")]
fn("FLOsimula_output", D, "Output variable.", p, flo_body("FLOsimula_output", p), "other")
p = [("Name1", A, "First input variable."), ("Name2", A, "Second input variable."), ("Coefficient", A, "Rank correlation between -1 and 1."),
     ("Type", A, "0 normal, 1 Student-t, 2 Clayton, 3 r-Clayton, 4 Gumbel, 5 r-Gumbel, 6 asy. Gumbel, 7 asy. Frechet, 8 asy. Triangular.")]
fn("FLOsimula_Correlation", D, "Correlation between two input variables; returns the coefficient.", p, flo_body("FLOsimula_Correlation", p), "other",
   "The manual names the correlation types but not the formula name or the asymmetric families' definitions; see the README.")

# ============================================================ fMC_* result functions
N = ("Name", S, "Name of a simulated variable.")


def res(name, desc, expr, extra=(), note=""):
    fn(name, D, desc + " Returns -1 if no simulation has been run.", [N] + list(extra),
       f"return withSamples(Name, [&](const Vec& v) {{ return {expr}; }});", "res", note)


res("fMC_Average", "Average of the distribution.", "mc::stats::mean(v)")
res("fMC_Mean", "Average of the distribution.", "mc::stats::mean(v)")
res("fMC_Min", "Minimum of the distribution.", "mc::stats::min(v)")
res("fMC_Max", "Maximum of the distribution.", "mc::stats::max(v)")
res("fMC_Sum", "Sum of the distribution.", "mc::stats::sum(v)")
res("fMC_StdS", "Sample standard deviation.", "mc::stats::stdev(v)")
res("fMC_Variance", "Sample variance.", "mc::stats::variance(v)")
res("fMC_Skewness", "Skewness.", "mc::stats::skewness(v)")
res("fMC_Kurtosis", "Excess kurtosis.", "mc::stats::kurtosis(v)")
res("fMC_CV", "Coefficient of variation.", "mc::stats::cv(v)")
res("fMC_Semivariance", "Semivariance (below the mean).", "mc::stats::semivariance(v)")
res("fMC_SemivarianceL", "Semivariance of the values below Limit.", "mc::stats::semivarianceBelow(v, Limit)", [("Limit", D, "Limit.")])
res("fMC_SemivarianceR", "Semivariance of the values above Limit.", "mc::stats::semivarianceAbove(v, Limit)", [("Limit", D, "Limit.")])
res("fMC_ExpGain", "Average of all values above 0.", "mc::stats::expectedGain(v)")
res("fMC_ExpGainV", "Variance of all values above 0.", "mc::stats::expectedGainVariance(v)")
res("fMC_ExpLoss", "Average of all values below 0.", "mc::stats::expectedLoss(v)")
res("fMC_ExpectedShortfall", "Expected shortfall beyond the value-at-risk at the percentile (<= 0.5 left tail, else right tail).",
    "mc::stats::expectedShortfall(v, Percentile)", [("Percentile", D, "Percentile.")])
res("fMC_Percentile", "Value at the percentile.", "mc::stats::percentile(v, Percentile)", [("Percentile", D, "Percentile (0..1).")])
res("fMC_PercentileValue", "Percentile of a given value.", "mc::stats::percentRank(v, Value)", [("Value", D, "Value.")])
res("fMC_PercentileInterval", "Confidence level (share of iterations) between the limits.", "mc::stats::probBetween(v, Lower_Limit, Upper_Limit)",
    [("Lower_Limit", D, "Lower limit."), ("Upper_Limit", D, "Upper limit.")])
res("fMC_Mode", "Most frequent value based on a histogram.", "mc::stats::mode(v, int(UsedBins))", [("UsedBins", D, "Number of histogram columns.")])
res("fMC_Density", "Density of the simulated distribution at Value.", "mc::stats::density(v, Value)", [("Value", D, "Point.")])
res("fMC_ExtremeValue", "Value at position Pos of the sorted list.", "sortedValue(v, Pos)", [("Pos", D, "Position (1 = smallest).")])
res("fMC_PosValue", "Value at iteration Pos.", "iterValue(v, Pos)", [("Pos", D, "Iteration (1-based).")])
res("fMC_ConditionPosValue", "Position (iteration) whose value equals Value, or the nearest one.", "conditionPos(v, Value)", [("Value", D, "Value.")])


def pair(name, desc, expr, extra=(), n1="Name1", n2="Name2", note=""):
    fn(name, D, desc + " Returns -1 if no simulation has been run.", [(n1, S, "First variable."), (n2, S, "Second variable.")] + list(extra),
       f"return withPair({n1}, {n2}, [&](const Vec& a, const Vec& b) {{ return {expr}; }});", "res", note)


pair("fMC_CorrP", "Pearson correlation between two variables.", "mc::stats::pearson(a, b)")
pair("fMC_CorrS", "Spearman rank correlation between two variables.", "mc::stats::spearman(a, b)")
pair("fMC_RegressionX", "Slope of the linear regression (Name1 = x, Name2 = y).", "mc::stats::regressionSlope(a, b)")
pair("fMC_RegressionY", "Intercept of the linear regression (Name1 = x, Name2 = y).", "mc::stats::regressionIntercept(a, b)")
pair("fMC_Spider", "Pearson correlation of the two variables within the range Value1..Value2 of Name1.", "spider(a, b, Value1, Value2)",
     [("Value1", D, "Lower bound (quantile 0..1 or value)."), ("Value2", D, "Upper bound (quantile 0..1 or value).")],
     note="Value1 and Value2 are quantiles of Name1 when both lie in [0,1], otherwise values of Name1.")
pair("fMC_DSensitivity", "Dynamic sensitivity between input Name1 and output Name2: 0 range, 1 minimum, 2 maximum of the interval means.",
     "mc::stats::dynamicSensitivity(a, b, int(NumberBins))[std::min(2, std::max(0, int(DSensotype)))]",
     [("DSensotype", D, "0 range, 1 minimum, 2 maximum."), ("NumberBins", D, "Number of intervals.")])
pair("fMC_FinancialValue", "Percentile Value1 of event A (Name1) given that event B (Name2) is within Value2 +/- Diff.",
     "financialValue(a, b, Value2, Diff, Value1)",
     [("Value2", D, "Value of event B."), ("Diff", D, "Search area: +/- percent of Value2 (5 or 0.05 = 5%)."), ("Value1", D, "Percentile of event A.")])
pair("fMC_Bayes", "Conditional probability P(A | B), e.g. fMC_Bayes(\"NPV\";\">=\";1000;\"Costs\";\">=\";100).",
     "mc::stats::conditionalProbability(a, u8(Comparison1), Value1, b, u8(Comparison2), Value2)",
     [("Comparison1", S, "< <= > >= = <>"), ("Value1", D, "Threshold of A."), ("Comparison2", S, "< <= > >= = <>"), ("Value2", D, "Threshold of B.")])
# fix parameter order of fMC_Bayes to the manual's (Name1; Comparison1; Value1; Name2; Comparison2; Value2)
name, ret, desc, params, body, sec, note = F.pop()
fn(name, ret, desc, [params[0], params[2], params[3], params[1], params[4], params[5]], body, sec, note)
pair("fMC_BayesV", "Value at Percentile of A (Name1) given event B (Name2 Comparison2 Value2).",
     "conditionalPercentile(a, b, u8(Comparison2), Value2, Percentile)",
     [("Comparison2", S, "< <= > >= = <>"), ("Value2", D, "Threshold of B."), ("Percentile", D, "Percentile of A.")],
     note="Referenced but not specified in the manual; signature chosen to match fMC_Bayes4V.")

bayes4 = []
for i in range(1, 6):
    bayes4 += [(f"Name{i}", A, f"Variable of event {'ABCDE'[i-1]}."), (f"Compare{i}", A, "< <= > >= = <>"), (f"Value{i}", A, "Threshold.")]
fn("fMC_Bayes4", D, "P(A | B, C, D, E) for up to five events. Returns -1 if no simulation has been run.", bayes4,
   "return bayesMulti({" + ", ".join(p[0] for p in bayes4) + "}, Any());", "res")
fn("fMC_Bayes4V", D, "Value at Percentile of A given events B..E. Returns -1 if no simulation has been run.",
   bayes4 + [("Percentile", A, "Percentile of A.")], "return bayesMulti({" + ", ".join(p[0] for p in bayes4) + "}, Percentile);", "res")
fn("fMC_CorrRankN", S, "Name of the input with the Rank-th highest (absolute, Pearson) correlation with the output.",
   [N, ("Rank", D, "1 = strongest.")], "return corrRankName(Name, Rank);", "res")
fn("fMC_CorrRank", D, "Pearson correlation of the Rank-th strongest input with the output. Returns -1 if no simulation has been run.",
   [N, ("Rank", D, "1 = strongest.")], "return corrRankValue(Name, Rank);", "res")
fn("fMC_AD", D, "Anderson-Darling normality test on a variable name or a data area: TRUE (1) if very likely normal.",
   [("NameOrData", A, "Variable name or data area.")], "return andersonDarling(NameOrData);", "res")
fn("fMC_BootStrapN", S, "Bootstrap confidence interval of the mean (or of Percentile) of a variable: \"mean; lower; upper\".",
   [N, ("NumNewExamples", D, "Number of bootstrap samples."), ("Confidence", D, "Confidence level."), ("Percentile", A, "Optional percentile.")],
   "return bootstrapText(Name, nullptr, NumNewExamples, Confidence, Percentile);", "res")
fn("fMC_Kernel", "matrix", "Kernel density estimate of a variable: n x 1 smoothed sample or (ReturnHistogram) bins x 3 histogram (lower, upper, density).",
   [N, ("AdaptRate", D, "Percent by which the range is widened below the minimum and above the maximum."), ("Number_bins", D, "Histogram columns."),
    ("Bandwith", A, "Smoothing parameter (0 or empty = automatic)."), ("ReturnHistogram", A, "1 = histogram, 0 = data.")],
   "return kernel(Name, AdaptRate, Number_bins, Bandwith, ReturnHistogram);", "res")
fn("fMC_PredictiveRegressionLine", D, "Typical value at Value from simulated slope (Name1) and intercept (Name2); Percentile 0 = expected value.",
   [("Name1", S, "Slope variable."), ("Name2", S, "Intercept variable."), ("Value", D, "Point."), ("Percentile", A, "Optional percentile (0 = mean).")],
   "return predictiveLine(Name1, Name2, OUString(), Value, Percentile);", "res")
fn("fMC_PredictiveRegressionLineValue", D, "Predictive value at Value from simulated slope, intercept and standard error of regression.",
   [("Name1", S, "Slope variable."), ("Name2", S, "Intercept variable."), ("Name3", S, "Standard error variable."), ("Value", D, "Point."),
    ("Percentile", A, "Optional percentile (0 = mean).")],
   "return predictiveLine(Name1, Name2, Name3, Value, Percentile);", "res")
for nm, asym, desc in [("fMC_BayesMCMC", "false", "Gaussian proposal"), ("fMC_BayesMCMCA", "true", "asymmetric uniform proposal"),
                       ("fMC_BayesMCMCV", "false", "volatile variant"), ("fMC_BayesMCMCAV", "true", "volatile variant")]:
    fn(nm, "matrix", f"Posterior of prior Name1 and likelihood Name2 by Markov chain Monte Carlo ({desc}): n x 1 data or histogram.",
       [("Name1", S, "Prior distribution (variable)."), ("Name2", S, "Likelihood (variable of the same quantity)."), ("BurnIn", D, "Initial draws discarded."),
        ("Thinning", D, "Keep every Thinning-th draw."), ("NumberIterations", D, "Size of the posterior sample."), ("ReturnData", A, "TRUE = data, FALSE = histogram."),
        ("Number_bins", A, "Histogram columns of the output."), ("Number_binsDivide", A, "Bins used to estimate the densities.")],
       f"return mcmc(Name1, Name2, BurnIn, Thinning, NumberIterations, ReturnData, Number_bins, Number_binsDivide, {asym});", "bayes",
       "The posterior density is proportional to the product of the two (kernel-estimated) densities.")
fn("fMC_BayesPrediction", "matrix", "Predictive distribution of the failures before Count successes, using posterior success probabilities.",
   [("Name1", S, "Posterior of the success probability (variable)."), ("Name2", A, "Optional likelihood variable combined with Name1."),
    ("Count", D, "Number of successes."), ("Attempts", D, "Tolerable unsuccessful attempts (values above are reported as Attempts+1)."),
    ("NumberIterations", D, "Sample size."), ("ReturnData", A, "TRUE = data, FALSE = histogram."), ("Number_bins", A, "Histogram columns.")],
   "return bayesPrediction(Name1, Name2, Count, Attempts, NumberIterations, ReturnData, Number_bins);", "bayes",
   "The manual leaves the predictive model open; MonteCalc uses a negative-binomial predictive per posterior draw.")
comb = []
for i in range(1, 6):
    comb += [(f"Name{i}", A, f"Prior distribution {i} (variable)."), (f"Share{i}", A, f"Weight of prior {i}.")]
fn("fMC_BayesCombine", "matrix", "Combines up to five prior distributions into one (mixture): n x 1 data or histogram.",
   comb + [("ReturnData", A, "TRUE = data, FALSE = histogram."), ("NumBin", A, "Histogram columns.")],
   "return bayesCombine({" + ", ".join(p[0] for p in comb) + "}, ReturnData, NumBin);", "bayes")

# ============================================================ fMC_* information / settings
fn("fMC_Iterations", D, "Number of iterations of the last simulation.", [], "return mc::Simulation::instance().iterations();", "info")
for nm, fld in [("fMC_Best", "best"), ("fMC_Real", "real"), ("fMC_Worst", "worst")]:
    fn(nm, D, f"Confidence level of the {fld} case; with Value in [0,1] the {fld} case is set dynamically to Value.",
       [("Value", A, "Optional new level.")], f"return scenarioLevel(&mc::Settings::{fld}, Value);", "info")
fn("fMC_GetInputName", S, "Input variable at position Pos.", [("Pos", D, "Position (1-based).")], "return variableName(0, Pos);", "info")
fn("fMC_GetUserDefinedInputName", S, "User-defined input variable at position Pos.", [("Pos", D, "Position (1-based).")], "return variableName(2, Pos);", "info")
fn("fMC_GetOuputName", S, "Output variable at position Pos (name as spelled in the manual).", [("Pos", D, "Position (1-based).")], "return variableName(1, Pos);", "info")
fn("fMC_GetOutputName", S, "Output variable at position Pos.", [("Pos", D, "Position (1-based).")], "return variableName(1, Pos);", "info")
fn("fmc_Name", S, "Name of the MC FLO variable in the given cell.", [("Name", C, "Cell containing an MC FLO formula.")], "return variableInCell(Name);", "info")
fn("fMC_Bin", D, "Sets the number of histogram columns used by all histograms and functions (global).", [("Value", D, "Number of columns.")],
   "return setBins(Value);", "info")
fn("fMC_SimulaOn", D, "Workbook simulation mode: TRUE shows random values after each recalculation, FALSE the expected values.",
   [("Value", A, "TRUE or FALSE.")], "return simulaOn(Value);", "info")

# ============================================================ data tools
RG = lambda n, d: (n, R, d)
fn("fMC_Pearson", D, "Pearson correlation of two data rows/columns.", [RG("column1", "First data."), RG("column2", "Second data.")],
   "return guard([&] { return finiteOr(mc::stats::pearson(cells(column1), cells(column2))); });", "data")
fn("fMC_PearsonV", D, "Volatile variant of fMC_Pearson.", [RG("column1", "First data."), RG("column2", "Second data.")],
   "return guard([&] { return finiteOr(mc::stats::pearson(cells(column1), cells(column2))); });", "data")
fn("fMC_Spear", D, "Spearman rank correlation of two data rows/columns.", [RG("column1", "First data."), RG("column2", "Second data.")],
   "return guard([&] { return finiteOr(mc::stats::spearman(cells(column1), cells(column2))); });", "data")
fn("fMC_Sort", D, "Value at position pos of the sorted list (Sort 0 ascending, 1 descending).",
   [RG("column1", "Data."), ("pos", D, "Position (1-based)."), ("Sort", A, "0 ascending (default), 1 descending.")], "return sortValue(column1, pos, Sort);", "data")
fn("fMC_FitC", D, "Best correlation (copula) type 0..8 for two data series.", [RG("column1", "First series."), RG("column2", "Second series.")],
   "return guard([&] { std::mt19937_64 rng(1234); return double(mc::fitCopula(cells(column1), cells(column2), rng)); });", "data")
fn("fMC_BSMVal", D, "Price of the underlying after Time periods (Black-Scholes-Merton, risk-neutral): random in a simulation, expected value otherwise.",
   [("Basis", D, "Current price."), ("Time", D, "Periods."), ("Interest", D, "Riskless rate."), ("Sigma", D, "Volatility.")],
   "return bsmValue(Basis, Time, Interest, Sigma);", "data")
fn("fMC_BootStrap", S, "Bootstrap confidence interval of the mean (or Percentile) of a data area: \"mean; lower; upper\".",
   [RG("Matrix1", "Data."), ("NumNewExamples", D, "Number of bootstrap samples."), ("Confidence", D, "Confidence level."), ("Percentile", A, "Optional percentile.")],
   "return bootstrapText(OUString(), &Matrix1, NumNewExamples, Confidence, Percentile);", "data")
fn("fMC_BootStrapVector", "matrix", "Bootstrap distribution of the mean (or Percentile / hit rate / compound sum) of a data area, n x 1.",
   [RG("Matrix1", "Data."), ("NumNewExamples", D, "Number of new samples."), ("Percentile", A, "Optional percentile."),
    ("ReturnPercentH", A, "TRUE: share of hits (=1) for Bernoulli data."), ("Compound", A, "If > 1: sum of Compound random items.")],
   "return bootstrapVector(Matrix1, NumNewExamples, Percentile, ReturnPercentH, Compound);", "data")
fn("fMC_Kernel2", "matrix", "Kernel density estimate of a data area over bins: bins x 1 densities or bins x 3 (lower, upper, density).",
   [RG("Data", "Data."), ("LowerLimit", A, "Lower range limit."), ("UpperLimit", A, "Upper range limit."), ("Bandwith", A, "Bandwidth (0 = automatic)."),
    ("Number_bins", D, "Number of bins."), ("ReturnHistogram", A, "1 = bins x 3.")],
   "return kernel2(Data, LowerLimit, UpperLimit, Bandwith, Number_bins, ReturnHistogram);", "data")
fn("fMC_BayesMCMCV_Robust", "matrix", "Posterior of two data vectors (prior, likelihood) by a grid approximation (robust for sparse data), n x 1.",
   [RG("Vector1", "Prior data."), RG("Vector2", "Likelihood data.")], "return robustPosterior(Vector1, Vector2);", "bayes")
fn("fMC_AD_Data", D, "Anderson-Darling normality test on a data area (same as fMC_AD with a range).", [RG("Matrix1", "Data.")],
   "return guard([&] { return normalTest(cells(Matrix1)); });", "data")
REGP = [RG("DataY", "Dependent variable."), RG("DataX", "Independent variable(s), one column each."), ("NumberNewSamples", D, "Number of bootstrap samples."),
        ("UseRepeatedMedianRegression", A, "1 = median (repeated median) regression for one regressor.")]
for nm, col, what in [("fMC_BootStrapVectorLinearRegressionX", 1, "slope"), ("fMC_BootStrapVectorLinearRegressionY", 0, "intercept"),
                      ("fMC_BootStrapVectorLinearRegressionRE", -1, "standard error of regression")]:
    fn(nm, "matrix", f"Bootstrap distribution of the {what} of a simple linear regression, n x 1.", REGP,
       f"return bootstrapRegressionColumn(DataY, DataX, NumberNewSamples, UseRepeatedMedianRegression, {col});", "regr")
fn("fMC_BootStrapVectorMLRegression", "matrix", "Bootstrap samples of a multiple linear regression: rows (intercept, coefficients..., standard error).", REGP,
   "return bootstrapRegressionAll(DataY, DataX, NumberNewSamples, UseRepeatedMedianRegression);", "regr")
fn("fMC_BootStrapVectorMLRegressionParameters", "amatrix", "Parameter summary of a bootstrapped multiple regression: (m+2) x 4 (label, mean, lower, upper).",
   REGP + [("CI", A, "Interval (default 0.95)."), ("FeatureDescription", A, "Optional labels of the features.")],
   "return bootstrapRegressionParams(DataY, DataX, NumberNewSamples, UseRepeatedMedianRegression, CI, FeatureDescription);", "regr")
BR = [("UseLogForY", A, "1 = model log(Y)."), ("UseRepeatedMedianRegression", A, "1 = median regression (one regressor)."),
      ("NumNewSample", A, "Number of posterior samples (default 2000)."), ("kfolds", A, "Number of independent sub-samples (default 1).")]
fn("fMC_BayesPredictionRegression", D, "Forecast of a Bayesian linear regression at Value (mean, or Percentile of the predictive distribution).",
   [RG("DataY", "Dependent variable."), RG("DataX", "Independent variable(s)."), ("Value", A, "Feature value(s) of the forecast."), ("Percentile", A, "Optional percentile.")] + BR,
   "return bayesRegPredict(DataY, DataX, Value, Percentile, UseLogForY, UseRepeatedMedianRegression, NumNewSample, kfolds);", "regr",
   "Posterior from the Bayesian bootstrap (Dirichlet-weighted least squares).")
fn("fMC_BayesPredictionRegressionParameters", "amatrix", "Parameters of a Bayesian linear regression: (m+2) x 4 (label, mean, lower, upper).",
   [RG("DataY", "Dependent variable."), RG("DataX", "Independent variable(s)."), ("CI", A, "Credible interval (default 0.95)."),
    ("FeatureDescription", A, "Optional feature labels."), BR[0], BR[2], BR[1], BR[3]],
   "return bayesRegParams(DataY, DataX, CI, FeatureDescription, UseLogForY, NumNewSample, UseRepeatedMedianRegression, kfolds);", "regr")
TT = [RG("TrainDataY", "Training Y."), RG("TrainDataX", "Training X."), RG("TestDataY", "Test Y."), RG("TestDataX", "Test X.")]
fn("fMC_BayesPredictionRegressionCIP", D, "Coverage rate of the test data by the central predictive interval (1 = as expected).",
   TT + [("Percentile", A, "Coverage sought (default 0.95)."), BR[0], BR[2], BR[1], BR[3]],
   "return bayesRegTest(0, TrainDataY, TrainDataX, TestDataY, TestDataX, Percentile, UseLogForY, NumNewSample, UseRepeatedMedianRegression, kfolds).first;", "regr")
fn("fMC_BayesPredictionRegressionMAE", D, "Mean absolute error of the Bayesian regression on the test data.",
   TT + [BR[0], BR[2], BR[1], BR[3]],
   "return bayesRegTest(1, TrainDataY, TrainDataX, TestDataY, TestDataX, Any(), UseLogForY, NumNewSample, UseRepeatedMedianRegression, kfolds).first;", "regr")
fn("fMC_BayesPredictionRegressionResiduals", "matrix", "Residuals of the test data under the Bayesian regression, n x 1.",
   TT + [BR[0], BR[2], BR[1], BR[3]],
   "return bayesRegResiduals(TrainDataY, TrainDataX, TestDataY, TestDataX, UseLogForY, NumNewSample, UseRepeatedMedianRegression, kfolds);", "regr")
fn("fMC_BayesClassifier", "amatrix", "Naive Bayes classification (predictions, confusion matrix, credible intervals or feature impact).",
   [RG("FeatureData", "Features of the pre-classified data."), RG("ClassData", "Classes."), RG("PredictData", "Features to classify."),
    ("Testproportion", A, "Share of the data used as test set (default 0.2)."), ("ModelSelection", A, "0 automatic, 1 normal/Bernoulli, 2 kernel density."),
    ("ReturnConfusionMatrixTestSet", A, "TRUE = confusion matrix of the test set."), ("ReturnPredictionInterval", A, "TRUE = credible interval of the class probability."),
    ("PredictionInterval", A, "Credible level (default 0.9)."), ("Iterations", A, "Resamples for the interval (default 100)."), ("HisBins", A, "Kernel bins."),
    ("LowerLimit", A, "Kernel lower limit."), ("UpperLimit", A, "Kernel upper limit."), ("FeatureImpact", A, "TRUE = influence of each feature."),
    ("FeatureDescription", A, "Feature labels."), ("kfolds", A, "Repetitions for the confusion matrix (default 1)."),
    ("UseFlatPrior", A, "TRUE = non-informative prior."), ("UseWeightedFeatures", A, "TRUE = weight the features.")],
   "return classifier(FeatureData, ClassData, PredictData, Testproportion, ModelSelection, ReturnConfusionMatrixTestSet, ReturnPredictionInterval, "
   "PredictionInterval, Iterations, HisBins, LowerLimit, UpperLimit, FeatureImpact, FeatureDescription, kfolds, UseFlatPrior, UseWeightedFeatures);", "bayes",
   "Feature impact is the drop in test accuracy when the feature is permuted.")
fn("fMC_BayesFactor", D, "Bayes factor of H1 (p unknown) against H0 (p = ExpectedProbability) in a binomial model; with Percentile, the posterior credible value.",
   [("ExpectedProbability", D, "Probability under H0."), ("ObservedProbability", D, "Observed share."), ("NumberOfAttempts", D, "Number of trials."),
    ("NumNewSample", A, "Unused (analytic solution)."), ("Percentile", A, "Optional credible percentile."), ("Attempts", A, "Unused.")],
   "return bayesFactor(ExpectedProbability, ObservedProbability, NumberOfAttempts, Percentile);", "bayes",
   "Computed analytically (uniform prior under H1), so NumNewSample and Attempts are not needed.")
fn("fMC_FinancialDTrans", D, "Share of Amount in ActualYear when it is spread evenly over Periods years starting at Initial.",
   [("Initial", D, "Start year."), ("Periods", D, "Number of years."), ("ActualYear", D, "Year."), ("Amount", D, "Amount.")],
   "return financialDTrans(Initial, Periods, ActualYear, Amount);", "data")

# ============================================================ batch (forecasting)
BT = [RG("Matrix1", "Historical series (row or column)."), ("Number", D, "Number of forecast values."), ("ResultOrder", A, "0 line by line, 1 column by column."),
      ("Selection", A, "0 Akaike information criterion, 1 sum of residuals."), ("NumIterations", A, "Iterations for the percentile."), ("Percentil", A, "Percentile (0.5 = standard forecast).")]
fn("fMC_Batch_Time", S, "Marks a series for a batch forecast with stochastic time-series processes (run 'Start Batch').", BT, "return batchMarker(Matrix1, 0);", "batch")
for p_, label in [("D", "daily (season 7)"), ("M", "monthly (season 12)"), ("Q", "quarterly (season 4)")]:
    fn(f"fMC_Batch_HW_{p_}_Time", S, f"Marks a series for a batch Holt-Winters forecast, {label} (run 'Start Batch').", BT, "return batchMarker(Matrix1, 1);", "batch")

# ============================================================ matrix functions
fn("fMC_PortfolioCombn", D, "Number of portfolio combinations of n titles (without repetition and order).", [("AValue", D, "Number of titles.")],
   "return guard([&] { if (AValue < 0 || AValue > 60) throw std::invalid_argument(\"AValue must be 0..60\"); return std::pow(2.0, std::round(AValue)) - 1; });", "matrix")
fn("fMC_PortfolioMatrix", "amatrix", "All portfolios of the titles: rows (formula summing the MC FLO variables, portfolio name).",
   [RG("aMatrix", "Names of the titles."), ("AsInput", A, "1 = input variables (FLO_i_), else outputs (FLO_o_)."), ("Comparex", A, "Unused."),
    ("Comparesx", A, "Unused."), ("Comparevx", A, "Unused.")], "return portfolioMatrix(aMatrix, AsInput);", "matrix",
   "The Compare arguments are not described in the manual and are ignored.")
fn("fMC_PortfolioCorrMatrix", "amatrix", "Correlation formulas for all pairs of titles (random coefficients in +/-0.3 with FillRandom), q x 1.",
   [RG("aMatrix", "Names of the titles."), ("FillRandom", A, "1 = random coefficients, 0 = zero.")], "return portfolioCorr(aMatrix, FillRandom);", "matrix")
fn("fMC_PortfolioCorrMatrixF", "amatrix", "All pairwise correlations of simulated variables: rows (name1, name2, coefficient).",
   [RG("aMatrix", "Variable names."), ("CorrelT", A, "0 Pearson, otherwise Spearman.")], "return portfolioCorrF(aMatrix, CorrelT);", "matrix")
fn("fMC_PortfolioManualMatrix", "amatrix", "Portfolios from a 0-1 matrix (one column per portfolio): rows (formula, name).",
   [RG("aMatrix", "Names of the titles (n x 1)."), RG("cMatrix", "n x m matrix of 0/1."), ("isInput", A, "1 = inputs, else outputs.")],
   "return portfolioManual(aMatrix, cMatrix, isInput);", "matrix")
fn("fMC_SimpleMarkovChain", "matrix", "State distribution after n periods. aMatrix: transition matrix, optionally preceded by a row with the start distribution.",
   [RG("aMatrix", "Transition matrix (rows sum to 1)."), ("n", D, "Periods.")], "return markov(aMatrix, n);", "matrix")
fn("fMC_NBT", "amatrix", "Newcomb-Benford first-digit test (chi-squared), 18 x 3.", [RG("aMatrix", "Data."), ("UseSummary", A, "TRUE = include the test summary.")],
   "return digitTest(aMatrix, UseSummary, false);", "matrix")
fn("fMC_LDT", "amatrix", "Last-digit (before the decimal separator) uniformity test (chi-squared), 19 x 3.", [RG("aMatrix", "Data."), ("UseSummary", A, "TRUE = include the test summary.")],
   "return digitTest(aMatrix, UseSummary, true);", "matrix")

# ============================================================ generation
IDL_T = {S: "string", D: "double", A: "any", R: "sequence< sequence< any > >", C: "com::sun::star::table::XCellRange",
         "matrix": "sequence< sequence< double > >", "amatrix": "sequence< sequence< any > >"}
CPP_T = {S: "const OUString&", D: "double", A: "const css::uno::Any&", R: "const AnyMatrix&",
         C: "const css::uno::Reference< css::table::XCellRange >&"}
CPP_RET = {S: "OUString", D: "double", "matrix": "DoubleMatrix", "amatrix": "AnyMatrix"}


def idl(name):
    # UNO IDL identifiers starting with a lower-case letter may not contain '_';
    # the programmatic name is capitalised, the display name keeps the manual's spelling
    return name[0].upper() + name[1:]


MEMO_SECTIONS = {"data", "regr", "matrix"}
MEMO_EXTRA = {"fMC_BayesMCMCV_Robust", "fMC_BayesClassifier", "fMC_BayesFactor"}
NO_MEMO = {"fMC_BSMVal", "fMC_PortfolioCorrMatrixF"}


def wrap(name, ret, params, body, sec):
    """Deterministic data functions are cached on their arguments: Calc recalculates
    every formula in every iteration, and their inputs do not change."""
    if name in NO_MEMO or not (sec in MEMO_SECTIONS or name in MEMO_EXTRA):
        return body
    args = "".join(", " + p[0] for p in params)
    return f'return memo<{CPP_RET[ret]}>(memoKey("{name}"{args}), [&]() -> {CPP_RET[ret]} {{ {body} }});'


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


SECTIONS = [("cont", "Model functions: continuous distributions"), ("disc", "Model functions: discrete distributions"),
            ("ts", "Model functions: time series"), ("other", "Model functions: output, correlation, bounds"),
            ("res", "Result functions (need a simulation)"), ("info", "Information and settings"), ("data", "Data tools"),
            ("regr", "Regression"), ("bayes", "Bayesian statistics"), ("batch", "Batch forecasting"), ("matrix", "Matrix functions")]


def main(out):
    os.makedirs(out, exist_ok=True)
    seen = set()
    for f in F:
        assert f[0].upper() not in seen, f[0]
        seen.add(f[0].upper())
    with open(os.path.join(out, "XMonteCalc.idl"), "w") as h:
        h.write("// generated by tools/gen_functions.py - do not edit\n#include <com/sun/star/table/XCellRange.idl>\n")
        h.write("module org { module montecalc {\ninterface XMonteCalc : com::sun::star::uno::XInterface\n{\n")
        for name, ret, desc, params, body, sec, note in F:
            ps = ", ".join(f"[in] {IDL_T[t]} {n}" for n, t, _ in params)
            h.write(f"    {IDL_T[ret]} {idl(name)}({ps});\n")
        h.write("};\n}; };\n")
    with open(os.path.join(out, "functions_decl.inc"), "w") as h:
        for name, ret, desc, params, body, sec, note in F:
            ps = ", ".join(f"{CPP_T[t]} {n}" for n, t, _ in params)
            h.write(f"    {CPP_RET[ret]} SAL_CALL {idl(name)}({ps}) override;\n")
    with open(os.path.join(out, "functions_impl.inc"), "w") as h:
        for name, ret, desc, params, body, sec, note in F:
            ps = ", ".join(f"{CPP_T[t]} {n}" for n, t, _ in params)
            h.write(f"{CPP_RET[ret]} SAL_CALL MonteCalcAddIn::{idl(name)}({ps})\n{{\n    {wrap(name, ret, params, body, sec)}\n}}\n\n")
    with open(os.path.join(out, "functions_meta.inc"), "w") as h:
        for name, ret, desc, params, body, sec, note in F:
            args = ", ".join("{%s, %s}" % (cstr(n), cstr(d)) for n, _, d in params)
            h.write("{ %s, %s, %s, { %s } },\n" % (cstr(idl(name)), cstr(name), cstr(desc), args))
    with open(os.path.join(out, "FUNCTIONS.md"), "w") as h:
        h.write("# MonteCalc function reference\n\n_Generated by tools/gen_functions.py from the same table as the add-in._\n\n"
                "Function names follow the MC FLO user guide. Arguments in [brackets] are optional; separate arguments with `;`.\n"
                "Functions returning arrays (n x m) are entered as array formulas (Ctrl+Shift+Enter).\n")
        for key, title in SECTIONS:
            items = [f for f in F if f[5] == key]
            h.write(f"\n## {title}\n\n| Function | Description | Notes |\n|---|---|---|\n")
            for name, ret, desc, params, body, sec, note in items:
                args = "; ".join(f"[{n}]" if t == A and n not in ("Value",) and not (sec in ("cont", "disc", "ts") and n != "Name" and n not in ("Min", "Max", "Round", "Number"))
                                 else n for n, t, _ in params)
                h.write(f"| `{name}({args})` | {desc} | {note} |\n")
    print(f"generated {len(F)} functions")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/gen")
