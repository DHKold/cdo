// artifact.cpp — FileArtifact implementation.
// Uses PAL layer for filesystem operations (exists, mtime).

#include "build/artifact.h"
#include "pal/pal.h"

namespace cdo::build {

FileArtifact::FileArtifact(std::string path, ArtifactType type)
    : path_(std::move(path)), type_(type) {}

bool FileArtifact::exists() const {
    if (path_.empty()) return false;
    return pal_path_exists(path_.c_str()) == PAL_OK;
}

uint64_t FileArtifact::mtime() const {
    if (path_.empty()) return 0;
    uint64_t mtime_ns = 0;
    int rc = pal_file_mtime(path_.c_str(), &mtime_ns);
    if (rc != PAL_OK) return 0;
    return mtime_ns;
}

const std::string& FileArtifact::path() const {
    return path_;
}

ArtifactType FileArtifact::type() const {
    return type_;
}

} // namespace cdo::build
