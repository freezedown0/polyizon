#include "content_browser_panel.hpp"

#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTreeView>
#include <QVBoxLayout>

#include <system_error>

ContentBrowserPanel::ContentBrowserPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* addFilesButton = new QPushButton("Import Assets...", this);
    addFilesButton->setObjectName("PrimaryButton");
    addFilesButton->setToolTip("Copy files into the selected project folder");
    connect(addFilesButton, &QPushButton::clicked, this, &ContentBrowserPanel::OnAddFiles);
    auto* actionRow = new QHBoxLayout();
    actionRow->addStretch();
    actionRow->addWidget(addFilesButton);
    layout->addLayout(actionRow);

    m_Model = new QFileSystemModel(this);
    m_Model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_Model->setNameFilters({
        "*.scene", "*.lua", "*.obj", "*.fbx", "*.gltf", "*.glb",
        "*.png", "*.jpg", "*.jpeg", "*.tga", "*.bmp", "*.hdr"
    });
    m_Model->setNameFilterDisables(false);
    // Root path is set later, once a project is opened (SetRootDirectory) —
    // an empty filter path here just means the model shows nothing until
    // then.

    m_TreeView = new QTreeView(this);
    m_TreeView->setModel(m_Model);
    m_TreeView->setAlternatingRowColors(true);
    m_TreeView->setAnimated(true);
    m_TreeView->setIndentation(18);
    m_TreeView->setSortingEnabled(true);
    m_TreeView->setHeaderHidden(true);
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
    if (info.isDir() || info.suffix().compare("scene", Qt::CaseInsensitive) != 0) {
        return;
    }

    emit SceneFileActivated(std::filesystem::path(info.absoluteFilePath().toStdWString()));
}

std::filesystem::path ContentBrowserPanel::GetImportTargetDirectory() const {
    const QModelIndex current = m_TreeView->currentIndex();
    if (current.isValid()) {
        const QFileInfo info = m_Model->fileInfo(current);
        const QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        return std::filesystem::path(dirPath.toStdWString());
    }
    return std::filesystem::path(m_Model->rootPath().toStdWString());
}

void ContentBrowserPanel::OnAddFiles() {
    const QStringList filePaths = QFileDialog::getOpenFileNames(this, "Add Files");
    if (filePaths.isEmpty()) {
        return; // user cancelled
    }

    const std::filesystem::path targetDir = GetImportTargetDirectory();

    QStringList failed;
    for (const QString& filePath : filePaths) {
        const std::filesystem::path source(filePath.toStdWString());
        const std::filesystem::path destination = targetDir / source.filename();

        std::error_code copyError;
        std::filesystem::copy_file(
            source, destination, std::filesystem::copy_options::overwrite_existing, copyError);
        if (copyError) {
            failed.append(QString::fromStdWString(source.filename().native()));
        }
    }

    if (!failed.isEmpty()) {
        QMessageBox::critical(this, "Failed to import files",
            "Could not copy the following file(s):\n" + failed.join("\n"));
    }
}
