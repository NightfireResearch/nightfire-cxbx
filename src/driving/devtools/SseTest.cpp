// The C forms of the x86 instructions (src/driving/platform/SseC.h) against the instructions, on an x86 host.
//
//   sse_test                 every float input for the one-operand forms (RSQRTSS, SQRTSS, the conversions),
//                            random and edge operands for the rest; prints "[sse] ...: N cases, M checks, D differ"
//   sse_test --rsqrt-table   sweeps this CPU's RSQRTSS and prints SseRsqrtTable.h for it (run on the runner: the
//                            table is its Intel's, the CPU lbase_c was made on)

#include "../platform/SseC.h"

#include <xmmintrin.h>
#include <emmintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#include <x86intrin.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

float FromBits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

std::string CpuBrand() {
    unsigned int regs[12] = {};
#ifdef _MSC_VER
    for (int i = 0; i < 3; i++)
        __cpuid(reinterpret_cast<int *>(regs + 4 * i), int(0x80000002u + i));
#else
    for (int i = 0; i < 3; i++)
        __get_cpuid(0x80000002u + i, &regs[4 * i], &regs[4 * i + 1], &regs[4 * i + 2], &regs[4 * i + 3]);
#endif
    char brand[49] = {};
    memcpy(brand, regs, 48);
    std::string s = brand;
    size_t first = s.find_first_not_of(' '), last = s.find_last_not_of(' ');
    return first == std::string::npos ? s : s.substr(first, last - first + 1);
}

uint32_t HardwareRsqrt(uint32_t u) {
    return Bits(_mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(FromBits(u)))));
}

// ---- the table

int PrintRsqrtTable() {
    // The fewest top mantissa bits the estimate depends on, for exponents 126 and 127
    int ignored = 23;
    for (uint32_t exponent = 126; exponent <= 127; exponent++) {
        for (uint32_t m = 0; m < (1u << 23); m++) {
            uint32_t u = exponent << 23 | m;
            while (ignored > 0 && HardwareRsqrt(u) != HardwareRsqrt(u & ~((1u << ignored) - 1)))
                ignored--;
        }
    }
    int indexBits = 23 - ignored;

    // Every other exponent must be the table's result with the exponent stepped: checked for every input
    uint64_t exceptions = 0;
    for (uint32_t exponent = 1; exponent < 255; exponent++) {
        uint32_t odd = exponent & 1;
        int32_t halvings = (int32_t(exponent) - int32_t(126 + odd)) / 2;
        for (uint32_t m = 0; m < (1u << 23); m++) {
            uint32_t base = HardwareRsqrt((126 + odd) << 23 | (m >> ignored << ignored));
            if (HardwareRsqrt(exponent << 23 | m) != base - uint32_t(halvings * (1 << 23)))
                exceptions++;
        }
    }
    if (exceptions != 0) {
        fprintf(stderr, "[sse] RSQRTSS on %s: %llu results do not follow the exponent; no table\n", CpuBrand().c_str(),
                (unsigned long long)exceptions);
        return 1;
    }

    printf("#ifndef DRIVING_PLATFORM_SSERSQRTTABLE_H_\n#define DRIVING_PLATFORM_SSERSQRTTABLE_H_\n\n");
    printf("// RSQRTSS's results on %s,\n", CpuBrand().c_str());
    printf("// for the inputs with exponent 126 (row 0) and 127 (row 1), by the mantissa's top %d bits, the only\n",
           indexBits);
    printf("// ones it reads. Printed by sse_test --rsqrt-table (src/driving/devtools/SseTest.cpp); do not edit.\n\n");
    printf("#include <stdint.h>\n\nnamespace sse {\n\n");
    printf("constexpr int kRsqrtIndexBits = %d;\n\n", indexBits);
    printf("inline constexpr uint32_t kRsqrtEstimate[2][1 << kRsqrtIndexBits] = {\n");
    for (uint32_t odd = 0; odd < 2; odd++) {
        printf("    {\n");
        for (uint32_t i = 0; i < (1u << indexBits); i++) {
            uint32_t u = (126 + odd) << 23 | i << ignored;
            printf("%s0x%08x,%s", i % 8 == 0 ? "        " : " ", HardwareRsqrt(u), i % 8 == 7 ? "\n" : "");
        }
        printf("    },\n");
    }
    printf("};\n\n} // namespace sse\n\n#endif // DRIVING_PLATFORM_SSERSQRTTABLE_H_\n");
    return 0;
}

// ---- the checks

struct Tally {
    uint64_t checks = 0, differ = 0;

    void Check(const char *what, uint32_t input0, uint32_t input1, uint32_t hardware, uint32_t c) {
        checks++;
        if (hardware == c)
            return;
        differ++;
        static std::atomic<int> printed{0};
        if (printed++ < 20)
            printf("[sse] %s(%08x, %08x): instruction %08x, C %08x\n", what, input0, input1, hardware, c);
    }
};

Tally tally;

sse::m128 FromSse(__m128 v) {
    sse::m128 r;
    memcpy(&r, &v, 16);
    return r;
}

void CheckLanes(const char *what, __m128 hardware, sse::m128 c, uint32_t input0, uint32_t input1) {
    uint32_t h[4], s[4];
    memcpy(h, &hardware, 16);
    memcpy(s, &c, 16);
    for (int i = 0; i < 4; i++)
        tally.Check(what, input0, input1, h[i], s[i]);
}

// An addition or a multiplication of two NaNs gives the first operand's, but the compiler may swap the operands
// of the instruction (MSVC does), so the second operand's is the instruction's result too; such lanes are
// counted apart.
uint64_t commutedLanes = 0;

void CheckCommutative(const char *what, __m128 hardware, sse::m128 c, sse::m128 x, sse::m128 y, int lanes,
                      float (*operation)(float, float), uint32_t input0, uint32_t input1) {
    uint32_t h[4], s[4];
    memcpy(h, &hardware, 16);
    memcpy(s, &c, 16);
    for (int i = 0; i < 4; i++) {
        if (i < lanes && h[i] != s[i] && sse::IsNaN(x.lane[i]) && sse::IsNaN(y.lane[i]) &&
            h[i] == Bits(operation(y.lane[i], x.lane[i]))) {
            commutedLanes++;
            continue;
        }
        tally.Check(what, input0, input1, h[i], s[i]);
    }
}

// Every 32-bit pattern through the one-operand forms, on four threads
void CheckEveryFloat() {
    const int kThreads = 4;
    std::vector<std::thread> threads;
    Tally tallies[kThreads];
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t, &tallies] {
            Tally &tally = tallies[t];
            uint64_t first = (uint64_t(1) << 32) * t / kThreads, last = (uint64_t(1) << 32) * (t + 1) / kThreads;
            for (uint64_t i = first; i < last; i++) {
                uint32_t u = uint32_t(i);
                float f = FromBits(u);
                __m128 x = _mm_set_ss(f);
                sse::m128 y = sse::set_ss(f);
                tally.Check("rsqrt_ss", u, 0, Bits(_mm_cvtss_f32(_mm_rsqrt_ss(x))), Bits(sse::cvtss_f32(sse::rsqrt_ss(y))));
                tally.Check("sqrt_ss", u, 0, Bits(_mm_cvtss_f32(_mm_sqrt_ss(x))), Bits(sse::cvtss_f32(sse::sqrt_ss(y))));
                tally.Check("cvtt_ss2si", u, 0, uint32_t(_mm_cvtt_ss2si(x)), uint32_t(sse::cvtt_ss2si(y)));
                tally.Check("cvtss_si32", u, 0, uint32_t(_mm_cvtss_si32(x)), uint32_t(sse::cvtss_si32(y)));
                int32_t n = int32_t(u);
                tally.Check("cvtsi32_ss", u, 0, Bits(_mm_cvtss_f32(_mm_cvtsi32_ss(_mm_setzero_ps(), n))),
                            Bits(sse::cvtss_f32(sse::cvtsi32_ss(sse::setzero_ps(), n))));
            }
        });
    }
    for (int t = 0; t < kThreads; t++) {
        threads[t].join();
        tally.checks += tallies[t].checks;
        tally.differ += tallies[t].differ;
    }
}

// Operands: the edges (zeros, denormals, the normal limits, infinities, quiet and signalling NaNs of both signs,
// values near the integer limits and half-way cases) and random bit patterns
std::vector<uint32_t> Operands(std::mt19937 &random) {
    std::vector<uint32_t> v = {0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff, 0x807fffff, 0x00800000,
                               0x80800000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000,
                               0x7f800001, 0xff800001, 0x7fbfffff, 0x7fc12345, 0xffa00000, 0x3f800000, 0xbf800000,
                               0x3f000000, 0xbf000000, 0x3fc00000, 0x40200000, 0x4effffff, 0x4f000000, 0xcf000000,
                               0xcf000001, 0x34000000, 0x0c000000, 0x73000000, 0x3f800001, 0x3f7fffff};
    for (int i = 0; i < 4000; i++)
        v.push_back(uint32_t(random()));
    for (int i = 0; i < 2000; i++)   // ordinary magnitudes, where sums and products round
        v.push_back(Bits(std::uniform_real_distribution<float>(-1000.0f, 1000.0f)(random)));
    return v;
}

void CheckPairs(const std::vector<uint32_t> &operands) {
    for (uint32_t a : operands) {
        for (uint32_t b : operands) {
            __m128 x = _mm_setr_ps(FromBits(a), FromBits(b), FromBits(a ^ 0x80000000), FromBits(b));
            __m128 y = _mm_setr_ps(FromBits(b), FromBits(a), FromBits(b), FromBits(a ^ 0x00000001));
            sse::m128 sx = FromSse(x), sy = FromSse(y);
            CheckCommutative("add_ps", _mm_add_ps(x, y), sse::add_ps(sx, sy), sx, sy, 4, sse::Add, a, b);
            CheckLanes("sub_ps", _mm_sub_ps(x, y), sse::sub_ps(sx, sy), a, b);
            CheckCommutative("mul_ps", _mm_mul_ps(x, y), sse::mul_ps(sx, sy), sx, sy, 4, sse::Mul, a, b);
            CheckCommutative("add_ss", _mm_add_ss(x, y), sse::add_ss(sx, sy), sx, sy, 1, sse::Add, a, b);
            CheckLanes("sub_ss", _mm_sub_ss(x, y), sse::sub_ss(sx, sy), a, b);
            CheckCommutative("mul_ss", _mm_mul_ss(x, y), sse::mul_ss(sx, sy), sx, sy, 1, sse::Mul, a, b);
            CheckLanes("xor_ps", _mm_xor_ps(x, y), sse::xor_ps(sx, sy), a, b);
        }
    }
}

template <int Selector>
void CheckShuffle(__m128 x, __m128 y) {
    CheckLanes("shuffle_ps", _mm_shuffle_ps(x, y, Selector), sse::shuffle_ps(FromSse(x), FromSse(y), Selector),
               Selector, 0);
}

template <int... Selectors>
void CheckShuffles(__m128 x, __m128 y, std::integer_sequence<int, Selectors...>) {
    (CheckShuffle<Selectors>(x, y), ...);
}

void CheckMoves(std::mt19937 &random) {
    float in[8], hardware[8], c[8];
    for (float &f : in)
        f = FromBits(uint32_t(random()) | 0x7f800001);   // NaNs, signalling ones included, must move unchanged
    __m128 x = _mm_loadu_ps(in), y = _mm_loadu_ps(in + 4);
    CheckShuffles(x, y, std::make_integer_sequence<int, 256>());

    CheckLanes("loadu_ps", _mm_loadu_ps(in + 1), sse::loadu_ps(in + 1), 0, 0);
    CheckLanes("load_ps", _mm_load_ps(in + 4), sse::load_ps(in + 4), 0, 0);
    CheckLanes("load_ss", _mm_load_ss(in + 3), sse::load_ss(in + 3), 0, 0);
    CheckLanes("set_ss", _mm_set_ss(in[2]), sse::set_ss(in[2]), 0, 0);
    CheckLanes("set1_ps", _mm_set1_ps(in[5]), sse::set1_ps(in[5]), 0, 0);
    CheckLanes("set_ps", _mm_set_ps(in[0], in[1], in[2], in[3]), sse::set_ps(in[0], in[1], in[2], in[3]), 0, 0);
    CheckLanes("setr_ps", _mm_setr_ps(in[0], in[1], in[2], in[3]), sse::setr_ps(in[0], in[1], in[2], in[3]), 0, 0);
    CheckLanes("setzero_ps", _mm_setzero_ps(), sse::setzero_ps(), 0, 0);
    tally.Check("cvtss_f32", 0, 0, Bits(_mm_cvtss_f32(y)), Bits(sse::cvtss_f32(FromSse(y))));

    memcpy(hardware, in, 32);
    memcpy(c, in, 32);
    _mm_storeu_ps(hardware + 3, x);
    sse::storeu_ps(c + 3, FromSse(x));
    _mm_store_ss(hardware + 1, y);
    sse::store_ss(c + 1, FromSse(y));
    for (int i = 0; i < 8; i++)
        tally.Check("store", i, 0, Bits(hardware[i]), Bits(c[i]));

    alignas(16) int32_t ints[4] = {int32_t(random()), INT32_MIN, 16777217, -16777219};
    for (int i = 0; i < 1000; i++) {
        __m128i hi = _mm_load_si128(reinterpret_cast<const __m128i *>(ints));
        sse::m128i ci = sse::load_si128(reinterpret_cast<const sse::m128i *>(ints));
        CheckLanes("cvtepi32_ps", _mm_cvtepi32_ps(hi), sse::cvtepi32_ps(ci), uint32_t(ints[0]), 0);
        for (int32_t &n : ints)
            n = int32_t(random()) >> (random() % 32);
    }
}

void CheckStringMoves(std::mt19937 &random) {
    // Overlapping copies both ways, as REP MOVSD and REP MOVSB move them
    for (int t = 0; t < 2000; t++) {
        unsigned char hardware[96], c[96];
        for (unsigned char &b : hardware)
            b = (unsigned char)random();
        memcpy(c, hardware, sizeof c);
        size_t from = random() % 32, to = random() % 32, count = random() % 16;
        if (t & 1) {
#ifdef _MSC_VER
            __movsd(reinterpret_cast<unsigned long *>(hardware + to), reinterpret_cast<unsigned long *>(hardware + from), count);
#else
            void *d = hardware + to;
            const void *s = hardware + from;
            size_t n = count;
            __asm__ __volatile__("rep movsl" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
#endif
            sse::movsd(c + to, c + from, count);
        } else {
#ifdef _MSC_VER
            __movsb(hardware + to, hardware + from, count);
#else
            void *d = hardware + to;
            const void *s = hardware + from;
            size_t n = count;
            __asm__ __volatile__("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
#endif
            sse::movsb(c + to, c + from, count);
        }
        for (int i = 0; i < 96; i++)
            tally.Check(t & 1 ? "movsd" : "movsb", uint32_t(from), uint32_t(to), hardware[i], c[i]);
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--rsqrt-table") == 0)
        return PrintRsqrtTable();

    std::mt19937 random(20261016);
    CheckEveryFloat();
    CheckPairs(Operands(random));
    CheckMoves(random);
    CheckStringMoves(random);
    uint64_t a = sse::rdtsc(), b = sse::rdtsc();
    tally.Check("rdtsc goes forwards", 0, 0, 1, b >= a);

    printf("[sse] C forms against the instructions on %s: %llu checks, %llu differ (%llu lanes of two NaNs "
           "commuted)\n", CpuBrand().c_str(), (unsigned long long)tally.checks, (unsigned long long)tally.differ,
           (unsigned long long)commutedLanes);
    return tally.differ != 0;
}
