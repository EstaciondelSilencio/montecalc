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
# Names and argument order as in the MC FLO wiki: parameters first, then Variable_name and
# Number; MonteCalc adds the options Min, Max and Round after Number.
NAMEP = ("Variable_name", A, "Variable name (text). Leave empty for an unnamed input (Register variables names it).")
NUMP = ("Number", A, "Optional number of occurrences per iteration; the draws are summed (compound distribution). 0 or empty = 1.")
OPT = [("Min", A, "Optional lower bound (truncation)."), ("Max", A, "Optional upper bound (truncation)."),
       ("Round", A, "Optional number of decimals (TRUE = whole numbers).")]

CONT = [
    ("Uniform", "Continuous uniform distribution.", ["Lower_Limit", "Upper_Limit"], ["Uniforme"]),
    ("Triangular", "Triangular distribution.", ["Lower_Limit", "Upper_Limit", "Mode"], []),
    ("PERT", "PERT distribution.", ["Lower_Limit", "Upper_Limit", "Mode"], []),
    ("Normal", "Normal distribution.", ["Mean", "StDeviation"], []),
    ("LogNormal", "Log-normal distribution: Shift + exp(Normal(Mean; StDeviation)).", ["Shift", "Mean", "StDeviation"], []),
    ("Beta", "Beta distribution between Lower_Limit and Upper_Limit.", ["param_alfa", "param_beta", "Lower_Limit", "Upper_Limit"], []),
    ("Gumbel", "Gumbel (extreme value) distribution: location Shift, scale param_alfa.", ["Shift", "param_alfa"], []),
    ("Gamma", "Gamma distribution: Shift + Gamma(scale param_alfa, shape param_beta).", ["Shift", "param_alfa", "param_beta"], []),
    ("Exponential", "Exponential distribution: Shift + Exponential(mean param_alfa).", ["Shift", "param_alfa"], []),
    ("TruncNormal", "Truncated normal distribution.", ["Mean", "StDeviation", "Lower_Limit", "Upper_Limit"], ["TruncatedNormal"]),
    ("Weibull", "Weibull distribution: Shift + Weibull(scale param_alfa, shape param_beta).", ["Shift", "param_alfa", "param_beta"], []),
    ("chi2", "Chi-square distribution.", ["Freedom"], []),
    ("studentt", "Student-t distribution.", ["Freedom"], []),
    ("Pareto", "Pareto distribution with minimum Shift (> 0) and shape param_alfa.", ["Shift", "param_alfa"], []),
    ("Fisher", "F distribution.", ["Freedom1", "Freedom2"], []),
    ("ArcSin", "Arcsine distribution.", ["Lower_Limit", "Upper_Limit"], []),
    ("Cauchy", "Cauchy distribution: location Shift, scale param_alfa (the cell shows the median).", ["Shift", "param_alfa"], []),
    ("Frechet", "Frechet distribution: minimum Start, scale Shift, shape param_alfa.", ["Start", "Shift", "param_alfa"], []),
    ("Logistic", "Logistic distribution: location Location, scale Shift.", ["Location", "Shift"], []),
]
DISC = [
    ("DiscreteUniform", "Discrete uniform distribution (integers).", ["Lower_Limit", "Upper_Limit"], ["DiscreteUniforme"]),
    ("Bernoulli", "Bernoulli distribution: 1 (TRUE) with the given probability, else 0.", ["Probability"], []),
    ("Binomial", "Binomial distribution: successes in Tries trials.", ["Probability", "Tries"], []),
    ("Poisson", "Poisson distribution.", ["Occurency"], []),
    ("Pascal", "Pascal (negative binomial) distribution: failed attempts until Success successes; Probability = probability of a failure.", ["Probability", "Success"], []),
    ("Geometric", "Geometric distribution: failures before the first success.", ["Probability"], []),
    ("Hypergeometric", "Hypergeometric distribution (draws without replacement).", ["Tries", "Population", "Success_Possibles"], []),
]
TS = [
    ("TemporalS_ARmodel", "AR(1) process around the level Start.", ["Start", "Weight", "StDeviation", "UseBefore"]),
    ("TemporalS_MAmodel", "MA(1) process.", ["Start", "Weight", "StDeviation"]),
    ("TemporalS_ARMAmodel", "ARMA(1,1) process.", ["Start", "Weight1", "StDeviation1", "Weight2"]),
    ("TemporalS_ARCHmodel", "ARCH(1) process (mean 0, non-constant volatility).", ["Omega", "Alpha"]),
    ("TemporalS_WPmodel", "Wiener process (geometric Brownian motion); StDeviation is optional.", ["Start", "Drift", "Mu", "StDeviation", "UseBefore"]),
]


def flo_body(name, params):
    return f'return flo("{name}", {{{", ".join(p[0] for p in params)}}});'


def pid(n):
    # parameter identifier valid in UNO IDL (lower-case start + '_' is not allowed): param_alfa -> Param_alfa
    return n[0].upper() + n[1:] if n[0].islower() and "_" in n else n


for section, table in (("cont", CONT), ("disc", DISC)):
    for lab, desc, ps, aliases in table:
        params = [(pid(p), A, p.replace("_", " ") + ".") for p in ps] + [NAMEP, NUMP] + OPT
        for nm in [lab] + aliases:
            fn("FLOsimula_" + nm, D, "Input variable: " + desc, params, flo_body("FLOsimula_" + nm, params), section,
               "" if nm == lab else "Alias of FLOsimula_" + lab + " (MC FLO compatibility).")
TSNUM = ("Number", A, "Optional number of periods. 0 / empty: the path continues from iteration to iteration (5,000 iterations = 5,000 periods); n: every iteration starts at Start and returns period n.")
for lab, desc, ps in TS:
    params = [(p, A, p + ".") for p in ps] + [NAMEP, TSNUM] + OPT
    fn("FLOsimula_" + lab, D, "Input variable: " + desc, params, flo_body("FLOsimula_" + lab, params), "ts",
       "The (correlated) random number drives the noise of each iteration; UseBefore = 1 restarts every iteration at Start.")
p = [("Data", A, "Data area: raw data, a histogram (Min, Max, Number, ...) or quantile/probability (QP) pairs."),
     ("NumberOfBins", A, "Number of intervals for raw data (0 = empirical distribution)."), NAMEP, NUMP] + OPT
fn("FLOsimula_UserDefined", D, "Input variable: user-defined distribution from a data area.", p, flo_body("FLOsimula_UserDefined", p), "cont",
   "Two-column areas are read as (quantile, value) rows; if the quantiles are not all in [0,1] the last row is (minimum, maximum), as in the manual.")
p = [("Values", A, "Possible values."), ("Probabilities", A, "Probabilities or weights."), NAMEP, NUMP] + OPT
fn("FLOsimula_DiscreteUserDefined", D, "Input variable: user-defined discrete distribution.", p, flo_body("FLOsimula_DiscreteUserDefined", p), "disc")
p = [("Variable", A, "Cell of a registered MC FLO variable."), ("LowerLimit", A, "Lower limit."), ("UpperLimit", A, "Upper limit."), NAMEP]
fn("FLOsimula_Bounded", D, "Input variable: truncates an existing MC FLO variable to [LowerLimit, UpperLimit].", p, flo_body("FLOsimula_Bounded", p), "other",
   "Only values within the limits are drawn. Before a simulation the cell shows the referenced variable's expected value.")
p = [("Name", A, "Name of the output variable."), ("Value", A, "Optional value; if omitted the function returns 0 so that it can be added to a cell: =B30+FLOsimula_output(\"NPV\").")]
fn("FLOsimula_output", D, "Output variable.", p, flo_body("FLOsimula_output", p), "other")
p = [("Variable1", A, "First input variable (name or cell with the name)."), ("Variable2", A, "Second input variable."),
     ("Coef_corr", A, "Correlation coefficient between -1 and 1."),
     ("CorType", A, "0 Normal, 1 Clayton, 2 rClayton, 3 Gumbel, 4 rGumbel, 5 Student-t, 6 asy. Gumbel, 7 asy. Triangular, 8 asy. Frechet.")]
fn("FLOsimula_correlation", D, "Correlation between two input variables; returns the coefficient.", p, flo_body("FLOsimula_correlation", p), "other",
   "Type codes as in the MC FLO wiki. Archimedean and asymmetric correlations are bivariate.")

# ============================================================ fMC_* result functions
N = ("Name", S, "Name of a simulated variable.")
REF = ("Referenz", A, "Optional variant of a multiple simulation (variant name); empty = the selected variant.")
VS = "VariantScope vs_(Referenz); "


def res(name, desc, expr, extra=(), note=""):
    fn(name, D, desc + " Returns -1 if no simulation has been run.", [N] + list(extra) + [REF],
       VS + f"return withSamples(Name, [&](const Vec& v) {{ return {expr}; }});", "res", note)


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
res("fMC_Percentile", "Value at the percentile: the ROUNDUP(n * Percentile)-th smallest value.", "percentileMcflo(v, Percentile)",
    [("Percentile", D, "Percentile (0..1).")])
res("fMC_PercentileValue", "Percentile (0..1) of a given value: position of the first value >= Value among the sorted values.",
    "percentValueMcflo(v, Value)", [("Value", D, "Value.")])
res("fMC_PercentileInterval", "Confidence level (share of iterations) between the limits.", "mc::stats::probBetween(v, Lower_Limit, Upper_Limit)",
    [("Lower_Limit", D, "Lower limit."), ("Upper_Limit", D, "Upper limit.")])
res("fMC_Mode", "Most frequent value based on a histogram.", "mc::stats::mode(v, int(Number_bins))", [("Number_bins", D, "Number of histogram columns.")])
res("fMC_Density", "Share of the iterations at Value (discrete) or in the histogram column containing Value.", "densityShare(v, Value, Number_bins)",
    [("Value", D, "Point."), ("Number_bins", A, "Histogram columns (empty = setting).")])
res("fMC_ExtremeValue", "Value at position Pos of the sorted list.", "sortedValue(v, Pos)", [("Pos", D, "Position (1 = smallest).")])
res("fMC_PosValue", "Value at iteration Pos.", "iterValue(v, Pos)", [("Pos", D, "Iteration (1-based).")])
res("fMC_ConditionalPosValue", "Position (iteration) whose value equals Value, or the nearest one.", "conditionPos(v, Value)", [("Value", D, "Value.")])
res("fMC_ConditionPosValue", "Position (iteration) whose value equals Value, or the nearest one (alias of fMC_ConditionalPosValue).", "conditionPos(v, Value)", [("Value", D, "Value.")])
res("fMC_ADs", "Anderson-Darling normality test of a variable: 1 if very likely normal, else 0.", "normalTest(v)")


def pair(name, desc, expr, extra=(), n1="Name1", n2="Name2", note=""):
    fn(name, D, desc + " Returns -1 if no simulation has been run.", [(n1, S, "First variable."), (n2, S, "Second variable.")] + list(extra) + [REF],
       VS + f"return withPair({n1}, {n2}, [&](const Vec& a, const Vec& b) {{ return {expr}; }});", "res", note)


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
fn(name, ret, desc, [params[0], params[2], params[3], params[1], params[4], params[5], params[6]], body, sec, note)

bayesV = [("Name1", A, "Variable of event A."), ("Vergleich1", A, "< <= > >= = <> (or empty)."), ("Wert1", A, "Threshold of A."),
          ("Name2", A, "Variable of event B."), ("Vergleich", A, "< <= > >= = <>"), ("Wert2", A, "Threshold of B.")]
for nm in ("fMC_BayesV", "fMC_BayesV_MC"):
    fn(nm, D, "Average of A (Name1) over the iterations where A and event B (Name2) meet their conditions. Returns -1 if no simulation has been run.",
       bayesV + [REF], VS + "return bayesMulti({" + ", ".join(p[0] for p in bayesV) + "}, true);", "res")

bayes4 = []
for i in range(1, 6):
    bayes4 += [(f"Name{i}", A, f"Variable of event {'ABCDE'[i-1]}."), (f"Compare{i}", A, "< <= > >= = <>"), (f"Value{i}", A, "Threshold.")]
fn("fMC_Bayes4", D, "P(A | B, C, D, E) for up to five events; a condition may also be given as one text (\">=\"&100). Returns -1 if no simulation has been run.",
   bayes4 + [REF], VS + "return bayesMulti({" + ", ".join(p[0] for p in bayes4) + "}, false);", "res")
for nm in ("fMC_Bayes4V", "fMC_Bayes4V_MC"):
    fn(nm, D, "Average of A over the iterations where A and the events B..E meet their conditions. Returns -1 if no simulation has been run.",
       bayes4 + [REF], VS + "return bayesMulti({" + ", ".join(p[0] for p in bayes4) + "}, true);", "res")
fn("fMC_CorrRankN", S, "Name of the input with the Rank-th highest (absolute, Pearson) correlation with the output.",
   [N, ("Rank", D, "1 = strongest."), REF], VS + "return corrRankName(Name, Rank);", "res")
fn("fMC_CorrRank", D, "Pearson correlation of the Rank-th strongest input with the output. Returns -1 if no simulation has been run.",
   [N, ("Rank", D, "1 = strongest."), REF], VS + "return corrRankValue(Name, Rank);", "res")
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
        ("Number_bins", A, "Histogram columns of the output."), ("Number_binsDivide", A, "Bins used to estimate the densities."), REF],
       VS + f"return mcmc(Name1, Name2, BurnIn, Thinning, NumberIterations, ReturnData, Number_bins, Number_binsDivide, {asym});", "bayes",
       "The posterior density is proportional to the product of the two (kernel-estimated) densities.")
fn("fMC_BayesPrediction", "matrix", "Predictive distribution of the failures before Count successes, using posterior success probabilities.",
   [("Name1", S, "Posterior of the success probability (variable)."), ("Name2", A, "Optional likelihood variable combined with Name1."),
    ("Count", D, "Number of successes."), ("NumberIterations", D, "Sample size."),
    ("Attempts", D, "Tolerable unsuccessful attempts (values above are reported as Attempts+1)."),
    ("ReturnData", A, "TRUE = data, FALSE = histogram."), ("Number_bins", A, "Histogram columns."), REF],
   VS + "return bayesPrediction(Name1, Name2, Count, Attempts, NumberIterations, ReturnData, Number_bins);", "bayes",
   "The manual leaves the predictive model open; MonteCalc uses a negative-binomial predictive per posterior draw.")
comb = []
for i in range(1, 6):
    comb += [(f"Name{i}", A, f"Prior distribution {i} (variable)."), (f"Share{i}", A, f"Weight of prior {i}.")]
fn("fMC_BayesCombine", "matrix", "Combines up to five prior distributions into one (mixture): n x 1 data or histogram.",
   comb + [("ReturnData", A, "TRUE = data, FALSE = histogram."), ("NumBin", A, "Histogram columns."), REF],
   VS + "return bayesCombine({" + ", ".join(p[0] for p in comb) + "}, ReturnData, NumBin);", "bayes")

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
   "return guard([&] { std::mt19937_64 rng(1234); return double(mc::copulaToMcflo(mc::fitCopula(cells(column1), cells(column2), rng))); });", "data")
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
    ("Number_bins", A, "Number of bins (default 30)."), ("KernelType", A, "Kernel (currently Gauss)."), ("ReturnHistogram", A, "1 = bins x 3."),
    ("ReturnSample", A, "1 = return a sample of size Sample from the estimated density."), ("Sample", A, "Sample size (default 1000).")],
   "return kernel2(Data, LowerLimit, UpperLimit, Bandwith, Number_bins, ReturnHistogram, ReturnSample, Sample);", "data")
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
    ("FeatureDescription", A, "Optional feature labels.")] + BR,
   "return bayesRegParams(DataY, DataX, CI, FeatureDescription, UseLogForY, NumNewSample, UseRepeatedMedianRegression, kfolds);", "regr")
TT = [RG("TrainDataY", "Training Y."), RG("TrainDataX", "Training X."), RG("TestDataY", "Test Y."), RG("TestDataX", "Test X.")]
fn("fMC_BayesPredictionRegressionCIP", D, "Coverage rate of the test data by the central predictive interval (1 = as expected).",
   TT + [("Percentile", A, "Coverage sought (default 0.95).")] + BR,
   "return bayesRegTest(0, TrainDataY, TrainDataX, TestDataY, TestDataX, Percentile, UseLogForY, NumNewSample, UseRepeatedMedianRegression, kfolds).first;", "regr")
fn("fMC_BayesPredictionRegressionMAE", D, "Mean absolute error of the Bayesian regression on the test data.",
   TT + [("Percentile", A, "Unused (MC FLO signature).")] + BR,
   "return bayesRegTest(1, TrainDataY, TrainDataX, TestDataY, TestDataX, Any(), UseLogForY, NumNewSample, UseRepeatedMedianRegression, kfolds).first;", "regr")
fn("fMC_BayesPredictionRegressionResiduals", "matrix", "Residuals of the test data under the Bayesian regression, n x 1.",
   TT + [("Percentile", A, "Unused (MC FLO signature).")] + BR,
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



# ============================================================ MC FLO compatibility: _MC variants (Memory Calculation Engine)
def alias(new, old, note=None):
    for f in F:
        if f[0] == old:
            fn(new, f[1], f[2], f[3], f[4], f[5], note or ("Same as " + old + " (MC FLO Memory Calculation name)."))
            return
    raise SystemExit("alias: unknown " + old)


for old in ["fMC_BayesPredictionRegression", "fMC_BayesPredictionRegressionCIP", "fMC_BayesPredictionRegressionMAE"]:
    alias(old + "V_MC", old)
for old in ["fMC_BayesPredictionRegressionParameters", "fMC_BayesPredictionRegressionResiduals", "fMC_BayesClassifier",
            "fMC_BootStrapVectorMLRegression", "fMC_BootStrapVectorMLRegressionParameters"]:
    alias(old + "_MC", old)

# Holt-Winters with given smoothing factors (wiki "Forecast-Tool")
for nm, mult in (("fMC_HWAmodel", "false"), ("fMC_HWMmodel", "true")):
    fn(nm, "matrix", "Holt-Winters forecast (" + ("multiplicative" if mult == "true" else "additive") + ") with the given smoothing factors, Number x 1.",
       [("alpha", D, "Smoothing factor of the level."), ("beta", D, "Smoothing factor of the trend."), ("gamma", D, "Smoothing factor of the seasonality."),
        ("Period", D, "Season length: 7 daily, 4 quarterly, 12 monthly data."), RG("Matrix1", "Historical data (at least 2 x Period values)."),
        ("Number", A, "Number of forecasts (default 1)."), ("ResultOrder", A, "0 = forecasts only, 1 = fitted history followed by the forecasts.")],
       f"return hwModel(alpha, beta, gamma, Period, Matrix1, Number, ResultOrder, {mult});", "batch")

# Memory Calculation vector functions (wiki "Memory Calculation"): _simula_<Distribution>_MC(parameters; [count])
for section, table in (("mem", CONT), ("mem", DISC)):
    for lab, desc, ps, aliases in table:
        params = [(pid(p), A, p.replace("_", " ") + ".") for p in ps] + [("Count", A, "Number of random numbers (default 30,000; at most 1,048,576).")]
        fn("_simula_" + lab + "_MC", "matrix", "Memory Calculation: column of random numbers of the " + desc[0].lower() + desc[1:], params,
           f'return simulaMC("FLOsimula_{lab}", {{{", ".join(p[0] for p in params[:-1])}}}, Count);', section)
fn("_simula_calculation_MC", "matrix", "Memory Calculation: row-wise operation on two vectors (+ - * / */+ */- //+ //- Max Min com csme inc const).",
   [("Vector1", A, "First vector (or matrix for + and *), or a constant."), ("Vector2", A, "Second vector or constant."), ("MMOperation", S, "Operation.")],
   "return calculationMC(Vector1, Vector2, MMOperation);", "mem")
fn("_simula_correlation_MC", "matrix", "Memory Calculation: correlates two vectors (rank reordering), n x 2.",
   [("Vector1", A, "First vector."), ("Vector2", A, "Second vector."), ("CorrCoef", D, "Correlation coefficient."),
    ("CorrType", A, "MC FLO correlation type (0 Normal ... 8 asy. Frechet)."), ("DoSort", A, "TRUE = sort the pairs by Vector1.")],
   "return correlationMC(Vector1, Vector2, CorrCoef, CorrType, DoSort);", "mem")
fn("_simula_Bounded_MC", "matrix", "Memory Calculation: keeps the values of Vector1 within [LowerBound, UpperBound].",
   [("Vector1", A, "Vector."), ("LowerBound", D, "Lower bound."), ("UpperBound", D, "Upper bound."), ("Number", A, "Optional size of the result (resampled).")],
   "return boundedMC(Vector1, LowerBound, UpperBound, Number);", "mem")


def idl(name):
    # UNO IDL identifiers starting with a lower-case letter may not contain '_';
    # the programmatic name is capitalised, the display name keeps the manual's spelling
    name = name.lstrip("_")   # _simula_*_MC: IDL identifiers cannot start with '_'
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
            ("regr", "Regression"), ("bayes", "Bayesian statistics"), ("batch", "Batch forecasting"), ("matrix", "Matrix functions"),
            ("mem", "Memory Calculation vector functions")]


# ---------------------------------------------------------------- Excel XLL (xll/)
XLL_T = dict(CPP_T)
XLL_T[C] = "const montecalc::XlCell&"
XLL_ARG = {S: "argS", D: "argD", A: "argA", R: "argR", C: "argC"}
XLL_NONVOLATILE = {"data", "batch"}   # pure functions of their arguments; all others read the simulation


def xll_category(name):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from gen_ui import FUNCTION_MENU   # the MC FLO "Functions" categories
    for cat, names in FUNCTION_MENU:
        if name in names:
            return cat
    return "MC FLO Model functions"


def write_xll(out):
    """declarations without the UNO interface, one exported C function per spreadsheet function,
    the registration table (xlfRegister) and the export list for the .def file"""
    with open(os.path.join(out, "functions_decl_xll.inc"), "w") as h:
        for name, ret, desc, params, body, sec, note in F:
            ps = ", ".join(f"{XLL_T[t]} {n}" for n, t, _ in params)
            h.write(f"    {CPP_RET[ret]} {idl(name)}({ps});\n")
    with open(os.path.join(out, "functions_impl_xll.inc"), "w") as h:
        for name, ret, desc, params, body, sec, note in F:
            ps = ", ".join(f"{XLL_T[t]} {n}" for n, t, _ in params)
            h.write(f"{CPP_RET[ret]} MonteCalcAddIn::{idl(name)}({ps})\n{{\n    {wrap(name, ret, params, body, sec)}\n}}\n\n")
    with open(os.path.join(out, "xll_exports.inc"), "w") as h:
        h.write("// generated by tools/gen_functions.py - do not edit\n")
        for name, ret, desc, params, body, sec, note in F:
            ps = ", ".join(f"LPXLOPER12 a{i}" for i in range(len(params)))
            args = ", ".join(f"montecalc::xll::{XLL_ARG[t]}(a{i})" for i, (n, t, _) in enumerate(params))
            ops = ", ".join(f"a{i}" for i in range(len(params)))
            flo = "true" if name.startswith("FLOsimula_") else "false"
            h.write(f"XLL_EXPORT LPXLOPER12 XLL_CALL mc_{idl(name)}({ps}) {{\n"
                    f"    return montecalc::xll::call({flo}, {{{ops}}}, [&] {{ return montecalc::xll::result(montecalc::xll::addin().{idl(name)}({args})); }});\n"
                    f"}}\n")
    with open(os.path.join(out, "xll_register.inc"), "w") as h:
        h.write("// generated by tools/gen_functions.py - do not edit\n")
        for name, ret, desc, params, body, sec, note in F:
            macro = any(t == C for _, t, _ in params)
            typ = "Q" + "".join("U" if t == C else "Q" for _, t, _ in params)
            if macro:
                typ += "#"
            elif sec not in XLL_NONVOLATILE:
                typ += "!"
            argnames = ",".join(n for n, _, _ in params)
            adesc = ", ".join(cstr(d[:250]) for _, _, d in params)
            h.write("{%s, %s, %s, %s, %s, %s, {%s}},\n" % (cstr("mc_" + idl(name)), cstr(name), cstr(typ), cstr(argnames[:250]),
                                                           cstr(xll_category(name)), cstr(desc[:250]), adesc))
    with open(os.path.join(out, "montecalc_xll.def"), "w") as h:
        h.write("EXPORTS\n    xlAutoOpen\n    xlAutoClose\n    xlAutoFree12\n    xlAddInManagerInfo12\n")
        for cmd in XLL_COMMANDS:
            h.write(f"    {cmd}\n")
        for name, *_ in F:
            h.write(f"    mc_{idl(name)}\n")


# command functions of the XLL (xll/xll.cpp), exported by name
XLL_COMMANDS = ["mcStart", "mcReset", "mcSettings", "mcLanguage", "mcAbout", "mcInsertOutput", "mcLangAuto", "mcLangEnglish",
                "mcLangGerman", "mcLangSpanish"]


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
    write_xll(out)
    print(f"generated {len(F)} functions")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/gen")
