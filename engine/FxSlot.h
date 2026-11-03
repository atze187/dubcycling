#pragma once

namespace dc {

// Hook for a per-region effect. Unused in this plan (no effects yet); the Engine does not call it.
struct FxSlot
{
    virtual void process(float* l, float* r, int n) { (void)l, (void)r, (void)n; }
    virtual ~FxSlot() = default;
};

} // namespace dc
