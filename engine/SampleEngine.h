#pragma once

#include <array>
#include <memory>
#include <vector>

#include "engine/RollGenerator.h"
#include "engine/RollScheduler.h"

namespace dw {

// Decoded sample audio. Mono samples leave `right` empty.
struct SampleData {
    std::vector<float> left;
    std::vector<float> right;
    double sampleRate = 44100.0;
};
using SamplePtr = std::shared_ptr<const SampleData>;

// Per-sample parameters of spec Part 8.
struct SampleParams {
    double tuneSemitones = 0.0;  // playback speed: +12 = twice as fast
    double start = 0.0;          // fraction of the sample length
    double end = 1.0;
    double levelDb = 0.0;
};

// Engine constants, hidden and not stored in presets (spec Part 8).
constexpr double kAttackMs = 5.0;
constexpr double kReleaseMs = 10.0;
constexpr double kFadeDb = 60.0;  // fades are linear in dB between -60 dB and 0 dB

// Plays the triggers of the roll scheduler (spec Part 8). It knows nothing about JUCE.
//
// Every trigger releases the running voices over 10 ms and starts a new voice with a 5 ms attack
// (hard retrigger). The velocity is applied linearly (velocity / 127) to both samples. A trigger
// for a slot without a sample does nothing.
class SampleEngine {
public:
    explicit SampleEngine(double outputRate = 44100.0) { setSampleRate(outputRate); }

    // Not real-time safe: builds the fade tables.
    void setSampleRate(double outputRate);

    void setSample(Sample slot, SamplePtr data, const SampleParams& params);
    void setParams(Sample slot, const SampleParams& params);
    void reset();

    // Overwrites the buffers. `events` must be sorted by sampleOffset (as the scheduler returns them).
    void process(float* left, float* right, int numSamples, const std::vector<SchedulerEvent>& events);

    int activeVoices() const;

private:
    static constexpr int kMaxVoices = 4;

    struct Voice {
        enum class State { Idle, Playing, Releasing };
        State state = State::Idle;
        SamplePtr data;
        double pos = 0.0;
        double endFrame = 0.0;
        double rate = 1.0;
        float gain = 1.0f;
        int attackPos = 0;
        int releasePos = 0;
        float releaseStart = 1.0f;
    };

    void trigger(Sample slot, int velocity);
    void stopAll();
    void render(float* left, float* right, int from, int to);
    float envelopeNow(const Voice& v) const;

    double sampleRate_ = 44100.0;
    int attackLen_ = 0;
    int releaseLen_ = 0;
    std::vector<float> attackTable_;
    std::vector<float> releaseTable_;

    std::array<SamplePtr, 2> samples_;
    std::array<SampleParams, 2> params_;
    std::array<Voice, kMaxVoices> voices_;
};

}  // namespace dw
