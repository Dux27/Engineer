#pragma once
#include <algorithm>
#include <cstdint>

// Smooth three waveform-period estimates. A brief harmonic ambiguity must not
// turn a previous 80 bpm into 40/160 bpm. A sustained new rate is accepted after
// ten seconds; no target/reference BPM is stored here.
class PPGRateHistory
{
public:
    bool valid = false;
    float bpm = 0;
    void reset()
    {
        count_ = head_ = 0;
        last_good_ = last_trusted_ = ambiguity_start_ = 0;
        trusted_bpm_ = bpm = 0;
        valid = false;
    }
    void update(float candidate, uint64_t sample, bool usable)
    {
        valid = false;
        if (!usable)
        {
            ambiguity_start_ = 0;
            return;
        }
        const float ratio = trusted_bpm_ > 0 ? candidate / trusted_bpm_ : 1;
        const bool octave = (ratio > .45f && ratio < .55f) || (ratio > 1.8f && ratio < 2.2f);
        if (octave && sample - last_trusted_ < 3000)
        {
            if (!ambiguity_start_)
                ambiguity_start_ = sample;
            if (sample - ambiguity_start_ < 1000)
                return;
            count_ = head_ = 0;
            trusted_bpm_ = 0;
        }
        else
            ambiguity_start_ = 0;
        if (sample - last_good_ > 500)
            count_ = head_ = 0;
        last_good_ = sample;
        values_[head_] = candidate;
        head_ = (head_ + 1) % 3;
        if (count_ < 3)
            ++count_;
        if (count_ < 3)
            return;
        float sorted[3]{values_[0], values_[1], values_[2]};
        std::sort(sorted, sorted + 3);
        bpm = sorted[1];
        valid = true;
        trusted_bpm_ = bpm;
        last_trusted_ = sample;
    }

private:
    float values_[3]{}, trusted_bpm_ = 0;
    unsigned head_ = 0, count_ = 0;
    uint64_t last_good_ = 0, last_trusted_ = 0, ambiguity_start_ = 0;
};
