#include "main_window.hpp"
#include "editor_theme.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QPainter>

namespace {

QIcon LoadTightlyFittedIcon(const QString& path) {
    const QImage source(path);
    if (source.isNull()) {
        return {};
    }

    QRect contentBounds;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            if (source.pixelColor(x, y).alpha() > 8) {
                contentBounds = contentBounds.united(QRect(x, y, 1, 1));
            }
        }
    }
    if (contentBounds.isEmpty()) {
        return QIcon(QPixmap::fromImage(source));
    }

    constexpr int kMasterSize = 512;
    constexpr int kPadding = 44;
    const QSize fittedSize = contentBounds.size().scaled(
        kMasterSize - kPadding * 2, kMasterSize - kPadding * 2, Qt::KeepAspectRatio);
    const QImage cropped = source.copy(contentBounds).scaled(
        fittedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage canvas(kMasterSize, kMasterSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.drawImage((kMasterSize - cropped.width()) / 2,
        (kMasterSize - cropped.height()) / 2, cropped);
    return QIcon(QPixmap::fromImage(canvas));
}

} // namespace

// Qt owns the editor's window/event loop entirely — no polyizon::Application/
// Window/GLFW involved on this path at all (see VulkanViewportWindow /
// EditorViewportRenderer). The game client (game/src/main.cpp) is untouched
// and still uses polyizon::Application as before.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Polyizon Editor");
    app.setOrganizationName("Polyizon");
    const QString brandMarkPath = QDir(QCoreApplication::applicationDirPath())
                                      .filePath("branding/PolyizonMark.png");
    if (QIcon brandIcon = LoadTightlyFittedIcon(brandMarkPath); !brandIcon.isNull()) {
        app.setWindowIcon(brandIcon);
    }
    ApplyEditorTheme(app);

    MainWindow window;
    window.show();

    return app.exec();
}
