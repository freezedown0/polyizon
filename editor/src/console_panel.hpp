#pragma once

#include <QWidget>

#include <cstddef>

class QPlainTextEdit;

// Bottom dock panel (tabbed with ContentBrowserPanel): a read-only view of
// polyizon::Log (core/include/polyizon/log.hpp) — Vulkan validation
// messages and caught Lua script errors (see
// EditorViewportRenderer::RenderFrame's try/catch around
// ScriptEngine::Update) both land here instead of only going to stderr,
// which nothing alt-tabs to see while the editor has focus.
//
// Polled from MainWindow's existing render QTimer tick rather than pushed
// via a signal/callback from Log itself — Log is a plain static sink with
// no observer mechanism (deliberately kept minimal, see its doc comment),
// and polling once per ~16ms frame is cheap enough not to justify adding
// one.
class ConsolePanel : public QWidget {
    Q_OBJECT

public:
    explicit ConsolePanel(QWidget* parent = nullptr);

    // Appends any Log entries added since the last call. Safe to call every
    // frame — a no-op when nothing new has been logged.
    void Poll();

private:
    QPlainTextEdit* m_TextEdit = nullptr;
    std::size_t m_LastSeenCount = 0;
};
