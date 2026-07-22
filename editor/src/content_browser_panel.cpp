#include "content_browser_panel.hpp"

#include <QFileInfo>
#include <QFileSystemModel>
#include <QTreeView>
#include <QVBoxLayout>

ContentBrowserPanel::ContentBrowserPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_Model = new QFileSystemModel(this);
    // Root path is set later, once a project is opened (SetRootDirectory) —
    // an empty filter path here just means the model shows nothing until
    // then.

    m_TreeView = new QTreeView(this);
    m_TreeView->setModel(m_Model);
    // Name column only — Size/Type/Date Modified add clutter with no use
    // yet (no per-asset metadata beyond the filename this phase).
    m_TreeView->hideColumn(1);
    m_TreeView->hideColumn(2);
    m_TreeView->hideColumn(3);
    layout->addWidget(m_TreeView);

    connect(m_TreeView, &QTreeView::doubleClicked, this, &ContentBrowserPanel::OnDoubleClicked);
}

void ContentBrowserPanel::SetRootDirectory(const std::filesystem::path& rootDir) {
    const QString rootPath = QString::fromStdWString(rootDir.native());
    m_Model->setRootPath(rootPath);
    m_TreeView->setRootIndex(m_Model->index(rootPath));
}

void ContentBrowserPanel::OnDoubleClicked(const QModelIndex& index) {
    const QFileInfo info = m_Model->fileInfo(index);
    if (info.isDir() || info.suffix().compare("json", Qt::CaseInsensitive) != 0) {
        return;
    }

    emit SceneFileActivated(std::filesystem::path(info.absoluteFilePath().toStdWString()));
}
