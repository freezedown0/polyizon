#include "polyizon/rendering/render_graph.hpp"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace polyizon {

void RenderGraph::AddPass(PassDescription pass) {
    if (pass.name.empty()) {
        throw std::invalid_argument("Render graph pass must have a name");
    }
    if (!pass.execute) {
        throw std::invalid_argument("Render graph pass '" + pass.name + "' has no execute callback");
    }
    const auto duplicate = std::find_if(m_Passes.begin(), m_Passes.end(), [&](const PassDescription& existing) {
        return existing.name == pass.name;
    });
    if (duplicate != m_Passes.end()) {
        throw std::invalid_argument("Duplicate render graph pass: " + pass.name);
    }
    m_Passes.push_back(std::move(pass));
    m_Compiled = false;
    m_ExecutionOrder.clear();
}

void RenderGraph::Compile() {
    const std::size_t count = m_Passes.size();
    std::vector<std::unordered_set<std::size_t>> edges(count);
    std::vector<std::size_t> indegree(count, 0);
    std::unordered_map<std::string, std::size_t> lastWriter;

    for (std::size_t passIndex = 0; passIndex < count; ++passIndex) {
        const PassDescription& pass = m_Passes[passIndex];
        auto addDependency = [&](const std::string& resource) {
            const auto writer = lastWriter.find(resource);
            if (writer != lastWriter.end() && edges[writer->second].insert(passIndex).second) {
                ++indegree[passIndex];
            }
        };
        for (const std::string& resource : pass.reads) {
            addDependency(resource);
        }
        for (const std::string& resource : pass.writes) {
            addDependency(resource);
            lastWriter[resource] = passIndex;
        }
    }

    std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
    for (std::size_t i = 0; i < count; ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }

    m_ExecutionOrder.clear();
    while (!ready.empty()) {
        const std::size_t passIndex = ready.top();
        ready.pop();
        m_ExecutionOrder.push_back(passIndex);
        for (const std::size_t dependent : edges[passIndex]) {
            if (--indegree[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }
    if (m_ExecutionOrder.size() != count) {
        m_ExecutionOrder.clear();
        throw std::runtime_error("Render graph contains a dependency cycle");
    }
    m_Compiled = true;
}

void RenderGraph::Execute() const {
    if (!m_Compiled) {
        throw std::logic_error("Render graph must be compiled before execution");
    }
    for (const std::size_t passIndex : m_ExecutionOrder) {
        m_Passes[passIndex].execute();
    }
}

void RenderGraph::Clear() {
    m_Passes.clear();
    m_ExecutionOrder.clear();
    m_Compiled = false;
}

std::vector<std::string_view> RenderGraph::GetExecutionOrder() const {
    std::vector<std::string_view> names;
    names.reserve(m_ExecutionOrder.size());
    for (const std::size_t passIndex : m_ExecutionOrder) {
        names.emplace_back(m_Passes[passIndex].name);
    }
    return names;
}

RenderGraph BuildForwardSceneRenderGraph(
    RenderGraph::ExecuteCallback shadowPass,
    RenderGraph::ExecuteCallback forwardPass) {
    RenderGraph graph;
    graph.AddPass({ "Directional Shadow", {}, { "directional-shadow-map" }, std::move(shadowPass) });
    graph.AddPass({ "Forward PBR", { "directional-shadow-map" }, { "scene-color", "scene-depth" },
        std::move(forwardPass) });
    graph.Compile();
    return graph;
}

} // namespace polyizon
