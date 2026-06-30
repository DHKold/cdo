// condition.cpp — FreshnessCondition implementation.
// Implements mtime-based freshness comparison using the Artifact interface.

#include "build/condition.h"
#include "build/artifact.h"

namespace cdo::build {

FreshnessCondition::FreshnessCondition(bool forced)
    : forced_(forced) {}

ConditionResult FreshnessCondition::evaluate(const std::vector<const Artifact*>& inputs, const Artifact& primary_output) const {
    // 1. If output does not exist → Build
    if (!primary_output.exists()) {
        return { ConditionResult::Build, "does not exist" };
    }

    // 2. If forced=true and output exists → Build
    if (forced_) {
        return { ConditionResult::Build, "forced" };
    }

    // 3. If any input mtime > output mtime → Build("outdated")
    uint64_t output_mtime = primary_output.mtime();
    for (const Artifact* input : inputs) {
        if (input->mtime() > output_mtime) {
            return { ConditionResult::Build, "outdated" };
        }
    }

    // 4. Otherwise → Skip("up-to-date")
    return { ConditionResult::Skip, "up-to-date" };
}

} // namespace cdo::build
