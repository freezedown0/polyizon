#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace polyizon {

// A small backend-independent frame graph. Passes declare the resources they
// read and write; Compile derives a stable execution order and rejects cycles.
// Vulkan image/buffer allocation and barriers remain in the passes for now and
// can move behind graph-owned resources incrementally.
class RenderGraph {
public:
    using ExecuteCallback = std::function<void()>;

    struct PassDescription {
        std::string name;
        std::vector<std::string> reads;
        std::vector<std::string> writes;
        ExecuteCallback execute;
    };

    void AddPass(PassDescription pass);
    void Compile();
    void Execute() const;
    void Clear();

    [[nodiscard]] bool IsCompiled() const noexcept { return m_Compiled; }
    [[nodiscard]] std::size_t GetPassCount() const noexcept { return m_Passes.size(); }
    [[nodiscard]] std::vector<std::string_view> GetExecutionOrder() const;

private:
    std::vector<PassDescription> m_Passes;
    std::vector<std::size_t> m_ExecutionOrder;
    bool m_Compiled = false;
};

// The common realistic scene path used by both the editor viewport and the
// standalone client. Additional Stage 2 passes should be introduced here so
// both applications receive them together.
RenderGraph BuildForwardSceneRenderGraph(
    RenderGraph::ExecuteCallback shadowPass,
    RenderGraph::ExecuteCallback forwardPass);

} // namespace polyizon
