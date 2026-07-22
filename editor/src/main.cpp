#include "main_window.hpp"

#include <QApplication>

// Qt owns the editor's window/event loop entirely — no polyizon::Application/
// Window/GLFW involved on this path at all (see VulkanViewportWindow /
// EditorViewportRenderer). The game client (game/src/main.cpp) is untouched
// and still uses polyizon::Application as before.
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    return app.exec();
}
