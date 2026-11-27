// XEX03 - motor de afinacion vocal (C++ puro, sin dependencias de JUCE)
//
// Cadena:  entrada -> deteccion de F0 (YIN) -> nota objetivo (Key/Scale)
//          -> logica de correccion (Retune, Flex, Humanize, Vibrato)
//          -> cambio de altura PSOLA -> mezcla seco/procesado
//
// Procesa una sola voz (monofonica), muestra a muestra.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace xex
{
constexpr double kPi = 3.14159265358979323846;

struct Params
{
    float retuneMs = 20.0f;   // 0..400  velocidad de correccion
    float flex     = 0.0f;    // 0..100  tolerancia alrededor de la nota
    float humanize = 0.0f;    // 0..100  correccion mas lenta en notas sostenidas
    float vibrato  = 0.0f;    // -100..+100 %  0 = conserva el vibrato original
    float mix      = 100.0f;  // 0..100  % de senal procesada

    std::array<bool, 12> notes;   // notas permitidas por clase de altura (C = 0)
    Params() { notes.fill (true); }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setLowpass (double fs, double fc, double q = 0.70710678)
    {
        const double w = 2.0 * kPi * fc / fs, cw = std::cos (w), sw = std::sin (w);
        const double al = sw / (2.0 * q), a0 = 1.0 + al;
        b0 = (float) ((1.0 - cw) * 0.5 / a0);
        b1 = (float) ((1.0 - cw) / a0);
        b2 = b0;
        a1 = (float) (-2.0 * cw / a0);
        a2 = (float) ((1.0 - al) / a0);
    }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

class Engine
{
public:
    enum Mode { Normal = 0, Low = 1, Ultra = 2 };

    // mode fija la nota mas grave detectable (y con ello la latencia):
    //   Normal 70 Hz, Low 110 Hz, Ultra 180 Hz
    void prepare (double sampleRate, int mode)
    {
        sr = sampleRate;
        static const double minF0s[3] = { 70.0, 110.0, 180.0 };
        minF0 = minF0s[std::clamp (mode, 0, 2)];
        maxF0 = 1000.0;

        tMax    = (int) std::ceil (sr / minF0);
        tMin    = (int) std::floor (sr / maxF0);
        latency = 2 * tMax;

        decim = std::max (1, (int) std::lround (sr / 12000.0));
        srd   = sr / decim;
        if (decim > 1)
        {
            lp1.setLowpass (sr, 0.3 * srd);
            lp2.setLowpass (sr, 0.3 * srd);
        }

        yinTauMin = std::max (2, (int) std::floor (srd / maxF0));
        yinTauMax = (int) std::ceil (srd / minF0) + 2;
        yinW      = std::max (160, yinTauMax + 24);

        int n = 1;
        while (n < 8 * tMax + 8192) n <<= 1;
        nMask = n - 1;
        inBuf.assign ((size_t) n, 0.0f);
        acc.assign ((size_t) n, 0.0f);
        norm.assign ((size_t) n, 0.0f);

        decBuf.assign (2048, 0.0f);
        decMask = 2047;
        tmp.assign ((size_t) (yinW + yinTauMax), 0.0f);
        dpr.assign ((size_t) (yinTauMax + 2), 0.0f);

        ratioSlew = 1.0f - std::exp (-1.0f / (0.002f * (float) sr));
        wvSlew    = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
        reset();
    }

    void reset()
    {
        std::fill (inBuf.begin(), inBuf.end(), 0.0f);
        std::fill (acc.begin(), acc.end(), 0.0f);
        std::fill (norm.begin(), norm.end(), 0.0f);
        std::fill (decBuf.begin(), decBuf.end(), 0.0f);
        lp1.reset(); lp2.reset();
        n = 0; decCount = 0; decPhase = 0; hopCount = 0;
        markCount = 0; lastMark = 0; nextSynth = (double) tMax; lastS = 0.0;
        t0Cur = std::clamp (sr / 200.0, (double) tMin, (double) tMax);
        ratioNow = ratioTarget = 1.0f;
        wv = wvTarget = 0.0f;
        vRun = uvRun = 0; voicedState = false; tracking = false;
        corrCenter = 0; mCenter = 0; stableTime = 0; lastNote = -1;
        histN = 0;
        tel = Telemetry();
    }

    void setParams (const Params& p) { params = p; }
    int  latencySamples() const { return latency; }

    struct Telemetry
    {
        bool  voiced     = false;
        float detMidi    = 0.0f;   // altura detectada (MIDI, con decimales)
        float outMidi    = 0.0f;   // altura tras la correccion
        int   targetNote = -1;     // nota MIDI objetivo (-1 = ninguna)
    };
    const Telemetry& telemetry() const { return tel; }

    float process (float x)
    {
        // 1) deteccion de altura sobre senal diezmada
        float y = x;
        if (decim > 1) y = lp2.process (lp1.process (x));
        if (++decPhase >= decim)
        {
            decPhase = 0;
            decBuf[(size_t) (decCount & decMask)] = y;
            ++decCount;
            if (++hopCount >= hopDec) { hopCount = 0; analyse(); }
        }

        // 2) guardar entrada
        inBuf[(size_t) (n & nMask)] = x;

        // 3) marcas de analisis sincronas con la forma de onda (una por periodo)
        while (lastMark + (int64_t) (2.3 * t0Cur) + 4 <= n)
            makeMark();

        // 4) marcas de sintesis con el nuevo periodo
        ratioNow += (ratioTarget - ratioNow) * ratioSlew;
        while (nextSynth <= (double) (n + tMax))
        {
            const double ts = t0Cur / (double) ratioNow;
            double s = nextSynth;
            const int k = nearestMark (s - (double) latency);
            if (k >= 0)
            {
                // mientras no hay voz se bloquea la fase: la salida queda alineada con la
                // entrada retrasada, para que Mix no produzca cancelaciones al empezar la voz
                const double locked = (double) markPos[(size_t) k] + (double) latency;
                if (! voicedState && wv < 0.02f)
                {
                    if (locked > lastS + 0.5 * ts) s = locked;
                }
                else
                {
                    // con voz y correccion casi nula, un empuje suave mantiene la fase
                    // alineada con la entrada (evita el efecto peine al mezclar seco y procesado)
                    const double dev = std::fabs ((double) ratioNow - 1.0);
                    const double delta = locked - s;
                    if (dev < 0.001 && std::fabs (delta) < 0.5 * ts)
                        s += 0.08 * (1.0 - dev / 0.001) * delta;
                }
                placeGrain (k, s);
            }
            lastS = s;
            nextSynth = s + ts;
        }

        // 5) salida
        const size_t idx = (size_t) (n & nMask);
        const float  dry = inBuf[(size_t) ((n - latency) & nMask)];
        const float  nm  = norm[idx];
        const float  wet = (nm > 0.15f) ? acc[idx] / nm : dry;
        acc[idx] = 0.0f;
        norm[idx] = 0.0f;

        wv += (wvTarget - wv) * wvSlew;
        const float proc = wv * wet + (1.0f - wv) * dry;
        const float m = std::clamp (params.mix * 0.01f, 0.0f, 1.0f);

        ++n;
        return m * proc + (1.0f - m) * dry;
    }

private:
    // ---------------- deteccion de altura (YIN) ----------------
    void analyse()
    {
        const int total = yinW + yinTauMax;
        if (decCount < total) return;

        double energy = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const float v = decBuf[(size_t) ((decCount - total + i) & decMask)];
            tmp[(size_t) i] = v;
            energy += (double) v * v;
        }
        if (std::sqrt (energy / total) < 0.0015) { control (false, 0.0f); return; }

        dpr[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= yinTauMax; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < yinW; ++j)
            {
                const float diff = tmp[(size_t) j] - tmp[(size_t) (j + tau)];
                d += (double) diff * diff;
            }
            running += d;
            dpr[(size_t) tau] = (float) (running > 1e-12 ? d * tau / running : 1.0);
        }

        int best = -1;
        for (int tau = yinTauMin; tau < yinTauMax; ++tau)
        {
            if (dpr[(size_t) tau] < 0.20f)
            {
                while (tau + 1 < yinTauMax && dpr[(size_t) (tau + 1)] < dpr[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            int g = yinTauMin;
            for (int tau = yinTauMin; tau < yinTauMax; ++tau)
                if (dpr[(size_t) tau] < dpr[(size_t) g]) g = tau;
            if (dpr[(size_t) g] < 0.35f) best = g;
        }
        if (best < 2 || best >= yinTauMax) { control (false, 0.0f); return; }

        const float s0 = dpr[(size_t) (best - 1)], s1 = dpr[(size_t) best], s2 = dpr[(size_t) (best + 1)];
        const float den = s0 + s2 - 2.0f * s1;
        const float shift = std::fabs (den) > 1e-9f ? std::clamp (0.5f * (s0 - s2) / den, -1.0f, 1.0f) : 0.0f;
        const double f0 = srd / ((double) best + shift);

        if (f0 < minF0 * 0.9 || f0 > maxF0) { control (false, 0.0f); return; }
        control (true, (float) f0);
    }

    // ---------------- logica de correccion ----------------
    int nearestAllowed (float m) const
    {
        const int base = (int) std::floor (m);
        int   best = -1;
        float bestD = 1e9f;
        for (int cand = base - 3; cand <= base + 4; ++cand)
        {
            const int pc = ((cand % 12) + 12) % 12;
            if (! params.notes[(size_t) pc]) continue;
            const float d = std::fabs (m - (float) cand);
            if (d < bestD) { bestD = d; best = cand; }
        }
        // histeresis: conservar la nota anterior salvo que la nueva gane claramente
        if (lastNote >= 0 && best >= 0 && best != lastNote)
        {
            const int pc = ((lastNote % 12) + 12) % 12;
            if (params.notes[(size_t) pc] && std::fabs (m - (float) lastNote) < bestD + 0.12f)
                return lastNote;
        }
        return best;
    }

    void control (bool det, float f0)
    {
        const double dt = (double) (hopDec * decim) / sr;

        if (det) { ++vRun; uvRun = 0; } else { ++uvRun; vRun = 0; }
        if (! voicedState && vRun >= 2) voicedState = true;
        if (voicedState && uvRun >= 3) { voicedState = false; tracking = false; }

        if (! voicedState)
        {
            corrCenter = 0.0f;
            ratioTarget = 1.0f;
            wvTarget = 0.0f;
            tel.voiced = false;
            tel.targetNote = -1;
            return;
        }
        wvTarget = 1.0f;
        if (! det) return;   // se mantiene el ultimo estado

        // mediana de 3 para rechazar saltos de octava aislados
        const float cents = 1200.0f * std::log2 (f0);
        if (histN == 0 || ! tracking) { hist[0] = hist[1] = hist[2] = cents; histN = 3; }
        else { hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = cents; }
        const float med = std::max (std::min (hist[0], hist[1]),
                                    std::min (std::max (hist[0], hist[1]), hist[2]));
        const double fMed = std::pow (2.0, med / 1200.0);

        t0Cur = std::clamp (sr / fMed, (double) tMin, (double) tMax);

        const float m = 69.0f + 12.0f * std::log2 ((float) fMed / 440.0f);

        if (! tracking)
        {
            tracking = true;
            mCenter = m; corrCenter = 0.0f; stableTime = 0.0f; lastNote = -1;
        }

        // centro de la nota: sigue la altura lentamente (ignora el vibrato)
        float d = m - mCenter;
        if (std::fabs (d) > 1.2f) { mCenter = m; d = 0.0f; stableTime = 0.0f; }
        else
        {
            mCenter += d * (1.0f - std::exp ((float) -dt / 0.06f));
            d = m - mCenter;
        }

        const int note = nearestAllowed (mCenter);
        if (note == lastNote) stableTime += (float) dt; else stableTime = 0.0f;
        lastNote = note;

        float target = 0.0f;
        if (note >= 0)
        {
            const float e  = (float) note - mCenter;
            const float dz = params.flex * 0.01f * 0.5f;          // zona de tolerancia (semitonos)
            const float w  = dz > 1e-4f ? std::min (1.0f, std::fabs (e) / dz) : 1.0f;
            target = e * w;
        }

        // Humanize: en notas sostenidas la correccion se hace mas lenta
        const float h       = std::clamp (params.humanize * 0.01f, 0.0f, 1.0f);
        const float sustain = std::clamp ((stableTime - 0.12f) / 0.25f, 0.0f, 1.0f);
        const float tau     = params.retuneMs * 0.001f * (1.0f - h * sustain) + 0.25f * h * sustain;
        const float alpha   = tau < 1e-4f ? 1.0f : 1.0f - std::exp ((float) -dt / tau);
        corrCenter += alpha * (target - corrCenter);

        // vibrato: 0 conserva el original, -100 % lo elimina, +100 % lo duplica
        const float v    = std::clamp (params.vibrato * 0.01f, -1.0f, 1.0f);
        const float corr = note >= 0 ? corrCenter + v * d : 0.0f;

        ratioTarget = std::clamp (std::pow (2.0f, corr / 12.0f), 0.5f, 2.0f);

        tel.voiced = true;
        tel.detMidi = m;
        tel.outMidi = m + corr;
        tel.targetNote = note;
    }

    // ---------------- PSOLA ----------------
    int nearestMark (double c) const
    {
        int best = -1;
        double bestD = 1e18;
        const int64_t first = std::max<int64_t> (0, markCount - 64);
        for (int64_t i = markCount - 1; i >= first; --i)
        {
            const double dist = std::fabs ((double) markPos[(size_t) (i & 127)] - c);
            if (dist < bestD) { bestD = dist; best = (int) (i & 127); }
            else if ((double) markPos[(size_t) (i & 127)] < c) break;
        }
        return best;
    }

    float in (int64_t i) const { return i < 0 ? 0.0f : inBuf[(size_t) (i & nMask)]; }

    // Siguiente marca: se busca, alrededor de lastMark + T0, el desplazamiento que mejor
    // iguala la forma de onda del periodo anterior. Asi las marcas siguen el periodo real
    // de la senal (incluido el vibrato) y no la estimacion de F0, que llega con retraso.
    void makeMark()
    {
        const int T     = std::clamp ((int) std::lround (t0Cur), tMin, tMax);
        const int Hw    = T / 2;
        const int range = T / 4;

        int    bestS = 0;
        double bestV = -1e30;
        for (int sh = -range; sh <= range; ++sh)
        {
            const int64_t p2 = lastMark + T + sh;
            double num = 0.0, den = 1e-9;
            for (int k = -Hw; k <= Hw; ++k)
            {
                const float a = in (lastMark + k), b = in (p2 + k);
                num += (double) a * b;
                den += (double) b * b;
            }
            const double v = num / std::sqrt (den);
            if (v > bestV) { bestV = v; bestS = sh; }
        }

        const int64_t p = lastMark + T + bestS;
        const size_t  k = (size_t) (markCount & 127);
        markPos[k] = p;
        markT0[k]  = (float) (T + bestS);
        ++markCount;
        lastMark = p;
    }

    void placeGrain (int k, double s)
    {
        const int64_t mp = markPos[(size_t) k];
        const int H = std::clamp ((int) std::lround ((double) markT0[(size_t) k]), 8, tMax);
        const int64_t sInt = (int64_t) std::llround (s);
        const float invH = (float) kPi / (float) H;

        for (int j = -H; j <= H; ++j)
        {
            const int64_t o = sInt + j;
            const int64_t i = mp + j;
            if (o < n || i > n || i < 0) continue;
            const float w = 0.5f * (1.0f + std::cos (invH * (float) j));
            const size_t oi = (size_t) (o & nMask);
            acc[oi]  += w * inBuf[(size_t) (i & nMask)];
            norm[oi] += w;
        }
    }

    // ---------------- estado ----------------
    Params params;
    double sr = 48000.0, srd = 12000.0, minF0 = 70.0, maxF0 = 1000.0;
    int tMax = 686, tMin = 48, latency = 1372, decim = 4;
    int yinTauMin = 12, yinTauMax = 174, yinW = 200;
    static constexpr int hopDec = 16;

    Biquad lp1, lp2;
    std::vector<float> inBuf, acc, norm, decBuf, tmp, dpr;
    int64_t nMask = 0, decMask = 0;

    int64_t n = 0, decCount = 0;
    int decPhase = 0, hopCount = 0;

    int64_t markCount = 0;
    int64_t markPos[128] = {};
    float   markT0[128] = {};
    int64_t lastMark = 0;
    double  nextSynth = 0.0, lastS = 0.0, t0Cur = 240.0;

    float ratioNow = 1.0f, ratioTarget = 1.0f, ratioSlew = 0.01f;
    float wv = 0.0f, wvTarget = 0.0f, wvSlew = 0.005f;

    int  vRun = 0, uvRun = 0;
    bool voicedState = false, tracking = false;
    float corrCenter = 0.0f, mCenter = 0.0f, stableTime = 0.0f;
    int   lastNote = -1;
    float hist[3] = { 0, 0, 0 };
    int   histN = 0;

    Telemetry tel;
};

} // namespace xex
