#pragma once

#include <deque>
#include <string>

namespace polyizon {

enum class LogLevel {
    Info,
    Warning,
    Error,
};

struct LogEntry {
    LogLevel level;
    std::string message;
};

// Process-wide in-memory log sink, read by the editor's Console panel (see
// editor/src/console_panel.hpp) and written to by the Vulkan validation
// callback (core/src/vulkan/context.cpp) and the editor's ScriptEngine on a
// Lua runtime error. Lives in core (not editor) purely because the Vulkan
// debug callback needs to reach it and core has no notion of "editor".
//
// Not thread-safe: every writer/reader in this codebase runs on a single
// thread (the GLFW game loop or the Qt main thread respectively), matching
// every other "single-threaded this phase" assumption already made
// elsewhere (see EditorViewportRenderer/ScriptEngine's doc comments).
class Log {
public:
    static void Info(std::string message) { Push(LogLevel::Info, std::move(message)); }
    static void Warning(std::string message) { Push(LogLevel::Warning, std::move(message)); }
    static void Error(std::string message) { Push(LogLevel::Error, std::move(message)); }

    static const std::deque<LogEntry>& GetEntries() noexcept { return Entries(); }

private:
    static constexpr std::size_t kMaxEntries = 2000;

    static std::deque<LogEntry>& Entries() {
        static std::deque<LogEntry> entries;
        return entries;
    }

    static void Push(LogLevel level, std::string message) {
        std::deque<LogEntry>& entries = Entries();
        entries.push_back({ level, std::move(message) });
        if (entries.size() > kMaxEntries) {
            entries.pop_front();
        }
    }
};

} // namespace polyizon
