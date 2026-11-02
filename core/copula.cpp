// MonteCalc - copulas
#include "copula.h"
#include "specfun.h"

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
        (c.type >= 2 && c.type <= 5 ? arch : ell).push_back(c);
    }
    std::set<int> inArch;
    for (const auto& c : arch) {
        if (anyUse[c.a] > 1 || anyUse[c.b] > 1)
            throw std::invalid_argument(
                "Archimedean copulas (types 2-5) are bivariate: each variable may only appear in one correlation");
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

    // ---- Archimedean pairs ----
    for (const auto& c : arch) {
        double tau = spearmanToKendall(c.rho);
        bool negative = tau < 0;
        tau = std::min(std::fabs(tau), 0.98);
        bool clayton = c.type == 2 || c.type == 3;
        bool rotate = c.type == 3 || c.type == 5;
        auto& ua = res.u[c.a];
        auto& ub = res.u[c.b];
        ua.resize(iterations);
        ub.resize(iterations);
        for (int it = 0; it < iterations; ++it) {
            double u1, u2;
            if (tau < 1e-9) {
                u1 = uni(rng); u2 = uni(rng);
            } else if (clayton) {
                double th = 2 * tau / (1 - tau);
                double v = std::gamma_distribution<double>(1 / th, 1.0)(rng);
                u1 = std::pow(1 + ex(rng) / v, -1 / th);
                u2 = std::pow(1 + ex(rng) / v, -1 / th);
            } else {
                double th = 1 / (1 - tau);
                double v = positiveStable(1 / th, rng);
                u1 = std::exp(-std::pow(ex(rng) / v, 1 / th));
                u2 = std::exp(-std::pow(ex(rng) / v, 1 / th));
            }
            if (rotate) { u1 = 1 - u1; u2 = 1 - u2; }
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

} // namespace mc
