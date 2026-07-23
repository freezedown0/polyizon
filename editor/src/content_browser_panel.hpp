#pragma once

#include <QWidget>

#include <filesystem>

class QFileSystemModel;
class QModelIndex;
class QTreeView;

// Bottom dock panel: browses the current project's root folder (Scenes/,
// Scripts/, Assets/, project.json). Double-clicking a .json file loads it
// as the active scene. Phase 20 adds an "Add Files..." toolbar button
// (import external files — models, textures, scripts — into whichever
// folder is currently selected, or the project root if nothing is) — still
// no drag-and-drop or asset preview, just the one explicit import action.
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
    void OnAddFiles();

private:
    // The directory files should be imported into: the selected row's own
    // directory (or its parent, if the selected row is a file), falling back
    // to the project root when nothing is selected.
    std::filesystem::path GetImportTargetDirectory() const;

    QFileSystemModel* m_Model = nullptr;
    QTreeView* m_TreeView = nullptr;
};
