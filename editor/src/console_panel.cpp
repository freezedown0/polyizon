#include "console_panel.hpp"

#include "polyizon/log.hpp"

#include <QFont>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace {

QString ColorForLevel(polyizon::LogLevel level) {
    switch (level) {
        case polyizon::LogLevel::Error: return "#ff6b6b";
        case polyizon::LogLevel::Warning: return "#e0c341";
        case polyizon::LogLevel::Info: default: return "#d0d0d0";
    }
}

} // namespace

ConsolePanel::ConsolePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_TextEdit = new QPlainTextEdit(this);
    m_TextEdit->setReadOnly(true);
    m_TextEdit->setMaximumBlockCount(2000); // mirrors polyizon::Log's own cap
    m_TextEdit->setFont(QFont("Consolas", 9));
    layout->addWidget(m_TextEdit);
}

void ConsolePanel::Poll() {
    const auto& entries = polyizon::Log::GetEntries();
    if (entries.size() <= m_LastSeenCount) {
        // Log's ring buffer can also shrink (oldest entries dropped) without
        // any new ones added; that's not "new lines to append", so only the
        // strictly-greater case below does anything.
        m_LastSeenCount = entries.size();
        return;
    }

    for (std::size_t i = m_LastSeenCount; i < entries.size(); ++i) {
        const polyizon::LogEntry& entry = entries[i];
        m_TextEdit->appendHtml(
            QString("<span style=\"color:%1\">%2</span>")
                .arg(ColorForLevel(entry.level))
                .arg(QString::fromStdString(entry.message).toHtmlEscaped()));
    }
    m_LastSeenCount = entries.size();
}
