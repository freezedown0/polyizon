#include "polyizon/core.hpp"

int main() {
    polyizon::ApplicationSpec spec;
    spec.name = "Polyizon";
    spec.windowWidth = 1280;
    spec.windowHeight = 720;

    polyizon::Application app(spec);
    app.Run();

    return 0;
}
