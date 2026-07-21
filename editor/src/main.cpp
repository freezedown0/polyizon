#include "polyizon/core.hpp"

// Placeholder entry point. The editor UI (ImGui panels, viewport, inspector)
// is not implemented yet — this just proves the target links against
// polyizon_core and drives the same window lifecycle as the game target.
int main() {
    polyizon::ApplicationSpec spec;
    spec.name = "Polyizon Editor";
    spec.windowWidth = 1600;
    spec.windowHeight = 900;

    polyizon::Application app(spec);
    app.Run();

    return 0;
}
