#include "engine/Engine.h"
#include <algorithm>
#include <cmath>
#include "engine/LoopTiming.h"

namespace dc {

namespace {
// A host position further than this from the extrapolated one counts as a transport jump
// (~5 ms at 120 bpm). Smaller deviations come from tempo changes inside a block and host rounding.
constexpr double kJumpToleranceBeats = 0.01;
// Region detection looks this far ahead so a boundary that the host position hits exactly is not
// missed by floating-point rounding (far below one sample at any tempo).
constexpr double kPpqEpsilon = 1e-9;

bool validTransport(const TransportInfo& t)
{
    return t.playing && std::isfinite(t.ppq) && t.ppq >= 0.0 && std::isfinite(t.bpm) && t.bpm > 0.0;
}

size_t framesOf(const StereoData* d) { return d != nullptr ? std::min(d->left.size(), d->right.size()) : 0; }
} // namespace

void Engine::prepare(double sampleRate, int maxBlock)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::isfinite(sampleRate) && sampleRate > 0.0)
        sampleRate_ = sampleRate;
    maxBlock_ = std::max(0, maxBlock);
    // process() is not running (contract), so the audio-thread state may be set here.
    outRate_ = sampleRate_;
    grainLen_ = std::max(16, static_cast<int>(std::lround(LoopReader::kGrainMs * 0.001 * outRate_)));
    fadeLen_ = std::max(1, static_cast<int>(std::lround(kCancelFadeMs * 0.001 * outRate_)));
    for (auto& rt : owned_)
        rt->reader1.prepare(outRate_);
    resetPlayback();
}

void Engine::setModel(std::shared_ptr<const Model> model)
{
    auto rt = std::make_unique<Runtime>();
    rt->model = std::move(model);
    std::lock_guard<std::mutex> lock(mutex_);
    rt->reader1.prepare(sampleRate_);
    if (rt->model)
        rt->reader1.setData(rt->model->loop1.data);
    next_.store(rt.get());
    owned_.push_back(std::move(rt));
    releaseRetiredLocked();
}

void Engine::releaseRetired()
{
    std::lock_guard<std::mutex> lock(mutex_);
    releaseRetiredLocked();
}

void Engine::releaseRetiredLocked()
{
    // seq_cst: next_ was stored before inUse_ is read here; process() stores inUse_ and then re-reads
    // next_. So a Runtime the audio thread may use is always next_ or the inUse_ read below.
    const Runtime* next = next_.load();
    const Runtime* used = inUse_.load();
    std::erase_if(owned_, [&](const std::unique_ptr<Runtime>& rt) { return rt.get() != next && rt.get() != used; });
}

void Engine::resetPlayback()
{
    running_ = false;
    lastRegion1_ = -1;
    lastCycle1_ = 0;
    lastPpq_ = 0.0;
    sequencer_.reset();
    for (Voice* v : { &voice_, &tail_ })
    {
        v->state = Voice::State::Idle;
        v->gain = 0.0f;
        v->fadeLeft = 0;
        v->endedByItself = false;
        v->gl.reset();
        v->gr.reset();
    }
    if (current_ != nullptr)
        current_->reader1.reset();
}

void Engine::startVoice(const Model& m, int region2, PlayMode mode)
{
    Voice& v = voice_;
    const double len = static_cast<double>(framesOf(m.loop2.data.get()));
    const auto [lo01, hi01] = m.loop2.regions.bounds(region2);
    v.endedByItself = false;
    if (!(len > 0.0) || !(hi01 > lo01))
    {
        // Nothing to play (no Loop-2 data or unknown region): the voice ends at once.
        v.state = Voice::State::Idle;
        v.gain = 0.0f;
        sequencer_.onVoiceEnded();
        return;
    }
    v.lo = lo01 * len;
    v.hi = hi01 * len;
    v.mode = mode;
    v.elapsed = 0.0;
    v.state = Voice::State::Playing;
    v.gain = 0.0f;
    v.fadeLeft = fadeLen_; // fade in
    v.gl.start(v.lo, 1.0, grainLen_);
    v.gr.start(v.lo, 1.0, grainLen_);
}

void Engine::beginFadeOut(Voice& v) const
{
    v.state = Voice::State::FadingOut;
    v.fadeLeft = std::max(1, static_cast<int>(std::ceil(v.gain * static_cast<float>(fadeLen_))));
    v.fadeDelta = v.gain / static_cast<float>(v.fadeLeft);
}

void Engine::requestStart(const Model& m, int region2, PlayMode mode)
{
    if (voice_.state != Voice::State::Idle)
    {
        // Cancel: the running voice fades out as the tail while the new region starts on this frame.
        // A tail still fading from an earlier cancel (draws < kCancelFadeMs apart) is replaced.
        tail_ = voice_; // plain copy of fixed-size state, no allocation
        tail_.endedByItself = false;
        if (tail_.state == Voice::State::Playing)
            beginFadeOut(tail_);
    }
    startVoice(m, region2, mode);
}

void Engine::requestStop()
{
    voice_.endedByItself = false;
    if (voice_.state == Voice::State::Playing)
        beginFadeOut(voice_);
}

bool Engine::stepVoice(Voice& v, const StereoData& d, GrainFrame gf, double adv, float& outL, float& outR) const
{
    gf.lo = v.lo;
    gf.hi = v.hi;
    const double src = v.lo + v.elapsed;
    const float yl = v.gl.process(d.left, src, grainLen_, gf);
    const float yr = v.gr.process(d.right, src, grainLen_, gf);
    outL += v.gain * yl;
    outR += v.gain * yr;

    // Gain ramp (integer counters, so fades end exactly).
    if (v.state == Voice::State::Playing)
    {
        if (v.fadeLeft > 0)
            v.gain = --v.fadeLeft > 0 ? std::min(1.0f, v.gain + 1.0f / static_cast<float>(fadeLen_)) : 1.0f;
    }
    else if (--v.fadeLeft > 0)
        v.gain = std::max(0.0f, v.gain - v.fadeDelta);
    else
    {
        v.state = Voice::State::Idle;
        v.gain = 0.0f;
        return v.endedByItself;
    }

    // Position in the region.
    const double regionLen = v.hi - v.lo;
    v.elapsed += adv;
    if (v.mode == PlayMode::Gate)
    {
        if (v.elapsed >= regionLen)
            v.elapsed = std::fmod(v.elapsed, regionLen);
    }
    else if (v.elapsed >= regionLen)
    {
        // One-shot reached its region end (normally its end fade has run out by now). Not "by itself"
        // when a Stop was already fading it out: the Sequencer has been reset then.
        const bool byItself = v.endedByItself || v.state == Voice::State::Playing;
        v.state = Voice::State::Idle;
        v.gain = 0.0f;
        return byItself;
    }
    else if (v.state == Voice::State::Playing && (regionLen - v.elapsed) / adv <= fadeLen_)
    {
        beginFadeOut(v); // end fade of the one-shot, finishing at the region end
        v.endedByItself = true;
    }
    return false;
}

void Engine::process(const TransportInfo& t, float* l, float* r, int n)
{
    if (n <= 0 || l == nullptr || r == nullptr)
        return;

    // Take the latest Runtime (hazard pointer: announce, then confirm it is still published).
    Runtime* rt = next_.load();
    for (;;)
    {
        inUse_.store(rt);
        Runtime* again = next_.load();
        if (again == rt)
            break;
        rt = again;
    }
    if (rt != current_)
    {
        current_ = rt;
        resetPlayback(); // model change: hard restart, the next valid frame is a fresh entry
    }

    const Model* m = current_ != nullptr ? current_->model.get() : nullptr;
    const double ppqStep = std::isfinite(t.bpm) ? t.bpm / (60.0 * outRate_) : 0.0;
    if (m == nullptr || !validTransport(t) || !std::isfinite(ppqStep) || !(ppqStep > 0.0))
    {
        std::fill(l, l + n, 0.0f);
        std::fill(r, r + n, 0.0f);
        if (running_)
            resetPlayback();
        return;
    }

    const double bpb = beatsPerBar(t.tsNum, t.tsDen);
    const double beats1 = std::max(1, m->loop1.bars) * bpb;
    const double beats2 = std::max(1, m->loop2.bars) * bpb;

    // Loop 1: phase-locked to the host for the whole block.
    const StereoData* d1 = m->loop1.data.get();
    const double phase1 = std::fmod(t.ppq, beats1) / beats1;
    const double step1 = sourceStep(framesOf(d1), beats1, t.bpm, outRate_);
    current_->reader1.render(l, r, n, phase1, ppqStep / beats1, step1);

    // Loop 2: one voice, region entries detected per sample (sample-accurate boundaries).
    const StereoData* d2 = m->loop2.data.get();
    const size_t frames2 = framesOf(d2);
    const double len2 = static_cast<double>(frames2);
    const bool loop2Valid = sourceStep(frames2, beats2, t.bpm, outRate_) > 0.0;
    // Source frames per output frame at the host tempo (product first: exact for integral ratios).
    const double adv2 = loop2Valid ? len2 * t.bpm / (60.0 * outRate_ * beats2) : 0.0;
    GrainFrame gf;
    const double srcRate2 = d2 != nullptr && std::isfinite(d2->sampleRate) && d2->sampleRate > 0.0 ? d2->sampleRate : outRate_;
    gf.step = srcRate2 / outRate_; // pitch from the sample rates only; tempo is in adv2
    gf.dir = 1.0;
    gf.cycle = true; // grains wrap inside the region: Gate repeats seamlessly

    bool entry = !running_ || std::abs(t.ppq - expectedPpq_) > kJumpToleranceBeats;
    if (entry)
        lastPpq_ = t.ppq;
    const bool jumped = running_ && entry;
    running_ = true;

    for (int i = 0; i < n; ++i)
    {
        // Never step backwards inside continuous playback (host rounding at block starts).
        const double ppq = std::max(t.ppq + i * ppqStep, lastPpq_);
        lastPpq_ = ppq;
        const double pos = ppq + kPpqEpsilon;
        const long long cycle = static_cast<long long>(std::floor(pos / beats1));
        const double ph = (pos - static_cast<double>(cycle) * beats1) / beats1;
        const int region = m->loop1.regions.regionAt(ph);
        Sequencer::Command c;
        if (entry || region != lastRegion1_ || cycle != lastCycle1_)
        {
            const bool jumpNow = jumped && i == 0;
            entry = false;
            lastRegion1_ = region;
            lastCycle1_ = cycle;
            c = sequencer_.onRegion1(region, m->rules); // None: region without rule, not a draw
            if (c.type == Sequencer::Command::Type::Start)
                requestStart(*m, c.region2, c.mode);
            else if (c.type == Sequencer::Command::Type::Stop)
                requestStop();
            else if (jumpNow)
            {
                // Spec: a transport jump ends Loop 2, also when the new region has no rule.
                requestStop();
                sequencer_.reset();
            }
        }
        // Gate ends when Loop 1 leaves its region and no new draw took over (same frame as the entry).
        if (c.type == Sequencer::Command::Type::None && sequencer_.shouldGateStop(region))
        {
            requestStop();
            sequencer_.onVoiceEnded();
        }

        if (adv2 <= 0.0)
        {
            // Loop-2 timing invalid (cannot happen with valid transport and data): stay silent.
            if (voice_.state != Voice::State::Idle)
                sequencer_.onVoiceEnded();
            voice_.state = Voice::State::Idle;
            tail_.state = Voice::State::Idle;
            continue;
        }
        if (tail_.state != Voice::State::Idle)
            stepVoice(tail_, *d2, gf, adv2, l[i], r[i]);
        if (voice_.state != Voice::State::Idle && stepVoice(voice_, *d2, gf, adv2, l[i], r[i]))
            sequencer_.onVoiceEnded(); // one-shot finished by itself
    }

    expectedPpq_ = t.ppq + n * ppqStep;

    // Final safety: never hand non-finite samples to the host.
    for (int i = 0; i < n; ++i)
    {
        if (!std::isfinite(l[i]))
            l[i] = 0.0f;
        if (!std::isfinite(r[i]))
            r[i] = 0.0f;
    }
}

} // namespace dc
