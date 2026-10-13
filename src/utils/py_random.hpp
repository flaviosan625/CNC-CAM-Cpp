// random.Random(seed) do CPython: o mesmo Mersenne Twister, a mesma semeadura
// (init_by_array) e o mesmo random()/uniform(). E o que faz a Textura sair com os
// mesmos tracos que no app em Python para a mesma semente.
#pragma once

#include <cstdint>

namespace cnc {

class PyRandom {
public:
    explicit PyRandom(long long seed);

    // random.random(): float em [0, 1) com 53 bits
    double random();
    // random.uniform(a, b) = a + (b - a) * random()
    double uniform(double a, double b) { return a + (b - a) * random(); }

private:
    static constexpr int kN = 624;
    std::uint32_t mt_[kN];
    int index_ = kN;

    void init_genrand(std::uint32_t s);
    void init_by_array(const std::uint32_t* key, int key_length);
    std::uint32_t genrand_uint32();
};

}  // namespace cnc
