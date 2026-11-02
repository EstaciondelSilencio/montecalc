// MonteCalc - special functions
#include "specfun.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace mc {

double normCdf(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }

// Acklam's rational approximation refined by one Halley step (|rel err| < 1e-15).
double normInv(double p) {
    if (p <= 0.0) return -std::numeric_limits<double>::infinity();
    if (p >= 1.0) return std::numeric_limits<double>::infinity();
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                               -2.759285104469687e+02, 1.383577518672690e+02,
                               -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                               -1.556989798598866e+02, 6.680131188771972e+01,
                               -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                               -2.400758277161838e+00, -2.549732539343734e+00,
                               4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                               2.445134137142996e+00, 3.754408661907416e+00};
    const double plow = 0.02425, phigh = 1 - plow;
    double x;
    if (p < plow) {
        double q = std::sqrt(-2 * std::log(p));
        x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    } else if (p <= phigh) {
        double q = p - 0.5, r = q * q;
        x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
    } else {
        double q = std::sqrt(-2 * std::log(1 - p));
        x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    double e = normCdf(x) - p;
    double u = e * std::sqrt(2 * kPi) * std::exp(x * x / 2);
    x = x - u / (1 + x * u / 2);
    return x;
}

static double gammaSeries(double a, double x) {
    double sum = 1.0 / a, del = sum, ap = a;
    for (int n = 0; n < 1000; ++n) {
        ap += 1;
        del *= x / ap;
        sum += del;
        if (std::fabs(del) < std::fabs(sum) * 1e-15) break;
    }
    return sum * std::exp(-x + a * std::log(x) - std::lgamma(a));
}

static double gammaContFrac(double a, double x) { // returns Q(a,x)
    const double tiny = 1e-300;
    double b = x + 1 - a, c = 1 / tiny, d = 1 / b, h = d;
    for (int i = 1; i < 1000; ++i) {
        double an = -i * (i - a);
        b += 2;
        d = an * d + b;
        if (std::fabs(d) < tiny) d = tiny;
        c = b + an / c;
        if (std::fabs(c) < tiny) c = tiny;
        d = 1 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1) < 1e-15) break;
    }
    return std::exp(-x + a * std::log(x) - std::lgamma(a)) * h;
}

double gammaP(double a, double x) {
    if (x <= 0) return 0.0;
    if (x < a + 1) return gammaSeries(a, x);
    return 1.0 - gammaContFrac(a, x);
}

static double betaContFrac(double a, double b, double x) {
    const double tiny = 1e-300;
    double qab = a + b, qap = a + 1, qam = a - 1;
    double c = 1, d = 1 - qab * x / qap;
    if (std::fabs(d) < tiny) d = tiny;
    d = 1 / d;
    double h = d;
    for (int m = 1; m < 1000; ++m) {
        int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1 + aa * d;
        if (std::fabs(d) < tiny) d = tiny;
        c = 1 + aa / c;
        if (std::fabs(c) < tiny) c = tiny;
        d = 1 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1 + aa * d;
        if (std::fabs(d) < tiny) d = tiny;
        c = 1 + aa / c;
        if (std::fabs(c) < tiny) c = tiny;
        d = 1 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1) < 1e-15) break;
    }
    return h;
}

double betaI(double a, double b, double x) {
    if (x <= 0) return 0.0;
    if (x >= 1) return 1.0;
    double lbt = std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) +
                 a * std::log(x) + b * std::log(1 - x);
    double bt = std::exp(lbt);
    if (x < (a + 1) / (a + b + 2)) return bt * betaContFrac(a, b, x) / a;
    return 1.0 - bt * betaContFrac(b, a, 1 - x) / b;
}

double tCdf(double x, double df) {
    double ib = betaI(df / 2, 0.5, df / (df + x * x));
    return x >= 0 ? 1 - 0.5 * ib : 0.5 * ib;
}

double chi2Cdf(double x, double df) { return gammaP(df / 2, x / 2); }

double fCdf(double x, double d1, double d2) {
    if (x <= 0) return 0.0;
    return betaI(d1 / 2, d2 / 2, d1 * x / (d1 * x + d2));
}

double invertCdf(const std::function<double(double)>& cdf, double p, double lo, double hi) {
    if (!(p > 0 && p < 1)) throw std::invalid_argument("probability out of range");
    // Expand bracket.
    double step = 1.0;
    if (!std::isfinite(lo)) {
        lo = std::isfinite(hi) ? hi - 1 : -1;
        while (cdf(lo) > p) { lo -= step; step *= 2; if (step > 1e300) break; }
    }
    step = 1.0;
    if (!std::isfinite(hi)) {
        hi = lo + 1;
        while (cdf(hi) < p) { hi += step; step *= 2; if (step > 1e300) break; }
    }
    for (int i = 0; i < 200; ++i) {
        double mid = 0.5 * (lo + hi);
        if (mid == lo || mid == hi) break;
        if (cdf(mid) < p) lo = mid; else hi = mid;
        if (hi - lo <= 1e-14 * std::max(1.0, std::fabs(mid))) break;
    }
    return 0.5 * (lo + hi);
}

} // namespace mc
