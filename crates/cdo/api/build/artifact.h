/**
 * artifact.h - Build artifact abstraction layer.
 *
 * Defines the Artifact abstract base class and FileArtifact concrete implementation.
 * Artifacts represent build inputs and outputs with typed access to existence,
 * modification time, and path. The class hierarchy is based on access method
 * (FileArtifact, HttpArtifact, etc.), not on file type, so that future artifact
 * sources can be added as new subclasses.
 *
 * Part of the cdo::build pipeline refactor.
 */
#ifndef CDO_BUILD_ARTIFACT_H
#define CDO_BUILD_ARTIFACT_H

#include <cstdint>
#include <string>

namespace cdo::build {

/// Classification of artifact content type for metadata purposes.
/// Does not affect class hierarchy or behavior — purely informational.
enum class ArtifactType {
    Source,
    Object,
    StaticLibrary,
    Executable,
    SharedLibrary,
    ShaderOutput,
    DepFile,
    Header,
};

/// Abstract base class representing a build input or output.
/// Subclassed by access method: FileArtifact for local files, HttpArtifact for remote, etc.
class Artifact {
public:
    virtual ~Artifact() = default;

    /// Returns true if the artifact exists at its location.
    virtual bool exists() const = 0;

    /// Returns the modification time in nanoseconds since epoch.
    /// Returns 0 if the artifact does not exist or mtime cannot be determined.
    virtual uint64_t mtime() const = 0;

    /// Returns the path (or URI) identifying this artifact.
    virtual const std::string& path() const = 0;
};

/// Concrete Artifact representing a local file on disk.
/// Provides existence check and mtime retrieval via the PAL layer.
/// Carries an optional ArtifactType property for metadata purposes.
class FileArtifact : public Artifact {
public:
    /// Construct a FileArtifact with the given path and optional type classification.
    explicit FileArtifact(std::string path, ArtifactType type = ArtifactType::Source);

    bool exists() const override;
    uint64_t mtime() const override;
    const std::string& path() const override;

    /// Returns the artifact type classification.
    ArtifactType type() const;

private:
    std::string path_;
    ArtifactType type_;
};

} // namespace cdo::build

#endif // CDO_BUILD_ARTIFACT_H
