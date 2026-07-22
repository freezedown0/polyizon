#pragma once

#include <QWidget>

#include <filesystem>

class QFileSystemModel;
class QModelIndex;
class QTreeView;

// Bottom dock panel: browses the current project's root folder (Scenes/,
// Scripts/, Assets/, project.json). Double-clicking a .json file loads it
// as the active scene — the only interactive action this panel has this
// phase; everything else (Scripts/, Assets/) is browsable but otherwise
// inert, matching Phase 16's "Assets/ is scaffolded but otherwise inert"
// scope decision (no preview, no drag-and-drop, no import UI).
class ContentBrowserPanel : public QWidget {
    Q_OBJECT

public:
    explicit ContentBrowserPanel(QWidget* parent = nullptr);

    // Called by MainWindow whenever a project opens (or a different one is
    // opened over an existing one) — re-roots the view at the new project's
    // folder.
    void SetRootDirectory(const std::filesystem::path& rootDir);

signals:
    void SceneFileActivated(const std::filesystem::path& scenePath);

private slots:
    void OnDoubleClicked(const QModelIndex& index);

private:
    QFileSystemModel* m_Model = nullptr;
    QTreeView* m_TreeView = nullptr;
};
