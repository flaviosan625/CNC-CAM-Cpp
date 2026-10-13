// Tipos geometricos basicos e aritmetica que reproduz o Python bit a bit.
//
// O app em Python e a referencia: onde um numero sai diferente na ultima casa,
// o G-code muda. Por isso aqui estao as pequenas funcoes de math/round do
// CPython com a mesma conta, e nao as "parecidas" da biblioteca padrao do C++.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace cnc {

struct Pt {
    double x = 0.0;
    double y = 0.0;
};

inline bool operator==(const Pt& a, const Pt& b) noexcept { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const Pt& a, const Pt& b) noexcept { return !(a == b); }

using Points = std::vector<Pt>;
using Rgb = std::array<double, 3>;

// (x0, y0, x1, y1) como as caixas do Python.
struct BBox {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
};

inline constexpr double kPi = 3.14159265358979323846;
// math.radians(x) = x * (pi / 180): a divisao vem primeiro, nao a multiplicacao.
inline constexpr double kDegToRad = kPi / 180.0;
inline constexpr double kRadToDeg = 180.0 / kPi;

inline double radians(double deg) noexcept { return deg * kDegToRad; }
inline double degrees(double rad) noexcept { return rad * kRadToDeg; }

// math.hypot / math.dist do CPython (3.10+): norma com precisao extra, quase sempre
// arredondada corretamente. std::hypot pode diferir de 1 ulp, e um empate de distancia
// (pecas simetricas) muda qual vertice o min() escolhe.
double hypot(double a, double b) noexcept;
double hypot3(double a, double b, double c) noexcept;

// math.dist em 2D e 3D
inline double dist(const Pt& a, const Pt& b) noexcept { return hypot(b.x - a.x, b.y - a.y); }
inline double dist(double ax, double ay, double bx, double by) noexcept {
    return hypot(bx - ax, by - ay);
}
inline double dist3(double ax, double ay, double az, double bx, double by, double bz) noexcept {
    return hypot3(bx - ax, by - ay, bz - az);
}

// sum() de floats do Python 3.12+: soma compensada de Neumaier (nao a soma ingenua).
class PySum {
public:
    void add(double x) noexcept {
        const double t = total_ + x;
        if (std::fabs(total_) >= std::fabs(x)) {
            comp_ += (total_ - t) + x;
        } else {
            comp_ += (x - t) + total_;
        }
        total_ = t;
    }
    double result() const noexcept {
        return (comp_ != 0.0 && std::isfinite(comp_)) ? total_ + comp_ : total_;
    }

private:
    double total_ = 0.0;
    double comp_ = 0.0;
};

template <class Range, class Fn>
double py_sum(const Range& range, Fn&& fn) {
    PySum s;
    for (const auto& item : range) s.add(fn(item));
    return s.result();
}

// math.isclose(a, b) com rel_tol=1e-9 e abs_tol=0, como no Python.
inline bool isclose(double a, double b, double rel_tol = 1e-9, double abs_tol = 0.0) noexcept {
    if (a == b) return true;
    if (std::isinf(a) || std::isinf(b)) return false;
    const double diff = std::fabs(b - a);
    return diff <= std::fabs(rel_tol * b) || diff <= std::fabs(rel_tol * a) || diff <= abs_tol;
}

inline double clamp(double v, double lo, double hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

// round(x) do Python 3: arredonda metade para o PAR (banker's), nao para longe de zero.
// std::nearbyint faz o mesmo no modo de arredondamento padrao.
inline double py_round(double x) noexcept { return std::nearbyint(x); }
inline std::int64_t py_round_i64(double x) noexcept {
    return static_cast<std::int64_t>(std::nearbyint(x));
}
// round(x, ndigits): correto sobre o valor binario exato, igual ao Python.
double py_round(double x, int ndigits);

// Divisao inteira e resto do Python (floor), diferentes do C++ para negativos.
inline long long floordiv(long long a, long long b) noexcept {
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}
// a // b e a % b para floats, com o algoritmo do CPython (float_divmod).
inline double fmod_py(double a, double b) noexcept {
    double mod = std::fmod(a, b);
    if (mod != 0.0) {
        if ((b < 0.0) != (mod < 0.0)) mod += b;
    } else {
        mod = std::copysign(0.0, b);
    }
    return mod;
}
inline double floordiv_f(double a, double b) noexcept {
    double mod = std::fmod(a, b);
    double div = (a - mod) / b;
    if (mod != 0.0) {
        if ((b < 0.0) != (mod < 0.0)) div -= 1.0;
    }
    if (div != 0.0) {
        double fl = std::floor(div);
        if (div - fl > 0.5) fl += 1.0;
        return fl;
    }
    return std::copysign(0.0, a / b);
}

// uuid.uuid4().hex[:12]: 12 caracteres hexadecimais aleatorios.
std::string new_id();
// uuid.uuid4().hex: 32 caracteres hexadecimais (grupos de entidades).
std::string new_uuid_hex();

// Formata como f"{v:g}" do Python (sem expoente para valores normais de CNC).
std::string fmt_g(double v);

}  // namespace cnc
