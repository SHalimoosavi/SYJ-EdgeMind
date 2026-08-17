#ifndef SYJ_EDGEMIND_MODEL_MODEL_RESOLVER_H
#define SYJ_EDGEMIND_MODEL_MODEL_RESOLVER_H

#include <string>

namespace syj::edgemind {

// Resolution outcome — deterministic, explicit, never a string classification.
enum class ModelResolutionStatus {
    Resolved,
    NeitherProvided,   // both model_path and model_id are empty
    BothProvided,      // both are non-empty — ambiguous, no precedence rule
                        // exists in this codebase, so this is rejected rather
                        // than silently preferring one (see model_resolver.cpp)
    ModelIdNotFound,    // model_id given, but no registry entry matches (this
                         // also covers "the registry file doesn't exist yet" —
                         // the practical caller action is identical either way:
                         // nothing has been imported under that id)
    RegistryUnreadable,  // model_id given, and the registry file exists but
                          // could not be trusted (see ModelRegistry::load's
                          // Corrupted case) — distinct from ModelIdNotFound
                          // because the caller action differs (fix/inspect the
                          // registry, vs. simply import the model first)
};

struct ModelResolutionResult {
    ModelResolutionStatus status = ModelResolutionStatus::NeitherProvided;
    std::string resolved_path; // populated only when status == Resolved
};

// v0.5.0. Resolves ONE input (a direct path, or a registry model_id) down to
// a single filesystem path — nothing more. This function's contract is
// deliberately narrow and must stay that way:
//
//   model_path + model_id + registry_path  ->  resolved path | explicit error
//
// It does NOT verify the file, compute a hash, parse GGUF, touch llama.cpp,
// perform memory admission, account for usage/quota, or write to the
// registry. Every one of those responsibilities already exists elsewhere
// (ModelVerifier, ModelRegistry::import_model, MemoryBudgetPolicy,
// UsageManager) and stays exactly where it is — this function's only job is
// to answer "given what the caller supplied, which path are we even talking
// about", so that Runtime::load() can hand that path to the SAME
// verify-then-register-then-load pipeline it already uses for the direct
// model_path case. Resolving by identity does not skip or shortcut
// verification: a resolved path is re-verified downstream exactly like any
// other path, every time.
//
// Precedence rule for "both provided": there is no existing precedence rule
// anywhere in this codebase for preferring model_path over model_id or vice
// versa, so per explicit instruction this case is rejected outright
// (ModelResolutionStatus::BothProvided) rather than silently picking one —
// an ambiguous config is a config error, not something to guess at.
//
// Read-only with respect to the registry: calls ModelRegistry::load() (an
// existing, unmodified, read-only operation) to search for `model_id`. Does
// not call ModelRegistry::import_model() or any other mutating registry
// operation — that stays downstream, in Runtime::load(), unchanged.
ModelResolutionResult resolve_model_path(const std::string& model_path, const std::string& model_id,
                                          const std::string& registry_path);

} // namespace syj::edgemind

#endif // SYJ_EDGEMIND_MODEL_MODEL_RESOLVER_H
