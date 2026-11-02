// MonteCalc - dependence between input variables (copulas)
//
// Correlation coefficients entered by the user are interpreted as Spearman rank
// correlations. Type codes:
//   0 Gaussian            (elliptical)
//   1 Student-t, 4 d.o.f. (elliptical, tail dependence)
//   2 Clayton             (lower-tail dependence)
//   3 rotated Clayton     (upper-tail dependence, 180 degrees)
//   4 Gumbel              (upper-tail dependence)
//   5 rotated Gumbel      (lower-tail dependence, 180 degrees)
//   6 asymmetric Gumbel   (Khoudraji asymmetrisation of Gumbel)
//   7 asymmetric Frechet  (Marshall-Olkin, mixture of comonotone and independent parts)
//   8 asymmetric triangular (Khoudraji asymmetrisation of Clayton)
// Elliptical correlations form one joint matrix (repaired to the nearest valid
// matrix if necessary). Archimedean and asymmetric copulas (2..8) are bivariate only: a
// variable may take part in at most one Archimedean pair and in no other
// correlation.
#pragma once

#include <random>
#include <string>
#include <vector>

namespace mc {

constexpr int kMaxCopulaType = 8;

struct CorrelationSpec {
    int a = 0, b = 0;   // variable indices
    double rho = 0;     // Spearman rank correlation in (-1,1)
    int type = 0;
};

struct CopulaResult {
    // u[var][iteration]
    std::vector<std::vector<double>> u;
    std::vector<std::string> warnings;
};

// Generate `iterations` uniforms for `nvars` variables honouring `corr`.
CopulaResult generateUniforms(int nvars, int iterations, const std::vector<CorrelationSpec>& corr,
                              std::mt19937_64& rng);

// Helpers exposed for testing
double spearmanToPearson(double rhoS);   // Gaussian copula mapping
double spearmanToKendall(double rhoS);   // via the Gaussian relationship
bool nearestCorrelation(std::vector<std::vector<double>>& m); // returns true if adjusted
std::pair<double, double> samplePair(int type, double theta, std::mt19937_64& rng);
double calibrateTheta(int type, double rho, std::vector<std::string>& warnings);
// Best copula type (0..8) for paired data by comparing empirical copulas.
int fitCopula(const std::vector<double>& x, const std::vector<double>& y, std::mt19937_64& rng);
bool cholesky(const std::vector<std::vector<double>>& m, std::vector<std::vector<double>>& l);

} // namespace mc
