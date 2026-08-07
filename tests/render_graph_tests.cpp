#include "polyizon/rendering/render_graph.hpp"
#include "polyizon/rendering/forward_plus_lights.hpp"
#include "polyizon/scene/components.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    bool passed = true;
    std::vector<std::string> executed;
    polyizon::RenderGraph graph;
    graph.AddPass({ "Shadow", {}, { "shadow" }, [&] { executed.emplace_back("Shadow"); } });
    graph.AddPass({ "Lighting", { "shadow" }, { "color" }, [&] { executed.emplace_back("Lighting"); } });
    graph.AddPass({ "Present", { "color" }, {}, [&] { executed.emplace_back("Present"); } });

    graph.Compile();
    graph.Execute();
    passed &= graph.IsCompiled();
    passed &= executed == std::vector<std::string>{ "Shadow", "Lighting", "Present" };

    graph.Clear();
    passed &= !graph.IsCompiled() && graph.GetPassCount() == 0;
    bool rejectedUncompiledExecution = false;
    try {
        graph.Execute();
    } catch (const std::logic_error&) {
        rejectedUncompiledExecution = true;
    }
    passed &= rejectedUncompiledExecution;

    executed.clear();
    polyizon::RenderGraph sceneGraph = polyizon::BuildForwardSceneRenderGraph(
        [&] { executed.emplace_back("Shadow"); },
        [&] { executed.emplace_back("Forward"); });
    const auto sceneOrder = sceneGraph.GetExecutionOrder();
    passed &= sceneOrder.size() == 2;
    passed &= sceneOrder[0] == "Directional Shadow" && sceneOrder[1] == "Forward PBR";
    sceneGraph.Execute();
    passed &= executed == std::vector<std::string>{ "Shadow", "Forward" };

    polyizon::Scene lightScene;
    auto& lightRegistry = lightScene.GetRegistry();
    const entt::entity pointEntity = lightScene.CreateEntity();
    lightRegistry.emplace<polyizon::TransformComponent>(pointEntity).position = { 1.0f, 2.0f, 3.0f };
    lightRegistry.emplace<polyizon::PointLightComponent>(pointEntity).range = 12.0f;
    const entt::entity spotEntity = lightScene.CreateEntity();
    lightRegistry.emplace<polyizon::TransformComponent>(spotEntity);
    lightRegistry.emplace<polyizon::SpotLightComponent>(spotEntity);
    const polyizon::ForwardPlusLightBuffer gpuLights = polyizon::BuildForwardPlusLightBuffer(lightScene);
    passed &= gpuLights.counts.x == 2 && gpuLights.counts.y == 1 && gpuLights.counts.z == 1;
    passed &= gpuLights.counts.w == 0;
    passed &= gpuLights.lights[0].positionAndRange.w == 12.0f;
    passed &= gpuLights.lights[0].outerCosAndType.y == 0.0f;
    passed &= gpuLights.lights[1].outerCosAndType.y == 1.0f;

    if (!passed) {
        std::cerr << "Polyizon render graph tests failed\n";
        return 1;
    }
    std::cout << "Polyizon render graph tests passed\n";
    return 0;
}
