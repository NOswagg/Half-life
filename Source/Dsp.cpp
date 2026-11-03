#include "Dsp.h"
#include <random>
#include <mutex>
#include <chrono>

namespace hl
{
static std::mt19937_64& gen()
{
    static thread_local std::mt19937_64 g { (uint64_t) std::random_device{}() ^ (uint64_t) std::chrono::steady_clock::now().time_since_epoch().count() };
    return g;
}
double rnd() { return std::uniform_real_distribution<double> (0.0, 1.0) (gen()); }
uint32_t newSeed() { return (uint32_t) (rnd() * 4294967296.0); }

double herm (const float* d, int n, double p)
{
    const int i = (int) std::floor (p); const double f = p - i;
    auto at = [&] (int k) { return (k >= 0 && k < n) ? (double) d[k] : 0.0; };
    const double xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const double c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

static double kaiserI0 (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; }

std::vector<float> kaiserLP (double fc, double tw)
{
    const double beta = 7.857; int n = (int) std::ceil (72 / (2.285 * 2 * PI * tw)); if (n % 2 == 0) n++;
    std::vector<double> h ((size_t) n); const double M = (n - 1) / 2.0, ib = kaiserI0 (beta); double sum = 0;
    for (int i = 0; i < n; ++i)
    {
        const double k = i - M, u = k / M, sinc = k == 0 ? 2 * fc : std::sin (2 * PI * fc * k) / (PI * k);
        h[(size_t) i] = sinc * kaiserI0 (beta * std::sqrt (std::max (0.0, 1 - u * u))) / ib; sum += h[(size_t) i];
    }
    std::vector<float> o ((size_t) n); for (int i = 0; i < n; ++i) o[(size_t) i] = (float) (h[(size_t) i] / sum);
    return o;
}

constexpr int SNC_N = 32, SNC_H = 15, SNC_P = 16384;
static const std::vector<float>& sincTable()
{
    static const std::vector<float> t = []
    {
        std::vector<float> v ((size_t) (SNC_P + 1) * SNC_N);
        const double beta = 8.5, ib = kaiserI0 (beta), M = SNC_N / 2.0, fc = 0.485;
        double row[SNC_N];
        for (int p = 0; p <= SNC_P; ++p)
        {
            const double f = (double) p / SNC_P; double sum = 0;
            for (int i = 0; i < SNC_N; ++i)
            {
                const double x = i - SNC_H - f, u = x / M;
                const double s = std::abs (x) < 1e-9 ? 2 * fc : std::sin (2 * PI * fc * x) / (PI * x);
                const double w = std::abs (u) <= 1 ? kaiserI0 (beta * std::sqrt (1 - u * u)) / ib : 0;
                row[i] = s * w; sum += row[i];
            }
            for (int i = 0; i < SNC_N; ++i) v[(size_t) p * SNC_N + (size_t) i] = (float) (row[i] / sum);
        }
        return v;
    }();
    return t;
}

double sread (const float* d, int n, double p)
{
    static const float* T = sincTable().data();
    const double fl = std::floor (p); const int i = (int) fl;
    const float* row = T + (size_t) std::lround ((p - fl) * SNC_P) * SNC_N;
    const int o = i - SNC_H; double s = 0;
    if (o >= 0 && o + SNC_N <= n)
    {
        // 4 running sums so the CPU can overlap the multiplies; same result to within double rounding
        const float* x = d + o; double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        for (int k = 0; k < SNC_N; k += 4) { s0 += (double) row[k] * x[k]; s1 += (double) row[k + 1] * x[k + 1]; s2 += (double) row[k + 2] * x[k + 2]; s3 += (double) row[k + 3] * x[k + 3]; }
        s = (s0 + s1) + (s2 + s3);
    }
    else { for (int k = 0; k < SNC_N; ++k) { const int q = o + k; if (q >= 0 && q < n) s += (double) row[k] * d[q]; } }
    return s;
}

static const std::vector<float>& levelFilter (int h)
{
    static std::mutex m; static std::vector<std::unique_ptr<std::vector<float>>> F;
    std::lock_guard<std::mutex> l (m);
    if ((int) F.size() <= h) F.resize ((size_t) h + 1);
    if (! F[(size_t) h]) { const double q = std::pow (2.0, h / 4.0); F[(size_t) h] = std::make_unique<std::vector<float>> (kaiserLP (0.475 / q, 0.05 / q)); }
    return *F[(size_t) h];
}

Level buildLevel (const float* x, int n, int h, double a, double b)
{
    Level L;
    const int D = 1 << (h / 4);
    const auto& flt = levelFilter (h); const int FL = (int) flt.size(), c = (FL - 1) / 2;
    a = std::max (0.0, std::floor (a)); b = std::min ((double) n, std::ceil (b));
    const int off = (int) std::floor (a / D) * D, cnt = std::max (1, (int) std::ceil ((b - off) / D) + 1);
    L.d.resize ((size_t) cnt); L.D = D; L.off = off; L.a = a; L.b = b;
    for (int j = 0; j < cnt; ++j)
    {
        const int o = off + j * D - c; double s = 0;
        const int k0 = std::max (0, -o), k1 = std::min (FL, n - o);
        for (int k = k0; k < k1; ++k) s += (double) flt[(size_t) k] * x[o + k];
        L.d[(size_t) j] = (float) s;
    }
    return L;
}

void Mip::set (const float* data, int len)
{
    x = data; n = len; lv.clear(); lv.resize (1);
    lv[0] = std::make_unique<Level>(); lv[0]->d.assign (data, data + len); lv[0]->a = 0; lv[0]->b = len;
}

const Level& Mip::level (int h, double a, double b)
{
    if ((int) lv.size() <= h) lv.resize ((size_t) h + 1);
    auto& have = lv[(size_t) h];
    if (have && have->a <= a && have->b >= b) return *have;
    if (have) { a = std::min (a, have->a); b = std::max (b, have->b); }
    have = std::make_unique<Level> (buildLevel (x, n, h, a, b));
    return *have;
}

double Mip::read (double p, double r)
{
    if (r <= 1) return sread (x, n, p);
    const double t = 4 * std::log2 (r); const int h = (int) std::floor (t); const double f = t - h;
    const Level& A = ((int) lv.size() > h && lv[(size_t) h]) ? *lv[(size_t) h] : level (h);
    if (f <= 0.85) return lvRead (A, p);
    const Level& B = ((int) lv.size() > h + 1 && lv[(size_t) h + 1]) ? *lv[(size_t) h + 1] : level (h + 1);
    const double w = (f - 0.85) / 0.15;
    return lvRead (A, p) * (1 - w) + lvRead (B, p) * w;
}

std::vector<float> resample (const std::vector<float>& x, double ratio)
{
    // to a lower rate: low-pass first (below the new Nyquist), then read with the sinc table
    if (x.empty()) return {};
    std::vector<float> src;
    const std::vector<float>* in = &x;
    if (ratio < 0.999)
    {
        const auto flt = kaiserLP (0.475 * ratio, 0.05 * ratio); const int FL = (int) flt.size(), c = (FL - 1) / 2, n = (int) x.size();
        src.resize (x.size());
        for (int i = 0; i < n; ++i) { double s = 0; const int o = i - c, k0 = std::max (0, -o), k1 = std::min (FL, n - o); for (int k = k0; k < k1; ++k) s += (double) flt[(size_t) k] * x[(size_t) (o + k)]; src[(size_t) i] = (float) s; }
        in = &src;
    }
    const int n = (int) std::floor (x.size() * ratio);
    std::vector<float> y ((size_t) std::max (0, n));
    if (std::abs (ratio - 1) < 1e-9) { y.assign (x.begin(), x.end()); return y; }
    for (int i = 0; i < n; ++i) y[(size_t) i] = (float) sread (in->data(), (int) in->size(), i / ratio);
    return y;
}

static const std::vector<float>& osf() { static const std::vector<float> f = kaiserLP (0.25, 0.05); return f; }

std::vector<float> upsample2 (const std::vector<float>& x)
{
    const auto& F = osf(); const int n = (int) x.size(), M = 2 * n, L = (int) F.size(), c = (L - 1) / 2;
    std::vector<float> y ((size_t) M);
    for (int m = 0; m < M; ++m)
    {
        const int o = m - c; double s = 0;
        for (int k = (o & 1) ? 1 : 0; k < L; k += 2) { const int q = o + k; if (q >= 0 && q < M) s += (double) F[(size_t) k] * x[(size_t) (q >> 1)]; }
        y[(size_t) m] = (float) (2 * s);
    }
    return y;
}

std::vector<float> downsample2 (const std::vector<float>& u)
{
    const auto& F = osf(); const int M = (int) u.size(), n = M >> 1, L = (int) F.size(), c = (L - 1) / 2;
    std::vector<float> y ((size_t) n);
    for (int j = 0; j < n; ++j)
    {
        const int o = 2 * j - c; double s = 0; const int k0 = std::max (0, -o), k1 = std::min (L, M - o);
        for (int k = k0; k < k1; ++k) s += (double) F[(size_t) k] * u[(size_t) (o + k)];
        y[(size_t) j] = (float) s;
    }
    return y;
}
}
