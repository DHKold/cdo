#ifndef CDO_BUILD_CONDITION_H
#define CDO_BUILD_CONDITION_H

#include <string>
#include <vector>

namespace cdo::build {

// Forward declaration — full definition in artifact.h
class Artifact;

/// Result of a TaskCondition evaluation: a decision (Build or Skip) with a human-readable reason.
struct ConditionResult {
    enum Decision { Build, Skip };
    Decision decision;
    std::string reason;
};

/// Abstract base class for task build conditions.
/// Evaluates whether a task needs to execute based on its inputs and primary output.
/// Designed so that alternative implementations (e.g., AlwaysRunCondition, ContentHashCondition)
/// can be added without modifying Task or Runner code.
class TaskCondition {
public:
    virtual ~TaskCondition() = default;

    /// Evaluate the condition against input artifacts and the primary output.
    /// The primary output is the main artifact used for freshness evaluation
    /// (e.g., the .o file for a compile task, not the .d file).
    virtual ConditionResult evaluate(const std::vector<const Artifact*>& inputs, const Artifact& primary_output) const = 0;
};

/// Concrete TaskCondition that implements mtime-based freshness comparison.
///
/// Logic:
///   1. If primary_output does not exist → Build("does not exist")
///   2. If forced=true and primary_output is up-to-date → Build("forced")
///   3. If any input mtime > primary_output mtime → Build("outdated")
///   4. Otherwise → Skip("up-to-date")
///
/// Uses the Artifact's mtime() method for timestamp retrieval (not raw PAL calls).
class FreshnessCondition : public TaskCondition {
public:
    explicit FreshnessCondition(bool forced = false);

    ConditionResult evaluate(const std::vector<const Artifact*>& inputs, const Artifact& primary_output) const override;

private:
    bool forced_;
};

} // namespace cdo::build

#endif // CDO_BUILD_CONDITION_H
