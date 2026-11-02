#pragma once
#include <optional>
#include <vector>
#include <juce_core/juce_core.h>
#include "plugin/PluginState.h"

namespace dc {

// Finds the file of a stored loop reference. Search order:
//  1. ref.path, if the file exists (accepted without a hash check: an in-place re-export
//     of the same file keeps working),
//  2. each folder of `candidateDirs` in order (empty entries and non-folders are skipped) for
//     ref.fileName with a matching hash.
// A same-named file with a different hash is never accepted in step 2. Nothing found gives
// nullopt; the caller keeps regions and rules as they are. The caller chooses the folders (the
// processor: folder of the other slot's file, folder of the last loaded/relinked file, session dir).
std::optional<juce::File> relink(const LoopRef& ref, const std::vector<juce::File>& candidateDirs);

} // namespace dc
