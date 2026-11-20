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
    active_.reset();
    pending_.reset();
}

void RollScheduler::startRoll(int playedVelocity) {
    const auto& follow = config_.playback.follow;
    RollSpec spec = config_.spec;
    spec.intensity = effectiveIntensity(spec.intensity, follow, playedVelocity);

    auto triggers = applyWindow(expressTriggers(generateRoll(spec), config_.expression), spec.type,
                                config_.playback.windowBars);
    for (auto& t : triggers) t.velocity = followVelocity(t.velocity, follow, playedVelocity);

    const auto range = windowRange(spec.type, config_.playback.windowBars);
    Active a;
    a.fired.assign(triggers.size(), 0);
    a.triggers = std::move(triggers);
    a.length = range.end - range.begin;
    active_ = std::move(a);
    pending_.reset();
}

// Moves the active roll from sample `cursor` to sample `target` of the block and emits its triggers.
void RollScheduler::advanceActive(double& cursor, double target, double bpm, std::vector<SchedulerEvent>& out) {
    const double samplesPerUnit = sampleRate_ * 60.0 / bpm / kUnitsPerBeat;
    const double unitsPerMs = bpm * kUnitsPerBeat / 60000.0;

    auto emit = [&](const ExpressedTrigger& t, double at) {
        const int s = std::max(0, static_cast<int>(std::ceil(at - kEps)));
        out.push_back({SchedulerEvent::Kind::Trigger, s, t.trigger.sample, t.velocity});
    };

    while (active_) {
        Active& a = *active_;
        const double phaseEnd = a.reverse ? 2.0 * a.length : a.length;

        if (a.pos >= phaseEnd - 1e-9) {
            // Phase finished: whatever is still unfired (delayed past the end) plays now.
            for (size_t i = 0; i < a.triggers.size(); ++i)
                if (!a.fired[i]) emit(a.triggers[i], cursor);
            if (!a.reverse && config_.playback.mode == PlaybackMode::Hold) {
                // Roll backward: mirrored positions, the trigger at the window start is not repeated.
                std::vector<ExpressedTrigger> back;
                for (auto t : a.triggers) {
                    if (t.trigger.position <= 0) continue;
                    t.trigger.position = static_cast<int>(2.0 * a.length) - t.trigger.position;
                    t.swingUnits = 0.0;
                    t.humanizeMs = 0.0;
                    back.push_back(t);
                }
                a.triggers = std::move(back);
                a.fired.assign(a.triggers.size(), 0);
                a.reverse = true;
                continue;
            }
            active_.reset();
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

std::vector<SchedulerEvent> RollScheduler::process(int numSamples, const std::vector<NoteEvent>& notes,
                                                   const Transport& transport) {
    std::vector<SchedulerEvent> out;
    const double bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;
    const double beatsPerSample = bpm / (60.0 * sampleRate_);
    double cursor = 0.0;

    auto advanceTo = [&](double target) {
        while (cursor < target) {
            if (pending_) {
                double start = std::ceil(cursor - kEps);
                if (transport.playing) {
                    const double s = std::ceil((pending_->targetBeat - transport.ppqPosition) / beatsPerSample - kEps);
                    start = std::max(start, s);
                }
                if (start < target) {
                    advanceActive(cursor, start, bpm, out);
                    startRoll(pending_->velocity);
                    cursor = start;
                    continue;
                }
            }
            advanceActive(cursor, target, bpm, out);
            cursor = target;
        }
    };

    for (const auto& n : notes) {
        advanceTo(n.sampleOffset);
        if (n.noteOn) {
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
                pending_ = Pending{target, n.velocity};
            }
        } else if (config_.playback.mode == PlaybackMode::Hold) {
            pending_.reset();
            if (active_) {
                out.push_back({SchedulerEvent::Kind::Stop, n.sampleOffset, Sample::A, 0});
                active_.reset();
            }
        }
    }
    advanceTo(numSamples);

    std::stable_sort(out.begin(), out.end(),
                     [](const SchedulerEvent& a, const SchedulerEvent& b) { return a.sampleOffset < b.sampleOffset; });
    return out;
}

}  // namespace dw
