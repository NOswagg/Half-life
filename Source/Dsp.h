#pragma once
/* Half Life — DSP building blocks, ported 1:1 from the prototype (half-life.html).
   No JUCE in here: plain C++ so the sound engine can be read and tested on its own. */
#include <vector>
#include <cstdint>
#include <cmath>
#include <memory>
#include <atomic>
#include <algorithm>

namespace hl
{
constexpr double PI = 3.14159265358979323846;
inline double clampd (double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline double lerp (double a, double b, double t) { return a + (b - a) * t; }
inline double linToDb (double x) { return 20.0 * std::log10 (std::max (x, 1e-9)); }
inline double sgn (double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); }

/* mulberry32, bit-exact with the prototype's rngOf(): the same seed gives the same sound */
struct Rng
{
    uint32_t a;
    explicit Rng (uint32_t seed = 1) : a (seed) {}
    double operator()()
    {
        a += 0x6D2B79F5u;
        uint32_t t = a;
        t = (t ^ (t >> 15)) * (t | 1u);
        t = (t + ((t ^ (t >> 7)) * (t | 61u))) ^ t;
        return (double) (t ^ (t >> 14)) / 4294967296.0;
    }
    uint32_t u32() { return (uint32_t) ((*this)() * 4294967296.0); }
};
inline double rr (Rng& r, double a, double b) { return a + (b - a) * r(); }
inline double gauss (Rng& r) { return (r() + r() + r() + r() - 2) * 0.866; }

/* non-deterministic randomness (the prototype's Math.random): new seeds, glide routes, perish */
double rnd();
uint32_t newSeed();

/* ---- band-limited reads ----
   Fractional reads use a 32-tap Kaiser-windowed sinc from a 16384-phase table. Reads faster than real time use
   quarter-octave band-limited copies ("levels"), built on first use and only over the stretch that is read. */
double herm (const float* d, int n, double p);
double sread (const float* d, int n, double p);
std::vector<float> kaiserLP (double fc, double tw);

struct Level { std::vector<float> d; int D = 1; int off = 0; double a = 0, b = 0; };
inline double lvRead (const Level& lv, double p) { return sread (lv.d.data(), (int) lv.d.size(), (p - lv.off) / lv.D); }
inline int levelFor (double r) { return r <= 1 ? 0 : std::min (40, (int) std::floor (4 * std::log2 (r))); }

struct Mip
{
    const float* x = nullptr; int n = 0;
    std::vector<std::unique_ptr<Level>> lv;      // index = level h (0 = the material itself)
    Mip() = default;
    Mip (const float* data, int len) { set (data, len); }
    void set (const float* data, int len);
    const Level& level (int h, double a, double b);
    const Level& level (int h) { return level (h, 0, n); }
    double read (double p, double r);            // variable-rate read with a crossfade between levels
};
Level buildLevel (const float* x, int n, int h, double a, double b);

std::vector<float> resample (const std::vector<float>& x, double ratio);   // ratio = out rate / in rate

/* oversampling for distortion (2×, linear phase) */
std::vector<float> upsample2 (const std::vector<float>& x);
std::vector<float> downsample2 (const std::vector<float>& u);
}
