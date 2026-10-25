// DetMath across hosts (docs/deterministic-maths.md): writes each function's results to <dir>/<name>.bin with an FNV
// hash, and against the host's libm prints how many results differ and the largest difference in ulps. Compare two
// hosts' folders with tools/detmath/compare.py; every file should be identical.
//
//   clang++ -O2 -std=c++20 -ffp-contract=off -DNIGHTFIRE_X87=0 -DNIGHTFIRE_DET_MATH=1 -Isrc/common/math \
//       tools/detmath/detprobe.cpp src/common/math/DetMath.cpp -o detprobe && ./detprobe out_<host>
//   cl /O2 /EHsc /std:c++20 /DNIGHTFIRE_X87=0 /DNIGHTFIRE_DET_MATH=1 /Isrc\common\math tools\detmath\detprobe.cpp
//       src\common\math\DetMath.cpp
#include "DetMath.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0777)
#endif

static uint64_t s = 0x243F6A8885A308D3ull;
static uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
static float frange(float lo, float hi) { return (float)(lo + (hi - lo) * unit()); }
static const int N = 1 << 21;

static int64_t ord(double d) { int64_t i; memcpy(&i, &d, 8); return i < 0 ? INT64_MIN - i : i; }
static uint64_t ulps(double a, double b) {
    if (a != a && b != b) return 0;
    int64_t x = ord(a), y = ord(b); return x > y ? (uint64_t)(x - y) : (uint64_t)(y - x);
}

static void report(const char *dir, const char *name, const std::vector<double> &det, const std::vector<double> &host) {
    uint32_t h = 2166136261u;
    for (double d : det) { const uint8_t *b = (const uint8_t *)&d; for (int i = 0; i < 8; i++) { h ^= b[i]; h *= 16777619u; } }
    size_t differ = 0, floatDiffer = 0; uint64_t worst = 0;
    for (size_t i = 0; i < det.size(); i++) {
        uint64_t u = ulps(det[i], host[i]);
        if (u) differ++;
        if (u > worst) worst = u;
        float a = (float)det[i], b = (float)host[i];
        if (memcmp(&a, &b, 4) != 0 && !(a != a && b != b)) floatDiffer++;
    }
    printf("%-12s %08x  vs host: %7zu differ, max %llu ulp, %zu differ as floats\n", name, h, differ,
           (unsigned long long)worst, floatDiffer);
    std::string p = std::string(dir) + "/" + name + ".bin";
    FILE *f = fopen(p.c_str(), "wb"); fwrite(det.data(), 8, det.size(), f); fclose(f);
}

template <class D, class H> static void one(const char *dir, const char *name, const std::vector<double> &a, D d, H hst) {
    std::vector<double> rd, rh;
    for (double v : a) { rd.push_back(d(v)); rh.push_back(hst(v)); }
    report(dir, name, rd, rh);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "det";
    MKDIR(dir);
    std::vector<double> ang, wide, big, huge, nearPi, unitv, expv, ys, xs, specials;
    for (int i = 0; i < N; i++) {
        ang.push_back(frange(-6.2831853f, 6.2831853f));
        wide.push_back(frange(-200000.f, 200000.f));
        double m; do { uint64_t b = next(); memcpy(&m, &b, 8); } while (!(fabs(m) < 1e6) || fabs(m) < 1e-300);
        big.push_back(m);
        do { uint64_t b = next(); memcpy(&m, &b, 8); } while (!(fabs(m) < 1e300) || fabs(m) < 2e6);
        huge.push_back(m);
        int k = (int)(next() % 400000) - 200000; nearPi.push_back(k * 1.5707963267948966 + (unit() - 0.5) * 1e-9);
        unitv.push_back(i & 1 ? frange(-1.f, 1.f) : (unit() * 2 - 1));
        expv.push_back((unit() - 0.5) * 1500);
        { volatile float t = frange(-1000.f, 1000.f) * (i & 1 ? 1e-3f : 1.f); ys.push_back(t); }
        xs.push_back(frange(-1000.f, 1000.f));
    }
    const double inf = INFINITY;
    double sp[] = {0.0, -0.0, 1.0, -1.0, inf, -inf, NAN, 1e-310, -1e-310, 0.5, -0.5, 0.975, 709.78, -745.13, 1e300};
    for (double v : sp) specials.push_back(v);

    one(dir, "sin_ang", ang, DetMath::Sin, [](double x) { return sin(x); });
    one(dir, "sin_wide", wide, DetMath::Sin, [](double x) { return sin(x); });
    one(dir, "sin_big", big, DetMath::Sin, [](double x) { return sin(x); });
    one(dir, "sin_huge", huge, DetMath::Sin, [](double x) { return sin(x); });
    one(dir, "sin_nearpi", nearPi, DetMath::Sin, [](double x) { return sin(x); });
    one(dir, "cos_ang", ang, DetMath::Cos, [](double x) { return cos(x); });
    one(dir, "cos_wide", wide, DetMath::Cos, [](double x) { return cos(x); });
    one(dir, "cos_big", big, DetMath::Cos, [](double x) { return cos(x); });
    one(dir, "cos_huge", huge, DetMath::Cos, [](double x) { return cos(x); });
    one(dir, "cos_nearpi", nearPi, DetMath::Cos, [](double x) { return cos(x); });
    one(dir, "tan_ang", ang, DetMath::Tan, [](double x) { return tan(x); });
    one(dir, "tan_big", big, DetMath::Tan, [](double x) { return tan(x); });
    one(dir, "tan_huge", huge, DetMath::Tan, [](double x) { return tan(x); });
    one(dir, "atan_wide", wide, DetMath::Atan, [](double x) { return atan(x); });
    one(dir, "atan_big", big, DetMath::Atan, [](double x) { return atan(x); });
    one(dir, "asin_unit", unitv, DetMath::Asin, [](double x) { return asin(x); });
    one(dir, "acos_unit", unitv, DetMath::Acos, [](double x) { return acos(x); });
    one(dir, "exp_wide", expv, DetMath::Exp, [](double x) { return exp(x); });
    one(dir, "exp_unit", unitv, DetMath::Exp, [](double x) { return exp(x); });
    one(dir, "specials_sin", specials, DetMath::Sin, [](double x) { return sin(x); });
    one(dir, "specials_exp", specials, DetMath::Exp, [](double x) { return exp(x); });
    one(dir, "specials_acos", specials, DetMath::Acos, [](double x) { return acos(x); });
    std::vector<double> logv;
    for (int i = 0; i < N; i++) { double m; uint64_t b = next() & 0x7fffffffffffffffull; memcpy(&m, &b, 8); logv.push_back(i % 3 == 0 ? m : (i % 3 == 1 ? unit() * 1e5 : 1.0 + (unit() - 0.5) * 1e-3)); }
    one(dir, "log_wide", logv, DetMath::Log, [](double x) { return log(x); });
    one(dir, "specials_log", specials, DetMath::Log, [](double x) { return log(x); });
    {
        std::vector<double> rd, rh;
        for (int i = 0; i < N; i++) {
            double x, y;
            switch (i % 5) {
            case 0: x = unit() * 100; y = (unit() - 0.5) * 40; break;
            case 1: x = unit() * 2; y = (unit() - 0.5) * 2000; break;
            case 2: x = -(unit() * 10); y = (double)((int)(next() % 61) - 30); break;
            case 3: x = 1.0 + (unit() - 0.5) * 1e-7; y = (unit() - 0.5) * 1e10; break;
            default: x = frange(0.f, 1000.f); y = frange(-3.f, 3.f); break;
            }
            rd.push_back(DetMath::Pow(x, y)); rh.push_back(pow(x, y));
        }
        for (double a : sp) for (double b : sp) { rd.push_back(DetMath::Pow(a, b)); rh.push_back(pow(a, b)); }
        report(dir, "pow", rd, rh);
    }
    {
        std::vector<double> rd, rh;
        for (int i = 0; i < N; i++) { rd.push_back(DetMath::Atan2(ys[i], xs[i])); rh.push_back(atan2(ys[i], xs[i])); }
        for (double a : sp) for (double b : sp) { rd.push_back(DetMath::Atan2(a, b)); rh.push_back(atan2(a, b)); }
        report(dir, "atan2", rd, rh);
    }
}
