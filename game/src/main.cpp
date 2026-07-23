#include "polyizon/core.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace {

// Resolved relative to the running executable's own directory, matching
// every other asset-loading class's identical private helper (Image,
// GraphicsPipeline, ScriptEngine, etc.) — duplicated here rather than shared
// for the same reason they duplicate it from each other.
std::filesystem::path GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer).parent_path();
}

} // namespace

// A "Build Game" export (see the editor's game_builder.hpp) drops this
// exe's own project.json right next to it, so a double-click with no
// arguments finds it automatically. An explicit first argument overrides
// that — mainly useful for testing a project without exporting it first.
// Neither present: falls back to the original hardcoded quad/instancing/sky
// demo (see Application's project mode) — unchanged standalone behavior.
int main(int argc, char** argv) {
    polyizon::ApplicationSpec spec;
    spec.name = "Polyizon";
    spec.windowWidth = 1280;
    spec.windowHeight = 720;

    if (argc >= 2) {
        spec.projectDir = std::filesystem::path(argv[1]);
    } else {
        const std::filesystem::path exeDir = GetExecutableDirectory();
        if (!exeDir.empty() && std::filesystem::exists(exeDir / "project.json")) {
            spec.projectDir = exeDir;
        }
    }

    try {
        polyizon::Application app(spec);
        app.Run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }

    return 0;
}
