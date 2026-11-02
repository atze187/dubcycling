#pragma once
#include <optional>
#include <juce_core/juce_core.h>
#include "plugin/PluginState.h"

namespace dc {

// Finds the file of a stored loop reference. Search order:
//  1. ref.path, if the file exists (accepted without a hash check: an in-place re-export
//     of the same file keeps working),
//  2. sessionDir (skipped if empty/not a directory) for ref.fileName with a matching hash,
//  3. the folder of ref.path for ref.fileName with a matching hash.
// A same-named file with a different hash is never accepted in steps 2/3. Nothing found gives
// nullopt; the caller keeps regions and rules as they are.
std::optional<juce::File> relink(const LoopRef& ref, const juce::File& sessionDir);

} // namespace dc
