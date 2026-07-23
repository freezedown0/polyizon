#include "polyizon/project_manifest.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace polyizon {

ProjectManifest ReadProjectManifest(const std::filesystem::path& projectRootDir) {
    const std::filesystem::path manifestPath = projectRootDir / "project.json";
    std::ifstream file(manifestPath);
    if (!file) {
        throw std::runtime_error("Failed to open project manifest: " + manifestPath.string());
    }

    nlohmann::json manifest;
    file >> manifest;

    ProjectManifest result;
    result.name = manifest.at("name").get<std::string>();
    result.defaultScene = manifest.at("defaultScene").get<std::string>();
    return result;
}

} // namespace polyizon
