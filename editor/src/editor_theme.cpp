#include "editor_theme.hpp"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

void ApplyEditorTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setFont(QFont("Segoe UI", 9));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#16191f"));
    palette.setColor(QPalette::WindowText, QColor("#dce2eb"));
    palette.setColor(QPalette::Base, QColor("#111419"));
    palette.setColor(QPalette::AlternateBase, QColor("#191d24"));
    palette.setColor(QPalette::ToolTipBase, QColor("#20252d"));
    palette.setColor(QPalette::ToolTipText, QColor("#f4f7fb"));
    palette.setColor(QPalette::Text, QColor("#dce2eb"));
    palette.setColor(QPalette::Button, QColor("#232831"));
    palette.setColor(QPalette::ButtonText, QColor("#dce2eb"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Highlight, QColor("#3d7eff"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::PlaceholderText, QColor("#737d8c"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#626a76"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#626a76"));
    app.setPalette(palette);

    app.setStyleSheet(R"(
        QMainWindow, QWidget { background: #16191f; color: #dce2eb; }
        QMenuBar { background: #111419; border-bottom: 1px solid #2a3039; padding: 3px 8px; spacing: 4px; }
        QMenuBar::item { padding: 5px 9px; border-radius: 4px; }
        QMenuBar::item:selected { background: #252b35; }
        QMenu { background: #1c2027; border: 1px solid #343b47; padding: 6px; }
        QMenu::item { padding: 7px 30px 7px 12px; border-radius: 4px; }
        QMenu::item:selected { background: #315fbe; color: white; }
        QMenu::separator { height: 1px; background: #343b47; margin: 5px 8px; }
        QToolBar { background: #181c22; border: 0; border-bottom: 1px solid #2a3039; padding: 6px 8px; spacing: 5px; }
        QToolBar::separator { background: #343b47; width: 1px; margin: 4px 6px; }
        QToolButton { background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 6px 9px; }
        QToolButton:hover { background: #252b35; border-color: #343b47; }
        QToolButton:pressed, QToolButton:checked { background: #294f9d; border-color: #477fe9; color: white; }
        QToolButton:disabled { color: #5d6571; }
        QToolButton#PlayButton { background: #263449; border-color: #3c5270; padding: 7px 13px; }
        QToolButton#PlayButton:hover { background: #304869; border-color: #4b77ad; }
        QToolButton#PlayButton:disabled, QToolButton#TransportButton:disabled { background: transparent; border-color: transparent; color: #596271; }
        QToolButton#TransportButton { padding: 7px 11px; }
        QToolButton#CreateEntityButton, QToolButton#AddComponentButton { background: #252b34; border-color: #39414d; padding: 6px 11px; }
        QToolButton#CreateEntityButton:hover, QToolButton#AddComponentButton:hover { background: #303744; border-color: #4b5666; }
        QDockWidget { font-weight: 600; color: #eef2f8; }
        QDockWidget::title { background: #1b1f26; border-bottom: 1px solid #2d333d; padding: 9px 10px; text-align: left; }
        QDockWidget::close-button, QDockWidget::float-button { border: 0; background: transparent; padding: 2px; }
        QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: #303743; }
        QSplitter::handle { background: #0e1014; }
        QSplitter::handle:horizontal { width: 3px; }
        QSplitter::handle:vertical { height: 3px; }
        QPushButton { background: #252b34; border: 1px solid #39414d; border-radius: 5px; padding: 6px 11px; min-height: 18px; }
        QPushButton:hover { background: #303744; border-color: #4b5666; }
        QPushButton:pressed { background: #20252d; }
        QPushButton#PrimaryButton { background: #356fd7; border-color: #4b82ea; color: white; font-weight: 600; }
        QPushButton#DangerButton { color: #ff8b91; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #101318; border: 1px solid #353d49; border-radius: 5px; padding: 5px 7px; selection-background-color: #3d7eff; }
        QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #4b82ea; }
        QDialog { background: #171a20; }
        QDialog QLabel { background: transparent; }
        QLabel#DialogDescription { color: #8f99a8; padding-bottom: 4px; }
        QListView, QTreeView, QPlainTextEdit { background: #12151a; border: 1px solid #292f38; border-radius: 4px; outline: 0; alternate-background-color: #15191f; }
        QListView::item, QTreeView::item { min-height: 25px; padding: 3px 6px; border-radius: 3px; }
        QListView::item:hover, QTreeView::item:hover { background: #202630; }
        QListView::item:selected, QTreeView::item:selected { background: #294f9d; color: white; }
        QHeaderView::section { background: #1b2027; color: #aeb7c5; border: 0; border-bottom: 1px solid #303742; padding: 6px 8px; font-weight: 600; }
        QTabBar::tab { background: #171b21; color: #919ba9; border: 0; border-right: 1px solid #2b313b; padding: 8px 14px; }
        QTabBar::tab:selected { background: #20252d; color: #eef2f8; border-top: 2px solid #4b82ea; padding-top: 6px; }
        QScrollBar:vertical { background: #12151a; width: 10px; margin: 0; }
        QScrollBar::handle:vertical { background: #3a424e; min-height: 28px; border-radius: 5px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QLabel#EmptyState { color: #778190; padding: 24px; }
        QLabel#EntityIdLabel { color: #7f8998; font-family: "Cascadia Mono", "Consolas"; font-size: 8pt; }
        QLabel#SectionHeading { color: #f0f3f8; font-weight: 600; padding: 10px 0 4px 0; border-bottom: 1px solid #2b313a; }
        QWidget#ComponentHeading { background: transparent; border-bottom: 1px solid #2b313a; }
        QLabel#SectionHeadingText { color: #f0f3f8; font-weight: 600; background: transparent; }
        QStatusBar { background: #111419; color: #8993a1; border-top: 1px solid #2a3039; }
    )");
}
