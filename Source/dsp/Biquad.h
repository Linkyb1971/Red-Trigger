#pragma once
#include <cmath>
#include <algorithm>

namespace dt
{
/** RBJ-cookbook biquad. Coefficients can be changed without resetting state. */
struct BiquadCoeffs
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    enum class Type { lowPass, highPass, peak, lowShelf, highShelf };

    static BiquadCoeffs make (Type type, double fs, double freq, double q, double gainDb = 0.0)
    {
        freq = std::clamp (freq, 5.0, fs * 0.49);
        q = std::max (q, 0.05);
        const double w0 = 2.0 * 3.14159265358979323846 * freq / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double sqA2a = 2.0 * std::sqrt (A) * alpha;

        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type)
        {
            case Type::lowPass:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
            case Type::highPass:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
                break;
            case Type::peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
                break;
            case Type::lowShelf:
                b0 = A * ((A + 1) - (A - 1) * cw + sqA2a);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - sqA2a);
                a0 = (A + 1) + (A - 1) * cw + sqA2a;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - sqA2a;
                break;
            case Type::highShelf:
                b0 = A * ((A + 1) + (A - 1) * cw + sqA2a);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - sqA2a);
                a0 = (A + 1) - (A - 1) * cw + sqA2a;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - sqA2a;
                break;
        }

        BiquadCoeffs c;
        c.b0 = (float) (b0 / a0); c.b1 = (float) (b1 / a0); c.b2 = (float) (b2 / a0);
        c.a1 = (float) (a1 / a0); c.a2 = (float) (a2 / a0);
        return c;
    }
};

class Biquad
{
public:
    void setCoeffs (const BiquadCoeffs& newCoeffs) noexcept { c = newCoeffs; }
    void reset() noexcept { z1 = z2 = 0.0f; }

    inline float process (float x) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    void process (float* data, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            data[i] = process (data[i]);
    }

private:
    BiquadCoeffs c;
    float z1 = 0.0f, z2 = 0.0f;
};

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); }
} // namespace dt
