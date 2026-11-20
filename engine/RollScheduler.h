#pragma once

#include <array>
#include <vector>

#include "engine/Expression.h"
#include "engine/FixedVector.h"
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

// Limits of the allocation-free API: what does not fit in a block is dropped.
constexpr std::size_t kMaxEvents = 2048;
constexpr std::size_t kMaxNotes = 256;
constexpr std::size_t kMaxCurvePoints = 16;  // the schema allows 16 points
using EventList = FixedVector<SchedulerEvent, kMaxEvents>;
using NoteList = FixedVector<NoteEvent, kMaxNotes>;

// Turns note events and the host transport into sample-accurate triggers (spec Parts 7 and 9).
// It knows nothing about JUCE and plays no audio.
//
// Everything on the audio thread is allocation-free: the roll is generated into fixed-capacity
// lists at Note-On (or at the quantized start), and setConfig copies into storage that was
// reserved up front (curves of up to kMaxCurvePoints points).
class RollScheduler {
public:
    explicit RollScheduler(double sampleRate) : sampleRate_(sampleRate) {
        config_.expression.curve.reserve(kMaxCurvePoints);
    }

    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate; }
    void setConfig(const RollConfig& config) { config_ = config; }
    void reset();

    // `notes` must be sorted by sampleOffset. Events come back sorted by sampleOffset.
    // This overload allocates; it is meant for tests and tools.
    std::vector<SchedulerEvent> process(int numSamples, const std::vector<NoteEvent>& notes,
                                        const Transport& transport);

    // The allocation-free variant for the audio thread. Notes and events beyond the limits are dropped.
    void process(int numSamples, const NoteEvent* notes, int numNotes, const Transport& transport, EventList& out);

    bool isRolling() const { return rolling_; }
    bool isPending() const { return pending_; }

private:
    static constexpr double kUnitsPerBeat = 16.0;  // 1/64 units per quarter note

    struct Active {
        ExpressedList triggers;                      // windowed, velocities already followed
        std::array<char, kMaxTriggers> fired{};
        double length = 0.0;  // window length in units
        double pos = 0.0;     // units since the roll started
        bool reverse = false;
    };

    void startRoll(int playedVelocity);
    void advanceActive(double& cursor, double target, double bpm, EventList& out);

    double sampleRate_;
    RollConfig config_;

    Active active_;
    bool rolling_ = false;
    TriggerList scratch_;  // the full roll while it is generated

    bool pending_ = false;
    double pendingTargetBeat_ = 0.0;
    int pendingVelocity_ = 100;
};

}  // namespace dw
