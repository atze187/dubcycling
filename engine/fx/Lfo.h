#pragma once

namespace dc::fx {

// LFO shapes; every shape has its maximum (+1) at phase 0, so all of them peak on the beat grid.
enum class LfoShape { Sine = 0, Triangle = 1, SawDown = 2, Square = 3 };

// Value of a shape at phase in [0, 1) (other values are wrapped, non-finite counts as 0); result in [-1, 1].
// Sine cos(2 pi p), Triangle 1 - 4 min(p, 1 - p), SawDown 1 - 2p, Square +1 for p < 0.5 else -1.
float lfoValue(LfoShape shape, double phase);

// Fractional part of beats / cycleBeats, in [0, 1), also for negative beats. Non-finite input or cycleBeats <= 0
// gives 0.
double lfoPhase(double beats, double cycleBeats);

} // namespace dc::fx
