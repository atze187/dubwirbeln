#pragma once

#include <optional>
#include <vector>

#include "engine/Expression.h"
#include "engine/RollGenerator.h"
#include "engine/VelocityFollow.h"

namespace dw {

enum class PlaybackMode { Trigger, Hold };
enum class Quantize { Off, Sixteenth, Eighth, Quarter, Bar };

struct PlaybackSettings {
    PlaybackMode mode = PlaybackMode::Trigger;
    double windowBars = 4.0;  // 4, 2, 1, 0.5
    Quantize quantize = Quantize::Off;
    VelocityFollow follow = VelocityFollow::VelocityIntensity;
};

struct RollConfig {
    bool enabled = true;  // false = no roll (preset without a roll type): the instrument is silent
    RollSpec spec;
    ExpressionSpec expression;
    PlaybackSettings playback;
};

// Host transport at the start of a block. Without a host: 120 BPM, not playing. 4/4 is assumed.
struct Transport {
    double bpm = 120.0;
    bool playing = false;
    double ppqPosition = 0.0;  // quarter notes, valid when playing
};

// Already filtered to the trigger note by the caller.
struct NoteEvent {
    int sampleOffset = 0;
    bool noteOn = true;
    int velocity = 100;
};

struct SchedulerEvent {
    enum class Kind { Trigger, Stop };
    Kind kind = Kind::Trigger;
    int sampleOffset = 0;        // within the block
    Sample sample = Sample::A;   // Trigger only
    int velocity = 0;            // Trigger only, 1..127
};

// Turns note events and the host transport into sample-accurate triggers (spec Parts 7 and 9).
// It knows nothing about JUCE and plays no audio.
class RollScheduler {
public:
    explicit RollScheduler(double sampleRate) : sampleRate_(sampleRate) {}

    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate; }
    void setConfig(const RollConfig& config) { config_ = config; }
    void reset();

    // `notes` must be sorted by sampleOffset. Events come back sorted by sampleOffset.
    std::vector<SchedulerEvent> process(int numSamples, const std::vector<NoteEvent>& notes,
                                        const Transport& transport);

    bool isRolling() const { return active_.has_value(); }
    bool isPending() const { return pending_.has_value(); }

private:
    static constexpr double kUnitsPerBeat = 16.0;  // 1/64 units per quarter note

    struct Active {
        std::vector<ExpressedTrigger> triggers;  // windowed, velocities already followed
        std::vector<char> fired;
        double length = 0.0;  // window length in units
        double pos = 0.0;     // units since the roll started
        bool reverse = false;
    };
    struct Pending {
        double targetBeat = 0.0;
        int velocity = 100;
    };

    void startRoll(int playedVelocity);
    void advanceActive(double& cursor, double target, double bpm, std::vector<SchedulerEvent>& out);

    double sampleRate_;
    RollConfig config_;
    std::optional<Active> active_;
    std::optional<Pending> pending_;
};

}  // namespace dw
