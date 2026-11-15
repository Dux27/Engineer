#pragma once

#include <cmath>
#include <cstddef>

// Engineering quality gate, not a validated physiological classifier.
// Eight seconds at 100 Hz, decimated to 50 Hz after low-pass filtering.
class PPGPeriodEstimator
{
public:
    bool valid = false;
    float correlation = 0, relative_ac = 0, candidate_bpm = 0;
    unsigned estimate_id = 0;
    void reset()
    {
        valid = false;
        correlation = relative_ac = candidate_bpm = dc_ = ac_ = anchor_ = 0;
        hp1_ = hp2_ = lp1_ = lp2_ = 0;
        head_ = count_ = step_ = 0;
        divider_ = 0;
        initialized_ = false;
        estimate_id = 0;
        for (auto &value : ring_)
            value = 0;
    }
    void push(float raw)
    {
        if (!initialized_)
        {
            dc_ = anchor_ = raw;
            initialized_ = true;
        }
        // DC estimate is only the denominator of relative amplitude. For pulse
        // quality use proper second-order HP 0.65 Hz and LP 4 Hz biquads,
        // rather than the previous weak first-order baseline rejection.
        dc_ += 0.018674f * (raw - dc_);
        const float input = raw - anchor_;
        const float hp = 0.97153418f * input + hp1_;
        hp1_ = -1.94306837f * input + 1.94225790f * hp + hp2_;
        hp2_ = 0.97153418f * input - 0.94387883f * hp;
        ac_ = 0.01335920f * hp + lp1_;
        lp1_ = 0.02671840f * hp + 1.64745998f * ac_ + lp2_;
        lp2_ = 0.01335920f * hp - 0.70089678f * ac_;
        if (++divider_ != 2)
            return;
        divider_ = 0;
        ring_[head_] = ac_;
        head_ = (head_ + 1) % N;
        if (count_ < N)
            ++count_;
        if (++step_ < 50 || count_ != N)
            return;
        step_ = 0;
        evaluate();
    }

private:
    static constexpr size_t N = 400;
    float ring_[N]{};
    size_t head_ = 0, count_ = 0, step_ = 0;
    int divider_ = 0;
    float dc_ = 0, ac_ = 0;
    float anchor_ = 0, hp1_ = 0, hp2_ = 0, lp1_ = 0, lp2_ = 0;
    bool initialized_ = false;
    float at(size_t i) const
    {
        return ring_[(head_ + i) % N];
    }
    void evaluate()
    {
        ++estimate_id;
        // ESP32-S3 has a single-precision FPU. AC values are already centered;
        // float preserves correlation precision without slow software doubles.
        float sum = 0, square = 0;
        for (size_t i = 0; i < N; ++i)
        {
            const float x = at(i);
            sum += x;
            square += x * x;
        }
        const float variance = square / N - (sum / N) * (sum / N);
        relative_ac = dc_ > 0 ? std::sqrt(std::fmax(0.0f, variance)) / dc_ : 0;
        float correlations[103]{};
        for (size_t lag = 14; lag <= 101; ++lag)
        {
            float sx = 0, sy = 0, xx = 0, yy = 0, xy = 0;
            const float n = N - lag;
            for (size_t i = 0; i < N - lag; ++i)
            {
                const float x = at(i), y = at(i + lag);
                sx += x;
                sy += y;
                xx += x * x;
                yy += y * y;
                xy += x * y;
            }
            const float denominator =
                std::sqrt(std::fmax(0.0f, (xx - sx * sx / n) * (yy - sy * sy / n)));
            correlations[lag] = denominator > 1e-12f ? (xy - sx * sy / n) / denominator : 0;
        }
        float best = -1;
        int fallback = 0;
        for (size_t lag = 15; lag <= 100; ++lag)
            if (correlations[lag] > correlations[lag - 1] &&
                correlations[lag] >= correlations[lag + 1])
                if (correlations[lag] > best)
                {
                    best = correlations[lag];
                    fallback = static_cast<int>(lag);
                }
        int selected = 0;
        // First sufficiently correlated repeat is the fundamental period.
        // Requiring it to nearly equal a later (2T) peak produced half-rate
        // estimates on real wrist waveforms with alternating pulse morphology.
        for (int lag = 15; lag <= 100; ++lag)
            if (correlations[lag] > correlations[lag - 1] &&
                correlations[lag] >= correlations[lag + 1] && correlations[lag] >= 0.65f)
            {
                selected = lag;
                break;
            }
        if (!selected)
            selected = fallback;
        correlation = selected ? correlations[selected] : 0;
        float offset = 0;
        if (selected)
        {
            const float curvature = correlations[selected - 1] - 2 * correlations[selected] +
                                    correlations[selected + 1];
            if (std::fabs(curvature) > 1e-8f)
                offset = std::fmax(-0.5f, std::fmin(0.5f, 0.5f *
                                                              (correlations[selected - 1] -
                                                               correlations[selected + 1]) /
                                                              curvature));
        }
        candidate_bpm = selected ? 3000.0f / (selected + offset) : 0;
        // Moderate waveform correlation plus independent beat-period agreement
        // is used by MAX30102; there is no target/reference BPM in this gate.
        valid = selected && correlation >= 0.65f && relative_ac >= 0.00005f && relative_ac <= 0.05f;
    }
};
