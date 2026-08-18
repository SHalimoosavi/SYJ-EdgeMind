#ifndef SYJ_EDGEMIND_CORE_MODEL_SELECTION_H
#define SYJ_EDGEMIND_CORE_MODEL_SELECTION_H

#include <string>
#include <vector>

#include "model/model_types.h"

namespace syj::edgemind {

// Phase 4. The CLI-facing decision of what to do when neither --model nor
// --model-id was supplied: read what's already registered and decide
// between "nothing to select", "exactly one, safe to auto-select", or
// "more than one, require the user to choose explicitly".
//
// Lives in src/core/ (not src/cli/) despite being motivated entirely by
// the CLI's UX: src/cli/main.cpp is documented to include ONLY
// api/edge_mind_api.h, never an internal C++ header directly (see
// main.cpp's own top-of-file comment — "platform code doesn't know
// llama.cpp internals... the Windows platform layer will wrap this same C
// API, not a different one"). This function is called from
// src/api/edge_mind_api.cpp (which, like Runtime, is free to include
// internal headers) and exposed to main.cpp only through a new C API
// function (syj_edgemind_select_model) — the same boundary every other
// CLI-visible piece of state already crosses.
enum class CliModelSelectionOutcome {
    NoRegisteredModels,
    SingleModelSelected,
    MultipleModelsRequireChoice,
};

struct CliModelSelectionResult {
    CliModelSelectionOutcome outcome = CliModelSelectionOutcome::NoRegisteredModels;

    // Populated only when outcome == SingleModelSelected. This is the
    // registry entry that was auto-selected — the caller (main.cpp) is
    // expected to feed entry.model_id into RuntimeConfig::model_id exactly
    // as if the user had typed --model-id themselves, so the selection
    // enters the SAME resolution -> verification -> admission -> load
    // pipeline as any other model_id, never a shortcut around it.
    RegistryEntry selected_entry;

    // Populated whenever the registry could be read (regardless of
    // outcome) — the CLI's MultipleModelsRequireChoice message reuses this
    // rather than re-querying the registry a second time, but it's exposed
    // unconditionally in case a caller wants it for NoRegisteredModels too
    // (always empty there) or SingleModelSelected (a redundant single-entry
    // list, harmless to expose).
    std::vector<RegistryEntry> all_entries;
};

// Reads the registry at `registry_path` via the EXISTING, UNMODIFIED
// ModelRegistry::load() (read-only — this function does not import,
// verify, hash, or write anything) and decides which of the three
// CLI-facing outcomes applies. A corrupted or not-yet-existing registry
// both collapse to NoRegisteredModels here — from the CLI's perspective,
// "nothing usable is registered" is the same actionable guidance either
// way ("import a model with --model <path>"); a caller that needs to
// distinguish corruption from a genuinely empty registry for some other
// reason should call ModelRegistry::load() directly instead.
//
// This function does NOT verify, admit, or load anything — it is a pure
// selection decision over already-persisted registry entries, mirroring
// resolve_model_path()'s (src/model/model_resolver.h) non-duplication
// posture: registry parsing/persistence logic is never reimplemented here,
// only consumed.
CliModelSelectionResult select_model_for_cli(const std::string& registry_path);

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_CORE_MODEL_SELECTION_H
