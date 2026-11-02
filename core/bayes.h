// MonteCalc - Bayesian statistics, kernel density estimation and regression tools
#pragma once

#include <functional>
#include <random>
#include <string>
#include <vector>

namespace mc::bayes {

using Vec = std::vector<double>;
using Mat = std::vector<Vec>;   // row-major

// ---------------------------------------------------------------- kernels
double silverman(const Vec& data);
double kdeDensity(const Vec& data, double bw, double x);
// Smoothed bootstrap: n draws from the Gaussian KDE; `adapt` (fraction, e.g. 0.05)
// widens the allowed range below the minimum / above the maximum.
Vec kdeSample(const Vec& data, int n, double bw, double adapt, std::mt19937_64& rng);
// Density on `bins` equal bins between lo and hi: rows (lo, hi, density).
Mat kdeHistogram(const Vec& data, double lo, double hi, double bw, int bins);
Mat histogram3(const Vec& data, int bins);  // rows (lo, hi, share)

// ---------------------------------------------------------------- MCMC
// Metropolis-Hastings on a 1-d log density. asymmetric = false: Gaussian random walk;
// true: uniform proposal on an asymmetric interval (drift corrected in the acceptance ratio).
Vec metropolis(const std::function<double(double)>& logp, double x0, double scale, int n, int burnIn,
               int thinning, bool asymmetric, std::mt19937_64& rng);
// Posterior of a quantity described by a prior sample and a likelihood sample of the same
// quantity: p(x) proportional to prior(x) * likelihood(x) (both histogram/KDE estimates).
Vec posteriorMCMC(const Vec& prior, const Vec& likelihood, int n, int burnIn, int thinning, int bins,
                  bool asymmetric, std::mt19937_64& rng);
// Same target evaluated on a grid (robust for sparse data).
Vec posteriorGrid(const Vec& prior, const Vec& likelihood, int n, std::mt19937_64& rng);
// Mixture of several samples with weights.
Vec combine(const std::vector<Vec>& samples, const Vec& weights, int n, std::mt19937_64& rng);

// ---------------------------------------------------------------- Bayes factor (binomial)
struct BayesFactorResult {
    double bf10;        // evidence for H1 (p ~ Beta(1,1)) against H0 (p = p0)
    double posteriorMean;
    double credible;    // posterior quantile at `percentile` (if requested)
};
BayesFactorResult bayesFactor(double p0, double pObserved, double attempts, double percentile);

// ---------------------------------------------------------------- regression
// Least squares with intercept. X: rows = observations. Returns {b0, b1..bm}; sigma set.
Vec ols(const Vec& y, const Mat& x, double& sigma);
Vec ols(const Vec& y, const Mat& x, const Vec& w, double& sigma);  // weighted
// Siegel repeated-median regression (one regressor): {intercept, slope}
Vec repeatedMedian(const Vec& y, const Vec& x, double& sigma);
// Bootstrap of the regression parameters: rows {b0, b1..bm, sigma}
Mat bootstrapRegression(const Vec& y, const Mat& x, int samples, bool medianRegression, std::mt19937_64& rng);
// Bayesian regression (Bayesian bootstrap, Dirichlet weights) pooled over `kfolds`
// disjoint random subsets. Rows {b0, b1..bm, sigma}.
Mat bayesRegression(const Vec& y, const Mat& x, int samples, bool medianRegression, int kfolds,
                    std::mt19937_64& rng);
// Predictive samples at the feature row `xv` from parameter rows.
Vec predictive(const Mat& params, const Vec& xv, bool withNoise, std::mt19937_64& rng);

// ---------------------------------------------------------------- naive Bayes classifier
struct ClassifierOptions {
    int model = 0;              // 0 automatic, 1 normal/Bernoulli, 2 kernel density
    bool flatPrior = false;
    bool weightedFeatures = false;
    int bins = 30;
    double lower = 0, upper = 0; // kernel range (0,0 = from data)
};
class NaiveBayes {
public:
    void train(const Mat& x, const std::vector<std::string>& y, const ClassifierOptions& o);
    // class probabilities in the order of classes()
    Vec probabilities(const Vec& row) const;
    std::string predict(const Vec& row) const;
    const std::vector<std::string>& classes() const { return classes_; }

private:
    ClassifierOptions o_;
    std::vector<std::string> classes_;
    Vec prior_;
    std::vector<int> featureKind_;            // 0 normal, 1 Bernoulli, 2 kernel
    std::vector<std::vector<Vec>> data_;      // [class][feature] -> values
    std::vector<std::vector<double>> mu_, sd_, pb_;
    std::vector<double> weight_;
    std::vector<double> bw_;
};

// Benford / last digit tests: returns observed shares, expected shares, chi2, p-value
struct DigitTest { Vec observed, expected; double chi2, pValue, n, mad; };
DigitTest benford(const Vec& data);
DigitTest lastDigit(const Vec& data);

} // namespace mc::bayes
