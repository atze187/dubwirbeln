#include "engine/RollScheduler.h"

#include <algorithm>
#include <cmath>

namespace dw {

namespace {

constexpr double kEps = 1e-6;

double gridInBeats(Quantize q) {
    switch (q) {
        case Quantize::Sixteenth: return 0.25;
        case Quantize::Eighth: return 0.5;
        case Quantize::Quarter: return 1.0;
        case Quantize::Bar: return 4.0;
        case Quantize::Off: break;
    }
    return 0.0;
}

}  // namespace

void RollScheduler::reset() {
    rolling_ = false;
    pending_ = false;
}

void RollScheduler::startRoll(int playedVelocity) {
    const auto follow = config_.playback.follow;
    RollSpec spec = config_.spec;
    spec.intensity = effectiveIntensity(spec.intensity, follow, playedVelocity);

    generateRollInto(spec, kUnitsPerBar, scratch_);
    expressTriggersInto(scratch_, config_.expression, kUnitsPerBar, active_.triggers);
    applyWindowInto(active_.triggers, spec.type, config_.playback.windowBars);
    for (size_t i = 0; i < active_.triggers.size(); ++i)
        active_.triggers[i].velocity = followVelocity(active_.triggers[i].velocity, follow, playedVelocity);

    const auto range = windowRange(spec.type, config_.playback.windowBars);
    std::fill(active_.fired.begin(), active_.fired.end(), char{0});
    active_.length = range.end - range.begin;
    active_.pos = 0.0;
    active_.reverse = false;
    rolling_ = true;
    pending_ = false;
}

// Moves the active roll from sample `cursor` to sample `target` of the block and emits its triggers.
void RollScheduler::advanceActive(double& cursor, double target, double bpm, EventList& out) {
    const double samplesPerUnit = sampleRate_ * 60.0 / bpm / kUnitsPerBeat;
    const double unitsPerMs = bpm * kUnitsPerBeat / 60000.0;

    auto emit = [&](const ExpressedTrigger& t, double at) {
        const int s = std::max(0, static_cast<int>(std::ceil(at - kEps)));
        out.push_back({SchedulerEvent::Kind::Trigger, s, t.trigger.sample, t.velocity});
    };

    while (rolling_) {
        Active& a = active_;
        const double phaseEnd = a.reverse ? 2.0 * a.length : a.length;

        if (a.pos >= phaseEnd - 1e-9) {
            // Phase finished: whatever is still unfired (delayed past the end) plays now.
            for (size_t i = 0; i < a.triggers.size(); ++i)
                if (!a.fired[i]) emit(a.triggers[i], cursor);
            if (!a.reverse && config_.playback.mode == PlaybackMode::Hold) {
                // Roll backward: mirrored positions, the trigger at the window start is not repeated.
                size_t kept = 0;
                for (size_t i = 0; i < a.triggers.size(); ++i) {
                    ExpressedTrigger t = a.triggers[i];
                    if (t.trigger.position <= 0) continue;
                    t.trigger.position = static_cast<int>(2.0 * a.length) - t.trigger.position;
                    t.swingUnits = 0.0;
                    t.humanizeMs = 0.0;
                    a.triggers[kept++] = t;
                }
                a.triggers.truncate(kept);
                std::fill(a.fired.begin(), a.fired.end(), char{0});
                a.reverse = true;
                continue;
            }
            rolling_ = false;
            return;
        }
        if (cursor >= target) return;

        const double samplesToEnd = (phaseEnd - a.pos) * samplesPerUnit;
        const bool endsHere = samplesToEnd <= target - cursor;
        const double segment = endsHere ? samplesToEnd : target - cursor;
        const double limit = endsHere ? phaseEnd : a.pos + segment / samplesPerUnit;

        for (size_t i = 0; i < a.triggers.size(); ++i) {
            if (a.fired[i]) continue;
            const auto& t = a.triggers[i];
            const double eff =
                a.reverse ? t.trigger.position
                          : std::max(0.0, t.trigger.position + t.swingUnits + t.humanizeMs * unitsPerMs);
            if (eff >= limit) continue;
            const double at = cursor + std::max(0.0, eff - a.pos) * samplesPerUnit;
            if (std::ceil(at - kEps) >= target) continue;  // belongs to the next block
            a.fired[i] = 1;
            emit(t, at);
        }
        a.pos = limit;
        cursor += segment;
    }
    cursor = target;
}

void RollScheduler::process(int numSamples, const NoteEvent* notes, int numNotes, const Transport& transport,
                            EventList& out) {
    out.clear();
    const double bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;
    const double beatsPerSample = bpm / (60.0 * sampleRate_);
    double cursor = 0.0;

    auto advanceTo = [&](double target) {
        while (cursor < target) {
            if (pending_) {
                double start = std::ceil(cursor - kEps);
                if (transport.playing) {
                    const double s = std::ceil((pendingTargetBeat_ - transport.ppqPosition) / beatsPerSample - kEps);
                    start = std::max(start, s);
                }
                if (start < target) {
                    advanceActive(cursor, start, bpm, out);
                    startRoll(pendingVelocity_);
                    cursor = start;
                    continue;
                }
            }
            advanceActive(cursor, target, bpm, out);
            cursor = target;
        }
    };

    for (int i = 0; i < numNotes && i < static_cast<int>(kMaxNotes); ++i) {
        const NoteEvent& n = notes[i];
        advanceTo(n.sampleOffset);
        if (n.noteOn && !config_.enabled) {
            continue;  // no roll type: the instrument is silent
        } else if (n.noteOn) {
            const double grid = gridInBeats(config_.playback.quantize);
            bool immediate = grid <= 0.0 || !transport.playing;
            double target = 0.0;
            if (!immediate) {
                const double beatAtNote = transport.ppqPosition + n.sampleOffset * beatsPerSample;
                target = std::ceil(beatAtNote / grid - 1e-9) * grid;
                immediate = (target - beatAtNote) / beatsPerSample < 1e-3;
            }
            if (immediate) {
                startRoll(n.velocity);
            } else {
                pending_ = true;
                pendingTargetBeat_ = target;
                pendingVelocity_ = n.velocity;
            }
        } else if (config_.playback.mode == PlaybackMode::Hold) {
            pending_ = false;
            if (rolling_) {
                out.push_back({SchedulerEvent::Kind::Stop, n.sampleOffset, Sample::A, 0});
                rolling_ = false;
            }
        }
    }
    advanceTo(numSamples);

    // Stable insertion sort by sample offset: the events are nearly sorted, and this does not allocate.
    for (size_t i = 1; i < out.size(); ++i) {
        const SchedulerEvent e = out[i];
        size_t j = i;
        for (; j > 0 && e.sampleOffset < out[j - 1].sampleOffset; --j) out[j] = out[j - 1];
        out[j] = e;
    }
}

std::vector<SchedulerEvent> RollScheduler::process(int numSamples, const std::vector<NoteEvent>& notes,
                                                   const Transport& transport) {
    EventList list;
    process(numSamples, notes.data(), static_cast<int>(notes.size()), transport, list);
    return std::vector<SchedulerEvent>(list.begin(), list.end());
}

}  // namespace dw
