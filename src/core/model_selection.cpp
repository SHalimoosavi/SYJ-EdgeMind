#include "core/model_selection.h"

#include "model/model_registry.h"

namespace syj::edgemind {

CliModelSelectionResult select_model_for_cli(const std::string& registry_path) {
    CliModelSelectionResult result;

    std::vector<RegistryEntry> entries;
    const RegistryLoadResult load_result = ModelRegistry::load(registry_path, &entries);

    // NotFound (no registry file yet) and Corrupted both collapse to
    // NoRegisteredModels here — see this function's header comment for why
    // that's the right granularity for a CLI selection decision
    // specifically, as opposed to ModelRegistry::load()'s own richer
    // return value, which remains available to any caller that needs it.
    if (load_result != RegistryLoadResult::Ok || entries.empty()) {
        result.outcome = CliModelSelectionOutcome::NoRegisteredModels;
        return result;
    }

    result.all_entries = entries;

    if (entries.size() == 1) {
        result.outcome = CliModelSelectionOutcome::SingleModelSelected;
        result.selected_entry = entries.front();
        return result;
    }

    result.outcome = CliModelSelectionOutcome::MultipleModelsRequireChoice;
    return result;
}

} // namespace syj::edgemind
