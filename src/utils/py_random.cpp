#include "utils/py_random.hpp"

#include <cstdlib>
#include <vector>

namespace cnc {

namespace {
constexpr int kM = 397;
constexpr std::uint32_t kMatrixA = 0x9908b0dfU;
constexpr std::uint32_t kUpper = 0x80000000U;
constexpr std::uint32_t kLower = 0x7fffffffU;
}  // namespace

void PyRandom::init_genrand(std::uint32_t s) {
    mt_[0] = s;
    for (int i = 1; i < kN; ++i) {
        mt_[i] = 1812433253U * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + static_cast<std::uint32_t>(i);
    }
    index_ = kN;
}

void PyRandom::init_by_array(const std::uint32_t* key, int key_length) {
    init_genrand(19650218U);
    int i = 1, j = 0;
    int k = kN > key_length ? kN : key_length;
    for (; k; --k) {
        mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1664525U)) + key[j] + static_cast<std::uint32_t>(j);
        ++i;
        ++j;
        if (i >= kN) {
            mt_[0] = mt_[kN - 1];
            i = 1;
        }
        if (j >= key_length) j = 0;
    }
    for (k = kN - 1; k; --k) {
        mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1566083941U)) - static_cast<std::uint32_t>(i);
        ++i;
        if (i >= kN) {
            mt_[0] = mt_[kN - 1];
            i = 1;
        }
    }
    mt_[0] = 0x80000000U;  // MSB 1: garante estado inicial nao nulo
}

PyRandom::PyRandom(long long seed) {
    // Semente inteira: o valor absoluto vira palavras de 32 bits, da menos para a mais
    // significativa (random_seed no Modules/_randommodule.c).
    unsigned long long n = seed < 0 ? 0ULL - static_cast<unsigned long long>(seed) : static_cast<unsigned long long>(seed);
    std::vector<std::uint32_t> key;
    do {
        key.push_back(static_cast<std::uint32_t>(n & 0xffffffffULL));
        n >>= 32;
    } while (n);
    init_by_array(key.data(), static_cast<int>(key.size()));
}

std::uint32_t PyRandom::genrand_uint32() {
    static const std::uint32_t mag01[2] = {0x0U, kMatrixA};
    std::uint32_t y;
    if (index_ >= kN) {
        int kk;
        for (kk = 0; kk < kN - kM; ++kk) {
            y = (mt_[kk] & kUpper) | (mt_[kk + 1] & kLower);
            mt_[kk] = mt_[kk + kM] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        for (; kk < kN - 1; ++kk) {
            y = (mt_[kk] & kUpper) | (mt_[kk + 1] & kLower);
            mt_[kk] = mt_[kk + (kM - kN)] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        y = (mt_[kN - 1] & kUpper) | (mt_[0] & kLower);
        mt_[kN - 1] = mt_[kM - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
        index_ = 0;
    }
    y = mt_[index_++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= (y >> 18);
    return y;
}

double PyRandom::random() {
    const std::uint32_t a = genrand_uint32() >> 5, b = genrand_uint32() >> 6;
    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}

}  // namespace cnc
