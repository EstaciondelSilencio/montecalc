// MonteCalc - copulas
#include "copula.h"
#include "specfun.h"
#include "stats.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace mc {

using Matrix = std::vector<std::vector<double>>;

double spearmanToPearson(double rs) { return 2 * std::sin(kPi * rs / 6); }
double spearmanToKendall(double rs) { return 2 / kPi * std::asin(spearmanToPearson(rs)); }

bool cholesky(const Matrix& m, Matrix& l) {
    size_t n = m.size();
    l.assign(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j <= i; ++j) {
            double s = m[i][j];
            for (size_t k = 0; k < j; ++k) s -= l[i][k] * l[j][k];
            if (i == j) {
                if (s <= 1e-12) return false;
                l[i][i] = std::sqrt(s);
            } else {
                l[i][j] = s / l[j][j];
            }
        }
    }
    return true;
}

// Cyclic Jacobi eigen decomposition of a symmetric matrix.
static void jacobiEigen(Matrix a, std::vector<double>& eval, Matrix& evec) {
    size_t n = a.size();
    evec.assign(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) evec[i][i] = 1;
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0;
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n; ++j) off += a[i][j] * a[i][j];
        if (off < 1e-22) break;
        for (size_t p = 0; p < n; ++p)
            for (size_t q = p + 1; q < n; ++q) {
                if (std::fabs(a[p][q]) < 1e-300) continue;
                double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                double t = (theta >= 0 ? 1 : -1) / (std::fabs(theta) + std::sqrt(theta * theta + 1));
                double c = 1 / std::sqrt(t * t + 1), s = t * c;
                for (size_t k = 0; k < n; ++k) {
                    double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (size_t k = 0; k < n; ++k) {
                    double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (size_t k = 0; k < n; ++k) {
                    double vkp = evec[k][p], vkq = evec[k][q];
                    evec[k][p] = c * vkp - s * vkq;
                    evec[k][q] = s * vkp + c * vkq;
                }
            }
    }
    eval.resize(n);
    for (size_t i = 0; i < n; ++i) eval[i] = a[i][i];
}

bool nearestCorrelation(Matrix& m) {
    Matrix l;
    if (cholesky(m, l)) return false;
    size_t n = m.size();
    double floor = 1e-6;
    for (int attempt = 0; attempt < 30; ++attempt) {
        std::vector<double> ev;
        Matrix v;
        jacobiEigen(m, ev, v);
        for (auto& e : ev) e = std::max(e, floor);
        Matrix r(n, std::vector<double>(n, 0.0));
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j)
                for (size_t k = 0; k < n; ++k) r[i][j] += v[i][k] * ev[k] * v[j][k];
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j)
                if (i != j) r[i][j] /= std::sqrt(r[i][i] * r[j][j]);
        for (size_t i = 0; i < n; ++i) r[i][i] = 1;
        m = r;
        if (cholesky(m, l)) return true;
        floor *= 10;
    }
    throw std::runtime_error("correlation matrix could not be repaired");
}

namespace {

struct UnionFind {
    std::vector<int> p;
    explicit UnionFind(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) { return p[x] == x ? x : p[x] = find(p[x]); }
    void unite(int a, int b) { p[find(a)] = find(b); }
};

double chi2Sample(double df, std::mt19937_64& rng) {
    return std::gamma_distribution<double>(df / 2, 2.0)(rng);
}

// Positive stable variable with Laplace transform exp(-t^alpha), 0<alpha<1 (Kanter).
double positiveStable(double alpha, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    double u = kPi * uni(rng);
    double w = std::exponential_distribution<double>(1.0)(rng);
    if (u <= 0) u = 1e-12;
    double a = std::pow(std::sin(alpha * u), alpha / (1 - alpha)) * std::sin((1 - alpha) * u) /
               std::pow(std::sin(u), 1 / (1 - alpha));
    return std::pow(a / w, (1 - alpha) / alpha);
}

double clampU(double u) { return std::min(std::max(u, 1e-15), 1 - 1e-15); }

} // namespace

// ---- bivariate families --------------------------------------------------
// theta is the family parameter (strength); every family is calibrated by simulation
// so that the Spearman rank correlation matches the requested coefficient.
std::pair<double, double> samplePair(int type, double theta, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::exponential_distribution<double> ex(1.0);
    auto clayton = [&](double th) -> std::pair<double, double> {
        if (th < 1e-9) return {uni(rng), uni(rng)};
        double v = std::gamma_distribution<double>(1 / th, 1.0)(rng);
        return {std::pow(1 + ex(rng) / v, -1 / th), std::pow(1 + ex(rng) / v, -1 / th)};
    };
    auto gumbel = [&](double th) -> std::pair<double, double> {
        if (th <= 1 + 1e-9) return {uni(rng), uni(rng)};
        double v = positiveStable(1 / th, rng);
        return {std::exp(-std::pow(ex(rng) / v, 1 / th)), std::exp(-std::pow(ex(rng) / v, 1 / th))};
    };
    // Khoudraji asymmetrisation: C(u,v) = u^(1-a) v^(1-b) C0(u^a, v^b)
    auto khoudraji = [&](std::pair<double, double> base, double a, double b) -> std::pair<double, double> {
        double w1 = uni(rng), w2 = uni(rng);
        double u = a >= 1 ? base.first : std::max(std::pow(base.first, 1 / a), std::pow(w1, 1 / (1 - a)));
        double v = b >= 1 ? base.second : std::max(std::pow(base.second, 1 / b), std::pow(w2, 1 / (1 - b)));
        return {u, v};
    };
    switch (type) {
    case 2: return clayton(theta);
    case 3: { auto p = clayton(theta); return {1 - p.first, 1 - p.second}; }
    case 4: return gumbel(1 + theta);
    case 5: { auto p = gumbel(1 + theta); return {1 - p.first, 1 - p.second}; }
    case 6: return khoudraji(gumbel(1 + theta), 1.0, 0.6);           // asymmetric Gumbel
    case 7: {                                                         // asymmetric Frechet (Marshall-Olkin)
        double m = uni(rng);
        return khoudraji({m, m}, std::min(1.0, theta), std::min(1.0, theta) / 2);
    }
    case 8: return khoudraji(clayton(theta), 0.6, 1.0);              // asymmetric triangular
    default: return {uni(rng), uni(rng)};
    }
}

double familySpearman(int type, double theta, int n, unsigned long long seed) {
    std::mt19937_64 rng(seed);
    std::vector<double> a(n), b(n);
    for (int i = 0; i < n; ++i) { auto p = samplePair(type, theta, rng); a[i] = p.first; b[i] = p.second; }
    return stats::spearman(a, b);
}

double calibrateTheta(int type, double rho, std::vector<std::string>& warnings) {
    if (rho < 1e-6) return 0;
    double lo = 0, hi = type == 7 ? 1.0 : 60.0;
    const int n = 4000;
    double maxRho = familySpearman(type, hi, n, 99);
    if (rho >= maxRho) {
        warnings.push_back("rank correlation " + std::to_string(rho) + " exceeds what copula type " + std::to_string(type) +
                           " can reach; using " + std::to_string(maxRho));
        return hi;
    }
    for (int i = 0; i < 30; ++i) {
        double mid = 0.5 * (lo + hi);
        if (familySpearman(type, mid, n, 99) < rho) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

int fitCopula(const std::vector<double>& x, const std::vector<double>& y, std::mt19937_64& rng) {
    size_t n = std::min(x.size(), y.size());
    if (n < 10) throw std::invalid_argument("at least 10 pairs are needed");
    std::vector<double> rx = stats::ranks(std::vector<double>(x.begin(), x.begin() + n));
    std::vector<double> ry = stats::ranks(std::vector<double>(y.begin(), y.begin() + n));
    double rho = stats::spearman(x, y);
    // empirical copula on a 10 x 10 grid
    const int G = 10;
    auto grid = [&](const std::vector<double>& u, const std::vector<double>& v) {
        std::vector<double> g(G * G, 0.0);
        for (size_t i = 0; i < u.size(); ++i) {
            int a = std::min(G - 1, int(u[i] * G)), b = std::min(G - 1, int(v[i] * G));
            g[a * G + b] += 1.0 / u.size();
        }
        return g;
    };
    std::vector<double> pu(n), pv(n);
    for (size_t i = 0; i < n; ++i) { pu[i] = rx[i] / (n + 1); pv[i] = ry[i] / (n + 1); }
    auto ge = grid(pu, pv);
    int best = 0;
    double bestD = 1e300;
    for (int type = 0; type <= kMaxCopulaType; ++type) {
        std::vector<CorrelationSpec> spec{{0, 1, std::max(-0.95, std::min(0.95, rho)), type}};
        CopulaResult cr;
        try { cr = generateUniforms(2, 20000, spec, rng); } catch (...) { continue; }
        auto gm = grid(cr.u[0], cr.u[1]);
        double d = 0;
        for (int i = 0; i < G * G; ++i) d += (ge[i] - gm[i]) * (ge[i] - gm[i]);
        if (d < bestD) { bestD = d; best = type; }
    }
    return best;
}

CopulaResult generateUniforms(int nvars, int iterations, const std::vector<CorrelationSpec>& corr,
                              std::mt19937_64& rng) {
    CopulaResult res;
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::normal_distribution<double> nd;
    std::exponential_distribution<double> ex(1.0);
    res.u.assign(nvars, std::vector<double>());

    std::vector<CorrelationSpec> ell, arch;
    std::map<int, int> archUse, anyUse;
    for (const auto& c : corr) {
        if (c.a < 0 || c.b < 0 || c.a >= nvars || c.b >= nvars) continue;
        if (c.a == c.b) { res.warnings.push_back("a variable cannot be correlated with itself; ignored"); continue; }
        if (!(c.rho > -1 && c.rho < 1)) { res.warnings.push_back("correlation outside (-1,1); ignored"); continue; }
        anyUse[c.a]++; anyUse[c.b]++;
        (c.type >= 2 && c.type <= kMaxCopulaType ? arch : ell).push_back(c);
    }
    std::set<int> inArch;
    for (const auto& c : arch) {
        if (anyUse[c.a] > 1 || anyUse[c.b] > 1)
            throw std::invalid_argument(
                "Archimedean and asymmetric copulas (all types except Normal and Student-t) are bivariate: each variable may only appear in one correlation");
        inArch.insert(c.a);
        inArch.insert(c.b);
    }

    // ---- elliptical components ----
    UnionFind uf(nvars);
    for (const auto& c : ell) uf.unite(c.a, c.b);
    std::map<int, std::vector<int>> comps;
    for (const auto& c : ell) { comps[uf.find(c.a)]; }
    for (int v = 0; v < nvars; ++v)
        if (comps.count(uf.find(v))) comps[uf.find(v)].push_back(v);

    std::vector<bool> done(nvars, false);
    for (auto& [root, vars] : comps) {
        size_t k = vars.size();
        std::map<int, size_t> pos;
        for (size_t i = 0; i < k; ++i) pos[vars[i]] = i;
        Matrix m(k, std::vector<double>(k, 0.0));
        for (size_t i = 0; i < k; ++i) m[i][i] = 1;
        bool useT = false, useN = false;
        for (const auto& c : ell) {
            if (!pos.count(c.a)) continue;
            double r = spearmanToPearson(c.rho);
            m[pos[c.a]][pos[c.b]] = m[pos[c.b]][pos[c.a]] = r;
            (c.type == 1 ? useT : useN) = true;
        }
        if (useT && useN)
            res.warnings.push_back("Gaussian and Student-t correlations linked in one group; Student-t used for the group");
        if (nearestCorrelation(m))
            res.warnings.push_back("correlation matrix was not consistent and has been adjusted to the nearest valid one");
        Matrix l;
        cholesky(m, l);
        for (int v : vars) res.u[v].resize(iterations);
        std::vector<double> z(k), x(k);
        for (int it = 0; it < iterations; ++it) {
            for (size_t i = 0; i < k; ++i) z[i] = nd(rng);
            double w = useT ? std::sqrt(4.0 / chi2Sample(4.0, rng)) : 1.0;
            for (size_t i = 0; i < k; ++i) {
                double s = 0;
                for (size_t j = 0; j <= i; ++j) s += l[i][j] * z[j];
                res.u[vars[i]][it] = clampU(useT ? tCdf(s * w, 4.0) : normCdf(s));
            }
        }
        for (int v : vars) done[v] = true;
    }

    // ---- bivariate (Archimedean and asymmetric) pairs ----
    for (const auto& c : arch) {
        bool negative = c.rho < 0;
        double theta = calibrateTheta(c.type, std::fabs(c.rho), res.warnings);
        auto& ua = res.u[c.a];
        auto& ub = res.u[c.b];
        ua.resize(iterations);
        ub.resize(iterations);
        for (int it = 0; it < iterations; ++it) {
            auto [u1, u2] = samplePair(c.type, theta, rng);
            if (negative) u2 = 1 - u2;
            ua[it] = clampU(u1);
            ub[it] = clampU(u2);
        }
        done[c.a] = done[c.b] = true;
    }

    // ---- independent ----
    for (int v = 0; v < nvars; ++v) {
        if (done[v]) continue;
        res.u[v].resize(iterations);
        for (int it = 0; it < iterations; ++it) res.u[v][it] = clampU(uni(rng));
    }
    return res;
}

namespace {
//                            MC FLO code: 0  1  2  3  4  5  6  7  8
constexpr int kFromMcflo[kMaxCopulaType + 1] = {0, 2, 3, 4, 5, 1, 6, 8, 7};
} // namespace

int copulaFromMcflo(int t) { return t >= 0 && t <= kMaxCopulaType ? kFromMcflo[t] : 0; }

int copulaToMcflo(int internal) {
    for (int t = 0; t <= kMaxCopulaType; ++t)
        if (kFromMcflo[t] == internal) return t;
    return 0;
}

const char* copulaMcfloName(int t) {
    static const char* n[kMaxCopulaType + 1] = {"Normal", "Clayton", "rClayton", "Gumbel", "rGumbel",
                                                "Student-t", "asy. Gumbel", "asy. Triangular", "asy. Frechet"};
    return t >= 0 && t <= kMaxCopulaType ? n[t] : "?";
}

bool copulaIsBivariateOnly(int t) { return t != 0 && t != 5; }

} // namespace mc
