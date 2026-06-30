/**
 * task.h - Abstract Task base class for the build pipeline.
 *
 * Defines the Task abstract base class representing a unit of build work.
 * Each task has input Artifacts, output Artifacts, a primary output (used for
 * freshness evaluation), and a TaskCondition that determines whether execution
 * is needed.
 *
 * The run() method implements the Template Method pattern: it evaluates the
 * condition, logs the decision, and calls execute() only if building is needed.
 * Concrete task subclasses implement execute() to perform their specific build
 * operation (compile, link, archive, etc.).
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_TASK_H
#define CDO_BUILD_TASK_H

#include "build/artifact.h"
#include "build/condition.h"

#include <vector>

namespace cdo::build {

/// Abstract base class representing a unit of build work.
///
/// Concrete tasks (BuildCSource, BuildStaticLibrary, etc.) inherit from this
/// class and implement the pure virtual methods to define their inputs, outputs,
/// condition, and execution logic.
///
/// The run() method is the public entry point: it evaluates the task's condition
/// against the primary output and inputs, logs the decision, then calls execute()
/// only if the condition says Build. This ensures consistent logging and
/// skip/build behavior across all task types.
class Task {
public:
    virtual ~Task() = default;

    /// Run the task: evaluate condition against primary output, then execute if needed.
    /// This is a non-virtual template method that enforces consistent condition
    /// evaluation, logging, and execution gating for all task types.
    /// Returns 0 on success (built or skipped), non-zero on execute failure.
    int run();

    /// Returns the list of input artifacts for this task.
    /// Used by the condition to determine freshness.
    virtual const std::vector<const Artifact*>& inputs() const = 0;

    /// All output artifacts produced by this task. A compile task produces both
    /// the .o file and the .d dependency file, for example.
    virtual const std::vector<const Artifact*>& outputs() const = 0;

    /// The primary output artifact — used for freshness condition evaluation and logging.
    /// For compile tasks, this is the .o file. For link tasks, the binary/library.
    virtual const Artifact& primaryOutput() const = 0;

    /// Returns the TaskCondition used to determine whether this task needs to execute.
    virtual const TaskCondition& condition() const = 0;

    /// Returns true if the last run() call resulted in a skip (condition said up-to-date).
    bool wasSkipped() const;

    /// Returns the task ID assigned by the TasksDag. -1 if not yet assigned.
    int id() const;

    /// Sets the task ID. Called by TasksDag when the task is added.
    void setId(int id);

protected:
    /// Perform the actual build operation. Called only if condition says Build.
    /// Concrete tasks implement this to invoke the compiler, linker, archiver, etc.
    /// Returns 0 on success, non-zero on failure.
    virtual int execute() = 0;

private:
    int id_ = -1;
    bool skipped_ = false;
};

} // namespace cdo::build

#endif // CDO_BUILD_TASK_H
