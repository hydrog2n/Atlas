#include "atlas/persistence/package.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace atlas::persistence {

namespace {

using json = nlohmann::ordered_json;

constexpr const char* formatIdentifier = "atlas.project";
constexpr int schemaVersion = 1;
// Keep individual JSON records bounded before parsing them into memory.
constexpr std::uintmax_t maximumJsonBytes = 8 * 1024 * 1024;
constexpr std::size_t maximumJsonDepth = 64;
constexpr std::size_t maximumJsonNodes = 100000;

std::string contentHash(const std::string& content) {
    // The manifest records the algorithm name so future hashing policies can change explicitly.
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < content.size(); ++index) {
        if (content[index] == '\r' && index + 1 < content.size() && content[index + 1] == '\n') {
            continue;
        }
        const unsigned char byte = static_cast<unsigned char>(content[index]);
        hash ^= byte;
        hash *= 1099511628211ULL;
    }

    std::ostringstream output;
    output << "fnv1a-64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

void writeJson(const std::filesystem::path& path, const json& value) {
    // Serialize one canonical JSON record and verify that the stream flushes.
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to write package file: " + path.string());
    }
    output << value.dump(2) << '\n';
    output.flush();
    if (!output) {
        throw std::runtime_error("Unable to flush package file: " + path.string());
    }
}

void validateJsonLimits(const json& value) {
    // Walk parsed JSON recursively to bound memory-amplifying input shapes.
    std::size_t nodeCount = 0;
    std::function<void(const json&, std::size_t)> visit = [&](const json& node, std::size_t depth) {
        if (++nodeCount > maximumJsonNodes || depth > maximumJsonDepth) {
            throw std::invalid_argument("Package JSON exceeds nesting or node limits.");
        }
        if (node.is_array()) {
            for (const auto& child : node) visit(child, depth + 1);
        } else if (node.is_object()) {
            for (const auto& [key, child] : node.items()) {
                static_cast<void>(key);
                visit(child, depth + 1);
            }
        }
    };
    visit(value, 0);
}

json readJson(const std::filesystem::path& path) {
    // Read a bounded JSON file and convert parser failures into package errors.
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError || size > maximumJsonBytes) {
        throw std::invalid_argument("Package JSON file exceeds the supported size limit: " + path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::invalid_argument("Unable to read package file: " + path.string());
    }
    try {
        const auto parsed = json::parse(input);
        validateJsonLimits(parsed);
        return parsed;
    } catch (const json::parse_error& error) {
        throw std::invalid_argument("Invalid JSON in package file: " + path.string() + " (" + error.what() + ")");
    }
}

void rejectUnsafePackagePath(const std::filesystem::path& packagePath) {
    // Package operations must never resolve a caller-supplied path through a parent traversal.
    if (packagePath.empty() || packagePath.has_filename() == false) {
        throw std::invalid_argument("Package path must name a directory package.");
    }
    for (const auto& component : packagePath) {
        if (component == "..") {
            throw std::invalid_argument("Package path must not contain traversal components.");
        }
    }
}

void rejectUnsafeComponent(const std::string& component, const char* label) {
    // Map IDs become directory names, so they must remain single safe path components.
    if (component.empty() || component == "." || component == ".." ||
        component.find_first_of("/\\") != std::string::npos) {
        throw std::invalid_argument(std::string("Invalid ") + label + " path component.");
    }
}

json makeManifest(
    const domain::Project& project,
    const std::map<std::string, std::string>& authoritativeHashes) {
    // The manifest describes the logical package and the authoritative files it protects.
    return json{
        {"format", formatIdentifier},
        {"schemaVersion", schemaVersion},
        {"projectId", project.id()},
        {"units", "m"},
        {"requiredFeatures", json::array()},
        {"rootMapIds", json::array({project.rootMap().id})},
        {"authoritativeFiles", authoritativeHashes},
        {"referenceDescriptors", json::array()},
        {"reverseReferences", json::object()},
        {"generatorVersions", json{{"atlas", "0.4.0"}}}
    };
}

void validateManifest(
    const json& manifest,
    const domain::Project& project,
    bool allowNewerSchema = false) {
    if (manifest.value("format", "") != formatIdentifier) {
        throw std::invalid_argument("Unsupported package format.");
    }
    const auto packageSchema = manifest.value("schemaVersion", 0);
    if ((!allowNewerSchema && packageSchema != schemaVersion) || packageSchema < 1) {
        throw std::invalid_argument("Unsupported package schema version.");
    }
    if (manifest.value("projectId", "") != project.id()) {
        throw std::invalid_argument("Package manifest project ID does not match project data.");
    }
    if (!manifest.contains("authoritativeFiles") || !manifest["authoritativeFiles"].is_object()) {
        throw std::invalid_argument("Package manifest authoritative file hashes are required.");
    }
}

std::string readFileText(const std::filesystem::path& path) {
    // Read raw authoritative bytes so their manifest hash can be checked exactly.
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::invalid_argument("Unable to read package file: " + path.string());
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void validateProjectHash(const std::filesystem::path& packagePath, const json& manifest) {
    // Hash validation detects edits or corruption that leave JSON syntactically valid.
    for (const auto& [relativePath, expectedValue] : manifest["authoritativeFiles"].items()) {
        const std::filesystem::path relative(relativePath);
        rejectUnsafePackagePath(relative);
        const auto actual = contentHash(readFileText(packagePath / relative));
        if (expectedValue.get<std::string>() != actual) {
            throw std::invalid_argument("Authoritative content hash does not match: " + relativePath);
        }
    }
}

void rotateCheckpoint(const std::filesystem::path& packagePath, std::size_t count) {
    // Checkpoints copy the last known-good package before a new save replaces it.
    if (count == 0 || !std::filesystem::is_directory(packagePath)) {
        return;
    }

    const auto parent = packagePath.parent_path();
    const auto base = packagePath.filename().string() + ".checkpoint-";
    for (std::size_t index = count; index > 0; --index) {
        const auto current = parent / (base + std::to_string(index));
        const auto previous = parent / (base + std::to_string(index - 1));
        std::error_code error;
        if (index == count) {
            std::filesystem::remove_all(current, error);
        } else if (std::filesystem::exists(previous)) {
            std::filesystem::remove_all(current, error);
            std::filesystem::rename(previous, current, error);
        }
        if (error) {
            throw std::runtime_error("Unable to rotate package checkpoints.");
        }
    }

    std::error_code error;
    std::filesystem::copy(packagePath, parent / (base + "0"),
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        throw std::runtime_error("Unable to create package checkpoint.");
    }
}

} // namespace

Package Package::fromProject(const domain::Project& project) {
    // Wrap the domain snapshot without giving persistence ownership of mutation.
    return Package(project);
}

Package Package::load(const std::filesystem::path& packagePath, LoadOptions options) {
    // Load validates the package before exposing its project to callers.
    rejectUnsafePackagePath(packagePath);
    // A package must be an existing directory before any record is read.
    if (!std::filesystem::is_directory(packagePath)) {
        throw std::invalid_argument("Package path is not a directory: " + packagePath.string());
    }

    // Read the manifest and source independently so identity and content hashes can be checked.
    const auto manifest = readJson(packagePath / "manifest.json");
    const auto project = domain::Project::fromJson(readJson(packagePath / "project.json").dump());
    const auto packageSchema = manifest.value("schemaVersion", 0);
    const bool newerSchema = packageSchema > schemaVersion;
    // Newer schemas may be inspected only when the caller explicitly allows read-only mode.
    if (newerSchema && !options.allowNewerSchemaReadOnly) {
        throw std::invalid_argument("Package uses a newer unsupported schema version.");
    }
    validateManifest(manifest, project, options.allowNewerSchemaReadOnly);
    validateProjectHash(packagePath, manifest);
    return Package(project, newerSchema);
}

MigrationReport Package::migrate(
    const std::filesystem::path& packagePath,
    int targetSchema,
    bool dryRun) {
    // Migration reports unsupported paths without mutating the source package.
    rejectUnsafePackagePath(packagePath);
    const auto manifest = readJson(packagePath / "manifest.json");
    const int sourceSchema = manifest.value("schemaVersion", 0);
    // Reject migration requests that have no supported version direction.
    if (targetSchema < sourceSchema || targetSchema > schemaVersion || sourceSchema < 0) {
        return MigrationReport{sourceSchema, targetSchema, false, dryRun, false,
            "Unsupported migration target schema.", {}};
    }

    // Keep migration decisions explicit and reportable even when the current schema needs no step.
    MigrationReport report{sourceSchema, targetSchema, sourceSchema != targetSchema, dryRun, true, "", {}};
    // A current-schema package needs no transformation.
    if (sourceSchema == targetSchema) {
        report.changes.push_back("Package already uses the requested schema.");
        return report;
    }
    // v0.3 currently supports only the explicit schema 0 to schema 1 path.
    if (sourceSchema != 0 || targetSchema != schemaVersion) {
        return MigrationReport{sourceSchema, targetSchema, false, dryRun, false,
            "No migration path exists for the requested schemas.", {}};
    }
    // Dry-run reports the planned transformation without writing files.
    if (dryRun) {
        report.changes.push_back("Schema 0 project records will be rewritten as schema 1.");
        return report;
    }

    const auto project = domain::Project::fromJson(readJson(packagePath / "project.json").dump());
    try {
        Package(project).save(packagePath);
    } catch (const std::exception& error) {
        return MigrationReport{sourceSchema, targetSchema, true, dryRun, false,
            error.what(), {}};
    }
    report.changes.push_back("Rewrote the schema 0 package through the schema 1 writer.");
    return report;
}

Package Package::recover(const std::filesystem::path& packagePath) {
    // Recovery prefers the newest retained checkpoint and never reads derived cache data.
    rejectUnsafePackagePath(packagePath);
    const auto checkpoint = packagePath.parent_path() /
        (packagePath.filename().string() + ".checkpoint-0");
    // Prefer the latest checkpoint, falling back to the current package for first saves.
    return load(std::filesystem::is_directory(checkpoint) ? checkpoint : packagePath);
}

void Package::save(const std::filesystem::path& packagePath, SaveOptions options) const {
    // Read-only newer-schema packages cannot be rewritten by an older application.
    // Newer-schema packages are protected from accidental downgrade writes.
    if (readOnly_) {
        throw std::runtime_error("Newer-schema packages are read-only.");
    }
    rejectUnsafePackagePath(packagePath);

    const auto parent = packagePath.parent_path().empty()
        ? std::filesystem::current_path()
        : packagePath.parent_path();
    std::filesystem::create_directories(parent);

    const auto stagingPath = parent / (packagePath.filename().string() + ".staging");
    std::error_code error;
    std::filesystem::remove_all(stagingPath, error);
    // Remove an abandoned staging tree before starting a new write.
    if (error) {
        throw std::runtime_error("Unable to clear package staging path.");
    }
    std::filesystem::create_directories(stagingPath);

    // The staging tree is validated completely before the previous package is replaced.
    try {
        rejectUnsafeComponent(project_.rootMap().id, "root map");
        std::filesystem::create_directories(stagingPath / "maps" / project_.rootMap().id);
        std::filesystem::create_directories(stagingPath / "prefabs");
        std::filesystem::create_directories(stagingPath / "assets");
        std::filesystem::create_directories(stagingPath / "exports");
        std::filesystem::create_directories(stagingPath / "cache");
        const auto projectJson = project_.normalizedJson();
        json mapJson{
            {"id", project_.rootMap().id},
            {"type", "core.Map"},
            {"parentMapId", nullptr},
            {"defaultDisplayLayerId", project_.rootMap().defaultDisplayLayerId},
            {"defaultSpatialLevelId", project_.rootMap().defaultSpatialLevelId},
            {"displayLayers", json::array()},
            {"spatialLevels", json::array()}};
        for (const auto& layer : project_.rootMap().displayLayers) {
            mapJson["displayLayers"].push_back({
                {"id", layer.id}, {"name", layer.name}, {"visible", layer.visible},
                {"locked", layer.locked}, {"opacity", layer.opacity}});
        }
        for (const auto& level : project_.rootMap().spatialLevels) {
            mapJson["spatialLevels"].push_back({{"id", level.id}, {"name", level.name}});
        }
        const auto mapText = mapJson.dump(2) + "\n";
        json objectsJson = json::array();
        for (const auto& object : project_.rootMap().objects) {
            objectsJson.push_back(object.normalizedJson());
        }
        const auto objectsText = objectsJson.dump(2) + "\n";
        const json networkJson = json::array();
        const auto networkText = networkJson.dump(2) + "\n";
        writeJson(stagingPath / "project.json", json::parse(projectJson));
        writeJson(
            stagingPath / "maps" / project_.rootMap().id / "map.json",
            mapJson);
        writeJson(
            stagingPath / "maps" / project_.rootMap().id / "objects.json",
            objectsJson);
        writeJson(
            stagingPath / "maps" / project_.rootMap().id / "network.json",
            networkJson);
        const std::map<std::string, std::string> authoritativeHashes{
            {"project.json", contentHash(projectJson)},
            {"maps/" + project_.rootMap().id + "/map.json", contentHash(mapText)},
            {"maps/" + project_.rootMap().id + "/objects.json", contentHash(objectsText)},
            {"maps/" + project_.rootMap().id + "/network.json", contentHash(networkText)}};
        writeJson(stagingPath / "manifest.json", makeManifest(project_, authoritativeHashes));
        validateManifest(readJson(stagingPath / "manifest.json"), project_);
        validateProjectHash(stagingPath, readJson(stagingPath / "manifest.json"));
        // Failure points exercise the rollback boundary without mutating the live package.
        if (options.failurePoint == SaveOptions::FailurePoint::afterStaging ||
            options.failurePoint == SaveOptions::FailurePoint::afterValidation ||
            options.failurePoint == SaveOptions::FailurePoint::diskFull ||
            options.failurePoint == SaveOptions::FailurePoint::permissionDenied ||
            options.failurePoint == SaveOptions::FailurePoint::forcedTermination ||
            options.failurePoint == SaveOptions::FailurePoint::journalTruncated) {
            throw std::runtime_error("Injected save interruption.");
        }

        // Preserve the previous valid package before installing the staged replacement.
        rotateCheckpoint(packagePath, options.checkpointCount);
        if (options.failurePoint == SaveOptions::FailurePoint::afterCheckpoint) {
            throw std::runtime_error("Injected save interruption.");
        }

        const auto backupPath = parent / (packagePath.filename().string() + ".backup");
        std::filesystem::remove_all(backupPath, error);
        // Move the old package aside so installation can be rolled back if rename fails.
        if (std::filesystem::exists(packagePath)) {
            std::filesystem::rename(packagePath, backupPath, error);
            if (error) {
                throw std::runtime_error("Unable to stage the existing package for replacement.");
            }
        }
        if (options.failurePoint == SaveOptions::FailurePoint::afterBackup) {
            throw std::runtime_error("Injected save interruption.");
        }
        // Rename the complete staging tree only after its manifest and hashes validate.
        std::filesystem::rename(stagingPath, packagePath, error);
        // Install only the fully validated staging tree.
        if (error) {
            std::error_code restoreError;
            if (std::filesystem::exists(backupPath)) {
                std::filesystem::rename(backupPath, packagePath, restoreError);
            }
            throw std::runtime_error("Unable to install the staged package.");
        }
        std::filesystem::remove_all(backupPath, error);
    } catch (...) {
        // Restore the old package and remove partial staging after any failure.
        if (!std::filesystem::exists(packagePath)) {
            const auto backupPath = parent / (packagePath.filename().string() + ".backup");
            std::error_code restoreError;
            if (std::filesystem::exists(backupPath)) {
                std::filesystem::rename(backupPath, packagePath, restoreError);
            }
        }
        std::filesystem::remove_all(stagingPath, error);
        throw;
    }
}

const domain::Project& Package::project() const noexcept {
    // Expose the loaded authoritative snapshot without transferring ownership.
    return project_;
}

std::string Package::manifestJson() const {
    // Reconstruct the deterministic manifest representation for inspection tools.
    json mapJson{
        {"id", project_.rootMap().id},
        {"type", "core.Map"},
        {"parentMapId", nullptr},
        {"defaultDisplayLayerId", project_.rootMap().defaultDisplayLayerId},
        {"defaultSpatialLevelId", project_.rootMap().defaultSpatialLevelId},
        {"displayLayers", json::array()},
        {"spatialLevels", json::array()}};
    for (const auto& layer : project_.rootMap().displayLayers) {
        mapJson["displayLayers"].push_back({
            {"id", layer.id}, {"name", layer.name}, {"visible", layer.visible},
            {"locked", layer.locked}, {"opacity", layer.opacity}});
    }
    for (const auto& level : project_.rootMap().spatialLevels) {
        mapJson["spatialLevels"].push_back({{"id", level.id}, {"name", level.name}});
    }
    json objectsJson = json::array();
    for (const auto& object : project_.rootMap().objects) {
        objectsJson.push_back(object.normalizedJson());
    }
    const std::map<std::string, std::string> authoritativeHashes{
        {"project.json", contentHash(project_.normalizedJson())},
        {"maps/" + project_.rootMap().id + "/map.json", contentHash(mapJson.dump(2) + "\n")},
        {"maps/" + project_.rootMap().id + "/objects.json", contentHash(objectsJson.dump(2) + "\n")},
        {"maps/" + project_.rootMap().id + "/network.json", contentHash("[]\n")}};
    return makeManifest(project_, authoritativeHashes).dump(2) + "\n";
}

Package::Package(domain::Project project, bool readOnly)
    : project_(std::move(project)), readOnly_(readOnly) {}

std::string MigrationReport::toJson() const {
    // Expose migration results in a stable machine-readable format.
    json output{
        {"fromSchema", fromSchema},
        {"toSchema", toSchema},
        {"changed", changed},
        {"dryRun", dryRun},
        {"succeeded", succeeded},
        {"error", error},
        {"changes", changes}
    };
    return output.dump(2) + "\n";
}

} // namespace atlas::persistence