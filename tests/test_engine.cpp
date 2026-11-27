// Pruebas del motor XEX03 con senales sinteticas.
// Compilar:  g++ -O2 -std=c++17 tests/test_engine.cpp -o test_engine && ./test_engine
#include "../Source/dsp/XexEngine.h"

#include <chrono>
#include <cstdio>
#include <random>

using xex::kPi;

static int failures = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        const bool ok_ = (cond);                          \
        if (! ok_) ++failures;                            \
        std::printf (ok_ ? "  ok:    " : "  FALLO: ");    \
        std::printf (__VA_ARGS__);                        \
        std::printf ("\n");                               \
    } while (0)

// ---------- generadores ----------
static std::vector<float> genVibrato (double sr, double f0, double depthCents, double vibHz, double secs, float amp = 0.5f)
{
    std::vector<float> x ((size_t) (sr * secs));
    double ph = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / sr;
        const double f = f0 * std::pow (2.0, depthCents * std::sin (2.0 * kPi * vibHz * t) / 1200.0);
        ph += 2.0 * kPi * f / sr;
        x[i] = amp * (float) std::sin (ph);
    }
    return x;
}

// voz sintetica: armonicos con envolvente formantica
static std::vector<float> genVoice (double sr, double f0, double secs)
{
    std::vector<float> x ((size_t) (sr * secs), 0.0f);
    for (int h = 1; h * f0 < 0.45 * sr && h < 40; ++h)
    {
        const double fh = h * f0;
        double a = 1.0 / h;
        a *= 1.0 + 4.0 * std::exp (-std::pow ((fh - 700.0) / 250.0, 2.0));
        a *= 1.0 + 2.5 * std::exp (-std::pow ((fh - 1800.0) / 300.0, 2.0));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] += (float) (a * std::sin (2.0 * kPi * fh * (double) i / sr + 0.3 * h));
    }
    float pk = 0; for (float v : x) pk = std::max (pk, std::fabs (v));
    for (float& v : x) v *= 0.5f / pk;
    return x;
}

static std::vector<float> run (xex::Engine& e, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = e.process (in[i]);
    return out;
}

// ---------- medicion independiente (autocorrelacion normalizada) ----------
static double measureF0 (const float* x, int N, double sr, double fmin, double fmax)
{
    const int lmin = (int) (sr / fmax), lmax = (int) (sr / fmin);
    std::vector<double> r ((size_t) lmax + 3, 0.0);
    double e0 = 0; for (int i = 0; i < N; ++i) e0 += (double) x[i] * x[i];
    double rmax = -1;
    for (int lag = lmin - 1; lag <= lmax + 1; ++lag)
    {
        double s = 0, e1 = 0;
        for (int i = 0; i < N; ++i) { s += (double) x[i] * x[i + lag]; e1 += (double) x[i + lag] * x[i + lag]; }
        r[(size_t) lag] = s / std::sqrt (e0 * e1 + 1e-12);
        if (lag >= lmin && lag <= lmax) rmax = std::max (rmax, r[(size_t) lag]);
    }
    for (int lag = lmin; lag <= lmax; ++lag)
    {
        const double v = r[(size_t) lag];
        if (v >= 0.9 * rmax && v >= r[(size_t) lag - 1] && v >= r[(size_t) lag + 1])
        {
            const double a = r[(size_t) lag - 1], b = v, c = r[(size_t) lag + 1];
            const double den = a + c - 2 * b;
            const double sh = std::fabs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
            return sr / ((double) lag + sh);
        }
    }
    return 0.0;
}

static double centsOf (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

// contorno en cents respecto a ref, entre t0 y t1
static std::vector<double> contour (const std::vector<float>& x, double sr, double t0, double t1, double ref)
{
    std::vector<double> c;
    const int N = (int) (0.025 * sr);
    for (double t = t0; t < t1; t += 0.01)
    {
        const int s = (int) (t * sr);
        if (s + N + (int) (sr / 100.0) + 4 >= (int) x.size()) break;
        const double f = measureF0 (&x[(size_t) s], N, sr, ref * 0.6, ref * 1.6);
        if (f > 0) c.push_back (centsOf (f, ref));
    }
    return c;
}

static double mean (const std::vector<double>& v) { double s = 0; for (double a : v) s += a; return v.empty() ? 0 : s / (double) v.size(); }
static double halfRange (std::vector<double> v)
{
    if (v.size() < 4) return 0;
    std::sort (v.begin(), v.end());
    return 0.5 * (v[(size_t) ((double) v.size() * 0.95)] - v[(size_t) ((double) v.size() * 0.05)]);
}

static xex::Params chromatic()
{
    xex::Params p; p.retuneMs = 0; p.flex = 0; p.humanize = 0; p.vibrato = 0; p.mix = 100; return p;
}

// centroide espectral (Hz) entre fLo y fHi, ventana Hann
static double centroidHz (const std::vector<float>& x, double sr, double t0, double t1, double fLo, double fHi)
{
    const size_t a = (size_t) (t0 * sr), b = (size_t) (t1 * sr);
    double sp = 0, sf = 0;
    for (double f = fLo; f <= fHi; f += 10.0)
    {
        double re = 0, im = 0;
        for (size_t i = a; i < b; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * kPi * (double) (i - a) / (double) (b - a));
            const double ph = 2 * kPi * f * (double) i / sr;
            re += w * x[i] * std::cos (ph); im += w * x[i] * std::sin (ph);
        }
        const double pw = re * re + im * im;
        sp += pw; sf += pw * f;
    }
    return sf / sp;
}

static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0; for (size_t i = a; i < b; ++i) e += (double) x[i] * x[i];
    return 10.0 * std::log10 (e / (double) (b - a) + 1e-20);
}

int main()
{
    std::printf ("XEX03 - pruebas del motor\n");

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("\n[1] Afinacion estatica 215 Hz -> A3 (220 Hz), sr=%.0f\n", sr);
        xex::Engine e; e.prepare (sr, 0); e.setParams (chromatic());
        auto in  = genVibrato (sr, 215.0, 0.0, 5.0, 2.0);
        auto out = run (e, in);
        auto c = contour (out, sr, 1.2, 1.8, 220.0);
        CHECK (! c.empty() && std::fabs (mean (c)) < 6.0, "error medio %.1f cents (limite 6)", mean (c));
    }

    {
        std::printf ("\n[2] Escala Do mayor: 229 Hz -> A3 (220 Hz)\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic();
        const bool maj[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
        for (int i = 0; i < 12; ++i) p.notes[(size_t) i] = maj[i];
        e.setParams (p);
        auto out = run (e, genVibrato (48000, 229.0, 0.0, 5.0, 2.0));
        auto c = contour (out, 48000, 1.2, 1.8, 220.0);
        CHECK (! c.empty() && std::fabs (mean (c)) < 6.0, "error medio %.1f cents", mean (c));
    }

    {
        std::printf ("\n[3] Vibrato conservado (vibrato = 0 %%)\n");
        xex::Engine e; e.prepare (48000, 0); e.setParams (chromatic());
        auto out = run (e, genVibrato (48000, 222.0, 40.0, 5.5, 3.0));
        auto c = contour (out, 48000, 1.5, 2.9, 220.0);
        CHECK (std::fabs (mean (c)) < 12.0, "centro en %.1f cents de A3", mean (c));
        CHECK (halfRange (c) > 25.0 && halfRange (c) < 55.0, "amplitud de vibrato %.1f cents (original 40)", halfRange (c));
    }

    {
        std::printf ("\n[4] Vibrato eliminado (vibrato = -100 %%)\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic(); p.vibrato = -100; e.setParams (p);
        auto out = run (e, genVibrato (48000, 222.0, 40.0, 5.5, 3.0));
        auto c = contour (out, 48000, 1.5, 2.9, 220.0);
        CHECK (std::fabs (mean (c)) < 10.0, "centro en %.1f cents de A3", mean (c));
        CHECK (halfRange (c) < 15.0, "amplitud residual %.1f cents (original 40)", halfRange (c));
    }

    {
        std::printf ("\n[5] Retune Speed lento (200 ms) vs rapido: 212 Hz -> G#3\n");
        const double target = 207.652;
        auto in = genVibrato (48000, 212.0, 0.0, 5.0, 3.0);
        xex::Engine slow; slow.prepare (48000, 0);
        auto p = chromatic(); p.retuneMs = 200; slow.setParams (p);
        auto o1 = run (slow, in);
        xex::Engine fast; fast.prepare (48000, 0); fast.setParams (chromatic());
        auto o2 = run (fast, in);
        const double eSlowEarly = std::fabs (mean (contour (o1, 48000, 0.12, 0.16, target)));
        const double eSlowLate  = std::fabs (mean (contour (o1, 48000, 2.2, 2.6, target)));
        const double eFastEarly = std::fabs (mean (contour (o2, 48000, 0.12, 0.16, target)));
        CHECK (eSlowEarly > eFastEarly + 6.0, "lento, error a 0.14 s: %.1f cents (el rapido: %.1f; el lento debe seguir corrigiendo)", eSlowEarly, eFastEarly);
        CHECK (eSlowLate  < 5.0,  "lento, error a 2.4 s: %.1f cents", eSlowLate);
        CHECK (eFastEarly < 6.0,  "rapido, error a 0.14 s: %.1f cents", eFastEarly);
    }

    {
        std::printf ("\n[6] Sin voz (ruido): debe pasar sin alterar, retrasado por la latencia\n");
        xex::Engine e; e.prepare (48000, 0); e.setParams (chromatic());
        std::mt19937 rng (1); std::normal_distribution<float> nd (0.0f, 0.1f);
        std::vector<float> in (48000 * 2); for (auto& v : in) v = nd (rng);
        auto out = run (e, in);
        const int L = e.latencySamples();
        double s = 0, a = 0, b = 0;
        for (size_t i = 48000; i < in.size(); ++i) { const double u = out[i], v = in[i - (size_t) L]; s += u * v; a += u * u; b += v * v; }
        const double corr = s / std::sqrt (a * b);
        CHECK (corr > 0.9, "correlacion con la entrada retrasada = %.3f (latencia %d muestras)", corr, L);
    }

    {
        std::printf ("\n[7] Voz sintetica 200 Hz -> G3 (196 Hz): altura y nivel\n");
        xex::Engine e; e.prepare (48000, 0); e.setParams (chromatic());
        auto in = genVoice (48000, 200.0, 2.5);
        auto out = run (e, in);
        auto c = contour (out, 48000, 1.5, 2.2, 196.0);
        CHECK (! c.empty() && std::fabs (mean (c)) < 8.0, "error medio %.1f cents", mean (c));
        double ri = 0, ro = 0;
        for (size_t i = 72000; i < 100000; ++i) { ri += (double) in[i] * in[i]; ro += (double) out[i] * out[i]; }
        const double db = 10.0 * std::log10 (ro / ri);
        CHECK (std::fabs (db) < 1.5, "diferencia de nivel %.2f dB", db);
    }

    {
        std::printf ("\n[8] Voz afinada: debe ser casi identica a la entrada retrasada\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic(); p.retuneMs = 20; e.setParams (p);
        auto in = genVoice (48000, 196.0, 2.0);
        auto out = run (e, in);
        const int L = e.latencySamples();
        double best = -1;
        for (int d = -4; d <= 4; ++d)
        {
            double s = 0, a = 0, b = 0;
            for (size_t i = 60000; i < 90000; ++i) { const double u = out[i], v = in[i - (size_t) (L + d)]; s += u * v; a += u * u; b += v * v; }
            best = std::max (best, s / std::sqrt (a * b));
        }
        CHECK (best > 0.95, "correlacion = %.3f", best);
    }

    {
        std::printf ("\n[9] Modos de latencia\n");
        for (int m = 0; m < 3; ++m)
        {
            xex::Engine e; e.prepare (48000, m);
            std::printf ("  modo %d: latencia %d muestras = %.1f ms\n", m, e.latencySamples(), 1000.0 * e.latencySamples() / 48000.0);
        }
        xex::Engine e; e.prepare (48000, 2); e.setParams (chromatic());
        auto out = run (e, genVibrato (48000, 330.0 * 1.0116, 0.0, 5.0, 2.0));
        auto c = contour (out, 48000, 1.2, 1.8, 329.63);
        CHECK (! c.empty() && std::fabs (mean (c)) < 8.0, "modo Ultra, 333.5 Hz -> E4: error %.1f cents", mean (c));
    }

    {
        std::printf ("\n[11] Flex-Tune: una nota 15 cents desafinada se corrige menos\n");
        auto in = genVoice (48000, 221.9, 2.5);   // ~ +15 cents sobre A3
        xex::Engine a; a.prepare (48000, 0); a.setParams (chromatic());
        auto p2 = chromatic(); p2.flex = 100;
        xex::Engine b; b.prepare (48000, 0); b.setParams (p2);
        const double ea = mean (contour (run (a, in), 48000, 1.5, 2.2, 220.0));
        const double eb = mean (contour (run (b, in), 48000, 1.5, 2.2, 220.0));
        CHECK (std::fabs (ea) < 6.0 && eb > 7.0, "flex 0: %.1f cents, flex 100: %.1f cents", ea, eb);
    }

    {
        std::printf ("\n[12] Mix 50 %% con la voz ya afinada: sin cancelaciones\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic(); p.mix = 50; e.setParams (p);
        auto in = genVoice (48000, 196.0, 2.0);
        auto out = run (e, in);
        double ri = 0, ro = 0;
        for (size_t i = 60000; i < 90000; ++i) { ri += (double) in[i] * in[i]; ro += (double) out[i] * out[i]; }
        const double db = 10.0 * std::log10 (ro / ri);
        CHECK (std::fabs (db) < 1.0, "diferencia de nivel %.2f dB", db);
    }

    {
        std::printf ("\n[10] Rendimiento\n");
        xex::Engine e; e.prepare (48000, 0); e.setParams (chromatic());
        auto in = genVoice (48000, 200.0, 10.0);
        const auto t0 = std::chrono::steady_clock::now();
        auto out = run (e, in);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (secs < 2.0, "10 s de audio procesados en %.3f s (%.1f %% de un nucleo)", secs, secs * 10.0);
    }

    std::printf ("\n===== FASE 2 =====\n");

    {
        std::printf ("\n[13] Transpose +12: 220 Hz -> 440 Hz\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic(); p.transpose = 12; e.setParams (p);
        auto in = genVoice (48000, 220.0, 2.5);
        auto out = run (e, in);
        auto c = contour (out, 48000, 1.5, 2.2, 440.0);
        CHECK (! c.empty() && std::fabs (mean (c)) < 10.0, "error medio %.1f cents", mean (c));
        CHECK (std::fabs (rmsDb (out, 72000, 100000) - rmsDb (in, 72000, 100000)) < 3.0, "nivel %.1f dB vs entrada", rmsDb (out, 72000, 100000) - rmsDb (in, 72000, 100000));
    }

    {
        std::printf ("\n[14] Transpose -12: 330 Hz -> 165 Hz\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic(); p.transpose = -12; e.setParams (p);
        auto in = genVoice (48000, 330.0, 2.5);
        auto out = run (e, in);
        auto c = contour (out, 48000, 1.5, 2.2, 164.81);
        CHECK (! c.empty() && std::fabs (mean (c)) < 12.0, "error medio %.1f cents", mean (c));
        CHECK (std::fabs (rmsDb (out, 72000, 100000) - rmsDb (in, 72000, 100000)) < 3.0, "nivel %.1f dB vs entrada", rmsDb (out, 72000, 100000) - rmsDb (in, 72000, 100000));
    }

    {
        std::printf ("\n[15] Conform to Scale: Do mayor, C4 + 1 semitono -> D4 (no C#4)\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic();
        const bool maj[12] = { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 };
        for (int i = 0; i < 12; ++i) p.notes[(size_t) i] = maj[i];
        p.transpose = 1; p.conform = true; e.setParams (p);
        auto out = run (e, genVoice (48000, 261.63, 2.5));
        auto c = contour (out, 48000, 1.5, 2.2, 293.66);
        CHECK (! c.empty() && std::fabs (mean (c)) < 10.0, "error respecto a D4: %.1f cents", mean (c));
    }

    {
        std::printf ("\n[16] Detune +30 cents: 220 Hz -> 223.8 Hz\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic(); p.detune = 30; e.setParams (p);
        auto out = run (e, genVoice (48000, 220.0, 2.5));
        auto c = contour (out, 48000, 1.5, 2.2, 220.0 * std::pow (2.0, 30.0 / 1200.0));
        CHECK (! c.empty() && std::fabs (mean (c)) < 8.0, "error medio %.1f cents", mean (c));
    }

    {
        std::printf ("\n[17] Formantes: cambian el timbre sin cambiar la nota\n");
        auto in = genVoice (48000, 196.0, 2.5);
        double cen[3]; double pit[3]; int idx = 0;
        for (float fs : { 0.0f, 7.0f, -5.0f })
        {
            xex::Engine e; e.prepare (48000, 0);
            auto p = chromatic(); p.formant = fs; e.setParams (p);
            auto out = run (e, in);
            cen[idx] = centroidHz (out, 48000, 1.6, 2.2, 300, 4500);
            auto c = contour (out, 48000, 1.5, 2.2, 196.0);
            pit[idx] = mean (c);
            ++idx;
        }
        CHECK (cen[1] > cen[0] * 1.25, "formant +7: centroide %.0f Hz (neutro %.0f Hz)", cen[1], cen[0]);
        CHECK (cen[2] < cen[0] * 0.85, "formant -5: centroide %.0f Hz", cen[2]);
        CHECK (std::fabs (pit[1]) < 10.0 && std::fabs (pit[2]) < 10.0, "la nota no cambia (%.1f / %.1f cents)", pit[1], pit[2]);
    }

    {
        std::printf ("\n[18] Formant Preserve apagado: subir una octava mueve tambien las formantes\n");
        auto in = genVoice (48000, 196.0, 2.5);
        double cen[2];
        for (int k = 0; k < 2; ++k)
        {
            xex::Engine e; e.prepare (48000, 0);
            auto p = chromatic(); p.transpose = 12; p.formantPreserve = (k == 0); e.setParams (p);
            cen[k] = centroidHz (run (e, in), 48000, 1.6, 2.2, 300, 6000);
        }
        CHECK (cen[1] > cen[0] * 1.3, "centroide: preservado %.0f Hz, siguiendo la altura %.0f Hz", cen[0], cen[1]);
    }

    {
        std::printf ("\n[19] Vibrato creativo (seno, 50 cents, 5.5 Hz) sobre nota fija\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic(); p.vibShape = 1; p.vibRate = 5.5f; p.vibDepth = 50; p.vibDelay = 0; p.vibOnset = 1; p.vibVariation = 0;
        e.setParams (p);
        auto out = run (e, genVoice (48000, 220.0, 3.0));
        auto c = contour (out, 48000, 1.5, 2.9, 220.0);
        CHECK (std::fabs (mean (c)) < 10.0, "centro en %.1f cents", mean (c));
        CHECK (halfRange (c) > 30.0 && halfRange (c) < 60.0, "amplitud %.1f cents (pedido 50)", halfRange (c));
    }

    {
        std::printf ("\n[20] Vibrato con Delay 1 s: la nota empieza estable\n");
        xex::Engine e; e.prepare (48000, 0);
        auto p = chromatic(); p.vibShape = 1; p.vibDepth = 50; p.vibDelay = 1000; p.vibOnset = 200; p.vibVariation = 0;
        e.setParams (p);
        auto out = run (e, genVoice (48000, 220.0, 3.5));
        auto early = contour (out, 48000, 0.5, 0.9, 220.0);
        auto late  = contour (out, 48000, 2.2, 3.2, 220.0);
        CHECK (halfRange (early) < 10.0 && halfRange (late) > 28.0, "amplitud inicial %.1f, final %.1f cents", halfRange (early), halfRange (late));
    }

    {
        std::printf ("\n[21] Tremolo (modulacion de amplitud)\n");
        auto in = genVoice (48000, 220.0, 3.0);
        double spread[2];
        for (int k = 0; k < 2; ++k)
        {
            xex::Engine e; e.prepare (48000, 0);
            auto p = chromatic(); p.vibShape = 1; p.vibDepth = 0; p.vibDelay = 0; p.vibOnset = 1; p.vibVariation = 0;
            p.vibTrem = k == 0 ? 0.0f : 100.0f; e.setParams (p);
            auto out = run (e, in);
            double lo = 1e9, hi = -1e9;
            for (size_t a = 72000; a + 480 < 140000; a += 480) { const double d = rmsDb (out, a, a + 480); lo = std::min (lo, d); hi = std::max (hi, d); }
            spread[k] = hi - lo;
        }
        CHECK (spread[1] > spread[0] + 3.0, "variacion de nivel: sin tremolo %.1f dB, con tremolo %.1f dB", spread[0], spread[1]);
    }

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
