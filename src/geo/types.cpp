#include "geo/types.hpp"

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>

namespace cnc {

namespace {

// Porte de vector_norm() de Modules/mathmodule.c (CPython 3.12+): soma de quadrados
// com transformacoes sem erro (fma) e uma correcao final. E o que da ao math.hypot e
// ao math.dist o resultado arredondado corretamente na quase totalidade dos casos.
struct DoubleLength {
    double hi;
    double lo;
};

inline DoubleLength dl_fast_sum(double a, double b) noexcept {
    const double x = a + b;
    const double y = (a - x) + b;
    return {x, y};
}

inline DoubleLength dl_mul(double x, double y) noexcept {
    const double z = x * y;
    const double zz = std::fma(x, y, -z);
    return {z, zz};
}

double vector_norm(int n, double* vec, double max, bool found_nan) noexcept {
    if (std::isinf(max)) return max;
    if (found_nan) return std::nan("");
    if (max == 0.0 || n <= 1) return max;
    int max_e = 0;
    std::frexp(max, &max_e);
    if (max_e < -1023) {
        // subnormais: ldexp(1.0, -max_e) estouraria
        for (int i = 0; i < n; ++i) vec[i] /= DBL_MIN;
        return DBL_MIN * vector_norm(n, vec, max / DBL_MIN, found_nan);
    }
    const double scale = std::ldexp(1.0, -max_e);
    double csum = 1.0, frac1 = 0.0, frac2 = 0.0;
    for (int i = 0; i < n; ++i) {
        double x = vec[i];
        x *= scale;                          // escala sem perda
        DoubleLength pr = dl_mul(x, x);      // quadrado sem perda
        DoubleLength sm = dl_fast_sum(csum, pr.hi);
        csum = sm.hi;
        frac1 += pr.lo;
        frac2 += sm.lo;
    }
    double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
    DoubleLength pr = dl_mul(-h, h);
    DoubleLength sm = dl_fast_sum(csum, pr.hi);
    csum = sm.hi;
    frac1 += pr.lo;
    frac2 += sm.lo;
    const double x = csum - 1.0 + (frac1 + frac2);
    h += x / (2.0 * h);
    return h / scale;
}

}  // namespace

double hypot(double a, double b) noexcept {
    double v[2] = {std::fabs(a), std::fabs(b)};
    const bool nan = std::isnan(v[0]) || std::isnan(v[1]);
    double max = 0.0;
    for (double d : v) {
        if (d > max) max = d;
    }
    return vector_norm(2, v, max, nan);
}

double hypot3(double a, double b, double c) noexcept {
    double v[3] = {std::fabs(a), std::fabs(b), std::fabs(c)};
    const bool nan = std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2]);
    double max = 0.0;
    for (double d : v) {
        if (d > max) max = d;
    }
    return vector_norm(3, v, max, nan);
}

double py_round(double x, int ndigits) {
    if (!std::isfinite(x)) return x;
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.*f", ndigits < 0 ? 0 : ndigits, x);
    double r = std::strtod(buf, nullptr);
    if (ndigits < 0) {
        const double p = std::pow(10.0, -ndigits);
        r = std::nearbyint(x / p) * p;
    }
    return r;
}

std::string new_id() {
    static std::mutex mu;
    static std::mt19937_64 rng{std::random_device{}() ^ (std::uint64_t{std::random_device{}()} << 32)};
    std::uint64_t v;
    {
        std::lock_guard<std::mutex> lock(mu);
        v = rng();
    }
    static const char* hex = "0123456789abcdef";
    std::string s(12, '0');
    for (int i = 0; i < 12; ++i) {
        s[static_cast<size_t>(i)] = hex[(v >> (i * 4)) & 0xF];
    }
    return s;
}

std::string new_uuid_hex() {
    static std::mutex mu;
    static std::mt19937_64 rng{std::random_device{}() ^ (std::uint64_t{std::random_device{}()} << 32)};
    std::uint64_t hi, lo;
    {
        std::lock_guard<std::mutex> lock(mu);
        hi = rng();
        lo = rng();
    }
    // uuid4: versao 4 e variante RFC 4122, como o uuid.uuid4 do Python.
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    static const char* hex = "0123456789abcdef";
    std::string s(32, '0');
    for (int i = 0; i < 16; ++i) {
        s[static_cast<size_t>(i)] = hex[(hi >> (60 - i * 4)) & 0xF];
        s[static_cast<size_t>(16 + i)] = hex[(lo >> (60 - i * 4)) & 0xF];
    }
    return s;
}

std::string fmt_g(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

}  // namespace cnc
