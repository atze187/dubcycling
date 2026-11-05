#include "engine/Engine.h"
#include <algorithm>
#include <cmath>
#include <tuple>
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
        rt->reader1->prepare(outRate_); // a shared reader is prepared more than once: harmless
    resetPlayback();
    resetPreview();
    previewReq_.store(-1, std::memory_order_relaxed);

    // Effects: bus and chain sized for maxBlock (no bus without a block size: Loop 2 then stays dry).
    busL_.assign(static_cast<size_t>(maxBlock_), 0.0f);
    busR_.assign(static_cast<size_t>(maxBlock_), 0.0f);
    mask_ = sliceMask_ = 0;
    maskOwner_ = MaskOwner::None;
    maskChangeCount_ = 0;
    busFrame_ = 0;
    chain_.setMask(0);
    chain_.prepare(outRate_, std::max(1, maxBlock_)); // resets every insert
    fxActive_.store(false, std::memory_order_relaxed);
}

void Engine::setTargetMask(std::uint8_t mask)
{
    if (mask == mask_)
        return;
    mask_ = mask;
    if (maskChangeCount_ < kMaxMaskChanges)
        maskChanges_[static_cast<size_t>(maskChangeCount_++)] = { busFrame_, mask };
    else
        maskChanges_[kMaxMaskChanges - 1].mask = mask; // overflow: the latest state wins
}

void Engine::refreshMask(const Model& m)
{
    // The switches of the sounding region are re-read only if its index still names the same
    // stretch of Loop 2 (same bounds). After a boundary edit before it the indices (and the
    // remapped switch masks) shift while the voice keeps its old index: then it keeps its mask.
    const auto sameRegion = [&m](const Voice& v) {
        const double len = static_cast<double>(framesOf(m.loop2.data.get()));
        if (!(len > 0.0) || v.region2 < 0 || v.region2 >= m.loop2.regions.regionCount())
            return false;
        const auto [lo01, hi01] = m.loop2.regions.bounds(v.region2);
        constexpr double kTolFrames = 0.5;
        return std::abs(lo01 * len - v.lo) <= kTolFrames && std::abs(hi01 * len - v.hi) <= kTolFrames;
    };
    if (maskOwner_ == MaskOwner::Voice && voice_.state == Voice::State::Playing)
    {
        if (sameRegion(voice_))
            setTargetMask(m.fx.maskFor(voice_.region2));
    }
    else if (maskOwner_ == MaskOwner::Preview && preview_.slot == 1 && preview_.voice.state != Voice::State::Idle)
    {
        if (sameRegion(preview_.voice))
            setTargetMask(m.fx.maskFor(preview_.voice.region2));
    }
}

void Engine::releasePreviewMask()
{
    if (maskOwner_ != MaskOwner::Preview)
        return;
    for (const PreviewVoice* pv : { &preview_, &previewTail_ })
        if (pv->slot == 1 && pv->voice.state != Voice::State::Idle)
            return; // a Loop-2 audition still sounds
    if (voice_.state == Voice::State::Playing && current_ != nullptr && current_->model)
    {
        maskOwner_ = MaskOwner::Voice;
        setTargetMask(current_->model->fx.maskFor(voice_.region2));
    }
    else
    {
        maskOwner_ = MaskOwner::None;
        setTargetMask(0);
    }
}

void Engine::runChain(float* l, float* r, int n)
{
    float* bl = busL_.data();
    float* br = busR_.data();
    if (maskChangeCount_ > 0 || sliceMask_ != 0 || chain_.anyActive())
    {
        std::uint8_t cur = sliceMask_;
        int from = 0;
        for (int k = 0; k < maskChangeCount_; ++k)
        {
            const MaskChange& c = maskChanges_[static_cast<size_t>(k)];
            const int to = std::clamp(c.frame, from, n);
            if (to > from)
            {
                chain_.setMask(cur);
                chain_.process(bl + from, br + from, to - from);
                from = to;
            }
            cur = c.mask;
        }
        chain_.setMask(cur);
        if (n > from)
            chain_.process(bl + from, br + from, n - from);
    }
    for (int i = 0; i < n; ++i)
    {
        l[i] += bl[i];
        r[i] += br[i];
    }
}

void Engine::resetPreview()
{
    for (PreviewVoice* pv : { &preview_, &previewTail_ })
    {
        pv->voice.state = Voice::State::Idle;
        pv->voice.gain = 0.0f;
        pv->voice.fadeLeft = 0;
        pv->voice.endedByItself = false;
        pv->voice.gl.reset();
        pv->voice.gr.reset();
        pv->slot = -1;
    }
}

void Engine::dropStalePreviews()
{
    for (PreviewVoice* pv : { &preview_, &previewTail_ })
    {
        if (pv->voice.state == Voice::State::Idle)
            continue;
        const std::uint64_t id = current_ == nullptr ? 0 : (pv->slot == 0 ? current_->data1Id : current_->data2Id);
        if (id != pv->dataId)
        {
            pv->voice.state = Voice::State::Idle;
            pv->voice.gain = 0.0f;
            pv->slot = -1;
        }
    }
}

void Engine::startPreview(int slot, int region)
{
    if (current_ == nullptr || !current_->model)
        return;
    const Model& m = *current_->model;
    const LoopSlot& s = slot == 0 ? m.loop1 : m.loop2;
    const double len = static_cast<double>(framesOf(s.data.get()));
    if (!(len > 0.0) || region >= s.regions.regionCount())
        return;
    const auto [lo01, hi01] = s.regions.bounds(region);
    if (!(hi01 > lo01))
        return;

    if (preview_.voice.state != Voice::State::Idle)
    {
        // Cancel: the running audition fades out as the tail while the new one starts at once.
        previewTail_ = preview_;
        previewTail_.voice.endedByItself = false;
        if (previewTail_.voice.state == Voice::State::Playing)
            beginFadeOut(previewTail_.voice);
    }
    Voice& v = preview_.voice;
    v.endedByItself = false;
    v.lo = lo01 * len;
    v.hi = hi01 * len;
    v.mode = PlayMode::OneShot;
    v.region2 = region;
    v.elapsed = 0.0;
    v.state = Voice::State::Playing;
    v.gain = 0.0f;
    v.fadeLeft = fadeLen_; // fade in
    v.gl.start(v.lo, 1.0, grainLen_);
    v.gr.start(v.lo, 1.0, grainLen_);
    preview_.slot = slot;
    preview_.dataId = slot == 0 ? current_->data1Id : current_->data2Id;
    if (slot == 1)
    {
        // A Loop-2 audition takes the switches of its region (also for a voice that plays meanwhile).
        maskOwner_ = MaskOwner::Preview;
        setTargetMask(m.fx.maskFor(region));
    }
}

void Engine::mixPreviewVoice(PreviewVoice& pv, const Model& m, float* l, float* r, int n)
{
    if (pv.voice.state == Voice::State::Idle)
        return;
    const LoopSlot& s = pv.slot == 0 ? m.loop1 : m.loop2;
    const StereoData* d = s.data.get();
    const size_t frames = framesOf(d);
    if (frames == 0)
    {
        pv.voice.state = Voice::State::Idle;
        pv.voice.gain = 0.0f;
        return;
    }
    // Source frames per output frame at the host tempo, like Loop 2.
    const double beats = std::max(1, s.bars) * lastBeatsPerBar_;
    const double adv = static_cast<double>(frames) * lastBpm_ / (60.0 * outRate_ * beats);
    GrainFrame gf;
    const double srcRate = std::isfinite(d->sampleRate) && d->sampleRate > 0.0 ? d->sampleRate : outRate_;
    gf.step = srcRate / outRate_;
    gf.dir = 1.0;
    gf.cycle = true;
    for (int i = 0; i < n && pv.voice.state != Voice::State::Idle; ++i)
        stepVoice(pv.voice, *d, gf, adv, l[i], r[i]);
}

void Engine::mixPreview(float* l, float* r, float* busL, float* busR, int n)
{
    if (current_ != nullptr && current_->model)
        for (PreviewVoice* pv : { &previewTail_, &preview_ })
            mixPreviewVoice(*pv, *current_->model, pv->slot == 1 ? busL : l, pv->slot == 1 ? busR : r, n);
    busFrame_ = n; // an audition that ended in this slice hands the switches back from the next one
    releasePreviewMask();
}

void Engine::setModel(std::shared_ptr<const Model> model)
{
    auto rt = std::make_unique<Runtime>();
    rt->model = std::move(model);
    std::lock_guard<std::mutex> lock(mutex_);
    // Same Loop-1 data as the latest published model: share its reader, so a seamless swap keeps
    // the grain state. prev is owned_ and published, so its data is alive (no address reuse).
    const Runtime* prev = next_.load();
    const bool comparable = prev != nullptr && prev->model && rt->model;
    const bool same1 = comparable && prev->model->loop1.data == rt->model->loop1.data;
    const bool same2 = comparable && prev->model->loop2.data == rt->model->loop2.data;
    rt->data1Id = same1 ? prev->data1Id : nextDataId_++;
    rt->data2Id = same2 ? prev->data2Id : nextDataId_++;
    if (same1)
        rt->reader1 = prev->reader1;
    else
    {
        rt->reader1 = std::make_shared<LoopReader>();
        rt->reader1->prepare(sampleRate_);
        if (rt->model)
            rt->reader1->setData(rt->model->loop1.data);
    }
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

Engine::SwapKey Engine::keyOf(const Runtime* rt)
{
    SwapKey k;
    if (rt == nullptr || !rt->model)
        return k;
    const Model& m = *rt->model;
    k.valid = true;
    k.data1Id = rt->data1Id;
    k.data2Id = rt->data2Id;
    k.bars1 = m.loop1.bars;
    k.bars2 = m.loop2.bars;
    return k;
}

void Engine::resetPlayback(bool keepLoop1)
{
    running_ = false;
    swapPending_ = false;
    lastRegion1_ = -1;
    lastLo1_ = lastHi1_ = 0.0;
    lastCycle1_ = 0;
    lastPpq_ = 0.0;
    pending_.valid = false;
    lastEntryFrame_ = -1;
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
    if (current_ != nullptr && !keepLoop1)
        current_->reader1->reset();
    // The voice is gone: its switches close (tails ring out; the chain is not flushed here).
    if (maskOwner_ == MaskOwner::Voice)
    {
        maskOwner_ = MaskOwner::None;
        setTargetMask(0);
    }
}

void Engine::adoptSwap(const Model& m, double beats1)
{
    swapPending_ = false;
    // Loop-1 region at the last rendered frame, in the new geometry. An index change caused by a
    // boundary edit is not an entry (no draw): it just becomes the current region.
    const double pos = lastPpq_ + kPpqEpsilon;
    const double cycle = std::floor(pos / beats1);
    const double ph = (pos - cycle * beats1) / beats1;
    const int region = m.loop1.regions.regionAt(ph);
    const auto [lo, hi] = m.loop1.regions.bounds(region);

    // Gate: while it runs, its trigger is lastRegion1_ (shouldGateStop is checked on every frame).
    // The trigger region still exists if the region now under the play position kept its start or
    // its end edge; then the Gate follows that region, otherwise it ends via shouldGateStop.
    if (sequencer_.gateTrigger() >= 0)
    {
        constexpr double kEdgeTol = 1e-9;
        const bool kept = std::abs(lo - lastLo1_) <= kEdgeTol || std::abs(hi - lastHi1_) <= kEdgeTol;
        sequencer_.retargetGate(kept ? region : -1);
    }
    lastRegion1_ = region;
    lastLo1_ = lo;
    lastHi1_ = hi;

    // Loop 2: the voice keeps the bounds it started with; only if its region index no longer exists
    // (the plugin's notion of a removed region, as for stale rule targets) it fades out like a Stop.
    if (voice_.state == Voice::State::Playing && voice_.region2 >= m.loop2.regions.regionCount())
    {
        requestStop();
        sequencer_.onVoiceEnded();
    }
    // A switch toggled in the editor acts on the sounding region at once.
    refreshMask(m);
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
        if (maskOwner_ == MaskOwner::Voice)
        {
            maskOwner_ = MaskOwner::None;
            setTargetMask(0);
        }
        return;
    }
    v.lo = lo01 * len;
    v.hi = hi01 * len;
    v.mode = mode;
    v.region2 = region2;
    v.elapsed = 0.0;
    v.state = Voice::State::Playing;
    v.gain = 0.0f;
    v.fadeLeft = fadeLen_; // fade in
    v.gl.start(v.lo, 1.0, grainLen_);
    v.gr.start(v.lo, 1.0, grainLen_);
    // The new voice takes the switches of its region from this frame on (the cancelled tail too).
    maskOwner_ = MaskOwner::Voice;
    setTargetMask(m.fx.maskFor(region2));
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
    // Silence or a Gate end: the inputs close, the tails ring out. A Loop-2 audition that took the
    // switches keeps them until it ends.
    if (maskOwner_ == MaskOwner::Voice)
    {
        maskOwner_ = MaskOwner::None;
        setTargetMask(0);
    }
}

bool Engine::stepVoice(Voice& v, const StereoData& d, GrainFrame gf, double adv, float& outL, float& outR) const
{
    // Gate: grains wrap inside the region (it repeats). One-Shot: grains read the whole loop, so a
    // grain overshooting the region end (host tempo below loop tempo: the region position advances
    // slower than the grains read) plays the real continuation, not the region start. The voice
    // still ends at its own region end (elapsed vs. hi - lo below).
    if (v.mode == PlayMode::Gate)
    {
        gf.lo = v.lo;
        gf.hi = v.hi;
    }
    else
    {
        gf.lo = 0.0;
        gf.hi = static_cast<double>(framesOf(&d));
    }
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

void Engine::planEarlyDraw(const Model& m, double ppq, double bpm, double beats1)
{
    const double lookBeats = teaserSec_.load(std::memory_order_relaxed) * bpm / 60.0;
    if (!(lookBeats > 0.0))
        return;
    // The next entry is the end of the region under the position (or the loop wrap).
    const double pos = ppq + kPpqEpsilon;
    const long long cycle = static_cast<long long>(std::floor(pos / beats1));
    const double ph = (pos - static_cast<double>(cycle) * beats1) / beats1;
    const int region = m.loop1.regions.regionAt(ph);
    const double hi = m.loop1.regions.bounds(region).second;
    const bool wrap = !(hi < 1.0);
    const long long nextCycle = wrap ? cycle + 1 : cycle;
    const int nextRegion = wrap ? 0 : region + 1;
    const double entryPpq = (static_cast<double>(cycle) + (wrap ? 1.0 : hi)) * beats1;
    if (entryPpq - ppq > lookBeats)
        return;
    if (pending_.valid && pending_.rt == current_ && pending_.cycle == nextCycle && pending_.region == nextRegion)
        return; // already drawn
    const Sequencer::Draw d = sequencer_.draw(nextRegion, m.rules);
    if (d.type == Sequencer::Draw::Type::None)
        return; // no rule there: the entry is not a draw
    pending_ = { true, current_, nextCycle, nextRegion, entryPpq, d };
}

void Engine::process(const TransportInfo& t, float* l, float* r, int n)
{
    if (n <= 0 || l == nullptr || r == nullptr)
        return;
    // Blocks larger than prepared are processed in consecutive slices that fit the bus.
    const int slice = busL_.empty() ? n : static_cast<int>(busL_.size());
    const double ppqStep = std::isfinite(t.bpm) ? t.bpm / (60.0 * outRate_) : 0.0;
    for (int pos = 0; pos < n; pos += slice)
    {
        TransportInfo ts = t;
        if (pos > 0)
            ts.ppq = t.ppq + pos * ppqStep;
        processSlice(ts, l + pos, r + pos, std::min(slice, n - pos));
    }
}

void Engine::processSlice(const TransportInfo& t, float* l, float* r, int n)
{
    // Loop 2 goes into the bus; without one (not prepared) straight into the output, dry.
    const bool useBus = !busL_.empty();
    float* const bl = useBus ? busL_.data() : l;
    float* const br = useBus ? busR_.data() : r;
    maskChangeCount_ = 0;
    sliceMask_ = mask_;
    busFrame_ = 0;

    const long long blockStart = frameCounter_;
    frameCounter_ += n;

    // Key of the Runtime in use, read while it is still protected (inUse_ == current_). It holds ids,
    // not data pointers: once the loop below moves inUse_ on, current_'s data may be freed and its
    // address reused by a new model.
    const SwapKey was = keyOf(current_);

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
        const SwapKey now = keyOf(rt);
        const bool loop1Same = was.valid && now.valid && was.data1Id == now.data1Id && was.bars1 == now.bars1;
        const bool loop2Same = was.valid && now.valid && was.data2Id == now.data2Id && was.bars2 == now.bars2;
        if (running_ && loop1Same && loop2Same)
            swapPending_ = true; // seamless: adopted below once the block's timing is known
        else
        {
            resetPlayback(running_ && loop1Same); // hard restart, the next valid frame is a fresh entry
            // Other loop data or bars (a preset load, a replaced file): no old tail into the new setup.
            // A model edit while stopped (same data) keeps the tails.
            if (!(loop1Same && loop2Same))
                chain_.flush();
            else if (rt != nullptr && rt->model)
                refreshMask(*rt->model); // e.g. a switch toggled during an audition while stopped
        }
    }

    // Audition: independent of the transport, so it is handled before the validity checks.
    dropStalePreviews();
    if (std::isfinite(t.bpm) && t.bpm > 0.0)
        lastBpm_ = t.bpm;
    if (t.tsNum > 0 && t.tsDen > 0)
        lastBeatsPerBar_ = beatsPerBar(t.tsNum, t.tsDen);
    if (const int req = previewReq_.exchange(-1, std::memory_order_relaxed); req >= 0)
        startPreview(req >> 16, req & 0xffff);

    const Model* m = current_ != nullptr ? current_->model.get() : nullptr;
    const double ppqStep = std::isfinite(t.bpm) ? t.bpm / (60.0 * outRate_) : 0.0;
    if (m == nullptr || !validTransport(t) || !std::isfinite(ppqStep) || !(ppqStep > 0.0))
    {
        // Nothing plays but the audition; the chain keeps running so its tails ring out.
        std::fill(l, l + n, 0.0f);
        std::fill(r, r + n, 0.0f);
        if (useBus)
        {
            std::fill(bl, bl + n, 0.0f);
            std::fill(br, br + n, 0.0f);
        }
        if (running_)
            resetPlayback();
        mixPreview(l, r, bl, br, n);
        if (m != nullptr)
            chain_.setParams(m->fx.params);
        finishSlice(l, r, n, useBus);
        publishPlayhead(-1.0f, -1.0f, -1);
        return;
    }

    const double bpb = beatsPerBar(t.tsNum, t.tsDen);
    const double beats1 = std::max(1, m->loop1.bars) * bpb;
    const double beats2 = std::max(1, m->loop2.bars) * bpb;
    if (swapPending_)
        adoptSwap(*m, beats1);

    // Loop 1: phase-locked to the host for the whole block.
    const StereoData* d1 = m->loop1.data.get();
    const double phase1 = std::fmod(t.ppq, beats1) / beats1;
    const double step1 = sourceStep(framesOf(d1), beats1, t.bpm, outRate_);
    current_->reader1->render(l, r, n, phase1, ppqStep / beats1, step1);
    if (useBus)
    {
        std::fill(bl, bl + n, 0.0f);
        std::fill(br, br + n, 0.0f);
    }

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
    gf.cycle = true; // grains wrap (Gate: inside the region; One-Shot: inside the loop, see stepVoice)

    bool entry = !running_ || std::abs(t.ppq - expectedPpq_) > kJumpToleranceBeats;
    if (entry)
        lastPpq_ = t.ppq;
    const bool jumped = running_ && entry;
    running_ = true;

    // An early draw is only good for continuous playback on the same model.
    if (entry || (pending_.valid && pending_.rt != current_))
        pending_.valid = false;
    if (!entry)
        planEarlyDraw(*m, t.ppq, t.bpm, beats1);
    // A host that loops without splitting the block at the loop end reports the wrap twice: once as
    // the extrapolated position crossing the cycle (an entry draw in the previous block) and once as
    // the jump back at the start of this block. A jump that lands on the start of the region the last
    // entry (shortly before) was for is that same entry, not a second draw.
    const long long duplicateWindow = static_cast<long long>(0.1 * outRate_);

    double lastPhase1 = phase1;
    for (int i = 0; i < n; ++i)
    {
        busFrame_ = i; // switch changes from voice starts and stops apply from this frame
        // Never step backwards inside continuous playback (host rounding at block starts).
        const double ppq = std::max(t.ppq + i * ppqStep, lastPpq_);
        lastPpq_ = ppq;
        const double pos = ppq + kPpqEpsilon;
        const long long cycle = static_cast<long long>(std::floor(pos / beats1));
        const double ph = (pos - static_cast<double>(cycle) * beats1) / beats1;
        lastPhase1 = ph;
        const int region = m->loop1.regions.regionAt(ph);
        Sequencer::Command c;
        if (entry || region != lastRegion1_ || cycle != lastCycle1_)
        {
            const bool jumpNow = jumped && i == 0;
            const long long frame = blockStart + i;
            const auto [regionLo, regionHi] = m->loop1.regions.bounds(region);
            (void) regionHi;
            const bool sameEntryAgain = jumpNow && lastEntryFrame_ >= 0 && region == lastRegion1_
                                        && frame - lastEntryFrame_ <= duplicateWindow
                                        && ph >= regionLo && (ph - regionLo) * beats1 <= kJumpToleranceBeats;
            entry = false;
            lastRegion1_ = region;
            std::tie(lastLo1_, lastHi1_) = m->loop1.regions.bounds(region);
            lastCycle1_ = cycle;
            if (!sameEntryAgain)
            {
                lastEntryFrame_ = frame;
                // The early draw for exactly this entry, if there is one; else draw now. None: region
                // without rule, not a draw.
                if (pending_.valid && pending_.rt == current_ && pending_.cycle == cycle && pending_.region == region)
                    c = sequencer_.apply(region, pending_.draw);
                else
                    c = sequencer_.onRegion1(region, m->rules);
                pending_.valid = false; // consumed, or for an entry that is not coming any more
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
            stepVoice(tail_, *d2, gf, adv2, bl[i], br[i]);
        if (voice_.state != Voice::State::Idle && stepVoice(voice_, *d2, gf, adv2, bl[i], br[i]))
            sequencer_.onVoiceEnded(); // one-shot finished by itself
    }

    expectedPpq_ = t.ppq + n * ppqStep;
    mixPreview(l, r, bl, br, n);
    chain_.setParams(m->fx.params);
    finishSlice(l, r, n, useBus);

    int teaser = kNoTeaser;
    float eta = 0.0f;
    if (pending_.valid && pending_.rt == current_)
    {
        const auto& d = pending_.draw;
        teaser = d.type == Sequencer::Draw::Type::Target ? d.region2
                 : d.type == Sequencer::Draw::Type::Silence ? kTeaserSilence
                                                           : kNoTeaser;
        eta = static_cast<float>(std::max(0.0, (pending_.entryPpq - expectedPpq_) * 60.0 / t.bpm));
    }
    const bool voiceShown = voice_.state != Voice::State::Idle && len2 > 0.0;
    publishPlayhead(static_cast<float>(std::clamp(lastPhase1, 0.0, 1.0)),
                    voiceShown ? static_cast<float>(std::clamp((voice_.lo + voice_.elapsed) / len2, 0.0, 1.0)) : -1.0f,
                    voiceShown ? voice_.region2 : -1, teaser, eta);
}

void Engine::finishSlice(float* l, float* r, int n, bool useBus)
{
    if (useBus)
        runChain(l, r, n);
    fxActive_.store(chain_.anyActive(), std::memory_order_relaxed);
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
