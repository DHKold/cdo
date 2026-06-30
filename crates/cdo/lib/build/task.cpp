// task.cpp — Task::run() base logic implementation.
// Implements the Template Method pattern: evaluate condition, log, execute if needed.

#include "build/task.h"
#include "core/log.h"

namespace cdo::build {

int Task::run() {
    ConditionResult result = condition().evaluate(inputs(), primaryOutput());

    if (result.decision == ConditionResult::Build) {
        cdo_log_info("Building: %s (%s)", primaryOutput().path().c_str(), result.reason.c_str());
        return execute();
    }

    // Skip
    cdo_log_debug("Up-to-date: %s", primaryOutput().path().c_str());
    skipped_ = true;
    return 0;
}

bool Task::wasSkipped() const {
    return skipped_;
}

int Task::id() const {
    return id_;
}

void Task::setId(int id) {
    id_ = id;
}

} // namespace cdo::build
