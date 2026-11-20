#include "engine/SampleEngine.h"

#include <algorithm>
#include <cmath>

namespace dw {

namespace {
int slotIndex(Sample s) { return s == Sample::A ? 0 : 1; }
}  // namespace

void SampleEngine::setSampleRate(double outputRate) {
    sampleRate_ = outputRate > 0.0 ? outputRate : 44100.0;
    attackLen_ = std::max(1, static_cast<int>(std::lround(kAttackMs * 0.001 * sampleRate_)));
    releaseLen_ = std::max(1, static_cast<int>(std::lround(kReleaseMs * 0.001 * sampleRate_)));

    // Fades are linear in dB: attack from -60 dB up to 0 dB, release from the start level down by 60 dB.
    const double dbPerUnit = kFadeDb / 20.0;
    attackTable_.resize(static_cast<size_t>(attackLen_));
    for (int i = 0; i < attackLen_; ++i)
        attackTable_[static_cast<size_t>(i)] = static_cast<float>(std::pow(10.0, -dbPerUnit * (1.0 - static_cast<double>(i) / attackLen_)));
    releaseTable_.resize(static_cast<size_t>(releaseLen_));
    for (int j = 0; j < releaseLen_; ++j)
        releaseTable_[static_cast<size_t>(j)] = static_cast<float>(std::pow(10.0, -dbPerUnit * static_cast<double>(j) / releaseLen_));
    reset();
}

void SampleEngine::setSample(Sample slot, SamplePtr data, const SampleParams& params) {
    samples_[static_cast<size_t>(slotIndex(slot))] = std::move(data);
    params_[static_cast<size_t>(slotIndex(slot))] = params;
}

void SampleEngine::setParams(Sample slot, const SampleParams& params) {
    params_[static_cast<size_t>(slotIndex(slot))] = params;
}

void SampleEngine::reset() {
    for (auto& v : voices_) {
        v.state = Voice::State::Idle;
        v.data.reset();
    }
}

int SampleEngine::activeVoices() const {
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(),
                                          [](const Voice& v) { return v.state != Voice::State::Idle; }));
}

float SampleEngine::envelopeNow(const Voice& v) const {
    return v.attackPos < attackLen_ ? attackTable_[static_cast<size_t>(v.attackPos)] : 1.0f;
}

void SampleEngine::stopAll() {
    for (auto& v : voices_) {
        if (v.state != Voice::State::Playing) continue;
        v.releaseStart = envelopeNow(v);
        v.releasePos = 0;
        v.state = Voice::State::Releasing;
    }
}

void SampleEngine::trigger(Sample slot, int velocity) {
    const auto index = static_cast<size_t>(slotIndex(slot));
    const SamplePtr& data = samples_[index];
    if (!data || data->left.empty()) return;

    const SampleParams& p = params_[index];
    const double frames = static_cast<double>(data->left.size());
    const double startFrame = std::clamp(p.start, 0.0, 1.0) * frames;
    const double endFrame = std::clamp(p.end, 0.0, 1.0) * frames;
    if (!(endFrame > startFrame)) return;

    stopAll();  // hard retrigger: the running voices fade out while the new one fades in

    Voice* target = nullptr;
    for (auto& v : voices_)
        if (v.state == Voice::State::Idle) {
            target = &v;
            break;
        }
    if (target == nullptr) {  // pool exhausted: take the voice that has been fading out the longest
        target = &*std::max_element(voices_.begin(), voices_.end(),
                                    [](const Voice& a, const Voice& b) { return a.releasePos < b.releasePos; });
    }

    const double level = std::pow(10.0, p.levelDb / 20.0);
    const int v = std::clamp(velocity, 1, 127);
    target->state = Voice::State::Playing;
    target->data = data;
    target->pos = startFrame;
    target->endFrame = endFrame;
    target->rate = std::pow(2.0, p.tuneSemitones / 12.0) * data->sampleRate / sampleRate_;
    target->gain = static_cast<float>(level * v / 127.0);
    target->attackPos = 0;
    target->releasePos = 0;
    target->releaseStart = 1.0f;
}

void SampleEngine::render(float* left, float* right, int from, int to) {
    for (auto& v : voices_) {
        if (v.state == Voice::State::Idle) continue;
        const SampleData& d = *v.data;
        const auto last = static_cast<int>(d.left.size()) - 1;
        const float* l = d.left.data();
        const float* r = d.right.empty() ? l : d.right.data();

        for (int i = from; i < to; ++i) {
            if (v.pos >= v.endFrame) {
                v.state = Voice::State::Idle;
                break;
            }
            float env;
            if (v.state == Voice::State::Playing) {
                env = envelopeNow(v);
                if (v.attackPos < attackLen_) ++v.attackPos;
            } else {
                if (v.releasePos >= releaseLen_) {
                    v.state = Voice::State::Idle;
                    break;
                }
                env = v.releaseStart * releaseTable_[static_cast<size_t>(v.releasePos++)];
            }

            const int idx = static_cast<int>(v.pos);
            const float frac = static_cast<float>(v.pos - idx);
            const int next = std::min(idx + 1, last);
            const float g = env * v.gain;
            left[i] += (l[idx] * (1.0f - frac) + l[next] * frac) * g;
            right[i] += (r[idx] * (1.0f - frac) + r[next] * frac) * g;
            v.pos += v.rate;
        }
        if (v.state == Voice::State::Idle) v.data.reset();
    }
}

void SampleEngine::process(float* left, float* right, int numSamples, const std::vector<SchedulerEvent>& events) {
    process(left, right, numSamples, events.data(), static_cast<int>(events.size()));
}

void SampleEngine::process(float* left, float* right, int numSamples, const SchedulerEvent* events, int numEvents) {
    std::fill(left, left + numSamples, 0.0f);
    std::fill(right, right + numSamples, 0.0f);

    int cursor = 0;
    for (int i = 0; i < numEvents; ++i) {
        const auto& e = events[i];
        const int at = std::clamp(e.sampleOffset, cursor, numSamples);
        render(left, right, cursor, at);
        cursor = at;
        if (e.kind == SchedulerEvent::Kind::Trigger) trigger(e.sample, e.velocity);
        else stopAll();
    }
    render(left, right, cursor, numSamples);
}

}  // namespace dw
