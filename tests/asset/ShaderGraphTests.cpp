#include <devex/asset/Artifact.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/ShaderGraphData.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/import/ShaderGraphFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace {

using devex::asset::ShaderData;
using devex::asset::ShaderFunction;
using devex::asset::ShaderGraphData;
using devex::asset::ShaderGraphLink;
using devex::asset::ShaderGraphNode;
using devex::asset::ShaderKind;

// A node of the catalog entry of that label.
std::uint32_t add(ShaderGraphData& graph, std::string_view label, ShaderFunction function = ShaderFunction::Fragment)
{
    const std::span<const devex::asset::ShaderNodeEntry> catalog = devex::asset::shaderNodeCatalog();
    const auto entry = std::ranges::find(catalog, label, &devex::asset::ShaderNodeEntry::label);
    REQUIRE(entry != catalog.end());
    const std::uint32_t id = graph.nextId();
    graph.nodes.push_back(devex::asset::makeShaderNode(*entry, graph.kind, function, id, {0.0f, 0.0f}));
    return id;
}

// The port of the output node of a function named so.
std::uint32_t outputPort(const ShaderGraphData& graph, ShaderFunction function, std::string_view name)
{
    const ShaderGraphNode* const output = graph.outputOf(function);
    REQUIRE(output != nullptr);
    const std::vector<devex::asset::ShaderGraphPort> ports = devex::asset::inputsOf(*output, graph.kind);
    const auto found = std::ranges::find(ports, name, &devex::asset::ShaderGraphPort::name);
    REQUIRE(found != ports.end());
    return static_cast<std::uint32_t>(found - ports.begin());
}

void link(ShaderGraphData& graph, std::uint32_t from, std::uint32_t fromPort, std::uint32_t to, std::uint32_t toPort)
{
    REQUIRE(graph.connect({from, fromPort, to, toPort}));
}

[[nodiscard]] std::string messages(const ShaderData& shader)
{
    std::string text;
    for (const devex::asset::ShaderDiagnostic& diagnostic : shader.diagnostics)
    {
        text += std::format("[node {}] {}\n", diagnostic.node, diagnostic.message);
    }
    return text;
}

} // namespace

TEST_CASE("New shader graphs compile for every kind", "[asset][shader][graph]")
{
    for (const ShaderKind kind : {ShaderKind::Spatial, ShaderKind::CanvasItem, ShaderKind::Particles, ShaderKind::Sky})
    {
        const ShaderGraphData graph = devex::asset::makeShaderGraph(kind);
        CHECK(graph.nodes.size() == devex::asset::functionsOf(kind).size());
        const ShaderData shader = devex::asset::compileShaderGraph(graph, "new.dvxshadergraph");
        INFO(devex::asset::toString(kind) << "\n" << messages(shader));
        CHECK(shader.compiled());
        CHECK(shader.kind == kind);
    }
}

TEST_CASE("Shader graphs turn into code that draws, with parameters, textures and conversions", "[asset][shader][graph]")
{
    ShaderGraphData graph = devex::asset::makeShaderGraph(ShaderKind::Spatial);
    const std::uint32_t output = graph.outputOf(ShaderFunction::Fragment)->id;

    // A colour parameter mixed towards white by noise, a fresnel glow, and the alpha of a texture.
    const std::uint32_t tint = add(graph, "Color Parameter");
    graph.find(tint)->setText("name", "tint");
    const std::uint32_t white = add(graph, "Color");
    const std::uint32_t noise = add(graph, "Noise");
    const std::uint32_t mix = add(graph, "Mix");
    link(graph, tint, 0, mix, 0);
    link(graph, white, 0, mix, 1);
    link(graph, noise, 0, mix, 2);
    link(graph, mix, 0, output, outputPort(graph, ShaderFunction::Fragment, "Albedo"));
    const std::uint32_t fresnel = add(graph, "Fresnel");
    link(graph, fresnel, 0, output, outputPort(graph, ShaderFunction::Fragment, "Emission"));
    const std::uint32_t mask = add(graph, "Texture Parameter");
    graph.find(mask)->setText("name", "mask");
    const std::uint32_t sample = add(graph, "Texture");
    link(graph, mask, 0, sample, 0);
    link(graph, sample, 1, output, outputPort(graph, ShaderFunction::Fragment, "Alpha"));
    // A vector into a number takes its first component; into the vertex, a wave.
    const std::uint32_t time = add(graph, "Input", ShaderFunction::Vertex);
    graph.find(time)->setText("name", "TIME");
    const std::uint32_t sine = add(graph, "Function", ShaderFunction::Vertex);
    link(graph, time, 0, sine, 0);
    const std::uint32_t vertex = add(graph, "Input", ShaderFunction::Vertex);
    graph.find(vertex)->setText("name", "VERTEX");
    const std::uint32_t offset = add(graph, "Vector Operator", ShaderFunction::Vertex);
    graph.find(offset)->setText("op", "add");
    link(graph, vertex, 0, offset, 0);
    link(graph, sine, 0, offset, 1);
    link(graph, offset, 0, graph.outputOf(ShaderFunction::Vertex)->id, outputPort(graph, ShaderFunction::Vertex, "Vertex"));

    const devex::asset::GeneratedShader generated = devex::asset::generateShaderGraph(graph, "test.dvxshadergraph");
    INFO(generated.code);
    CHECK(generated.diagnostics.empty());
    CHECK(generated.code.find("uniform float4 tint : source_color") != std::string::npos);
    CHECK(generated.code.find("uniform sampler2D mask : hint_default_white;") != std::string::npos);
    CHECK(generated.code.find("lerp(tint.rgb") != std::string::npos);
    CHECK(generated.code.find("float3(n") != std::string::npos);
    CHECK(generated.code.find("ALPHA = ") != std::string::npos);
    CHECK(std::ranges::count(generated.code, '\n') == static_cast<std::ptrdiff_t>(generated.lineNodes.size()));

    const ShaderData shader = devex::asset::compileShaderGraph(graph, "test.dvxshadergraph");
    INFO(messages(shader));
    REQUIRE(shader.compiled());
    CHECK(shader.transparent);
    REQUIRE(shader.parameters.size() == 2);
    CHECK(shader.parameters[0].name == "tint");
    CHECK(shader.parameters[1].type == devex::asset::ShaderParameterType::Texture);
}

TEST_CASE("Links of shader graphs stay within a function and never loop", "[asset][shader][graph]")
{
    ShaderGraphData graph = devex::asset::makeShaderGraph(ShaderKind::Spatial);
    const std::uint32_t a = add(graph, "Operator");
    const std::uint32_t b = add(graph, "Operator");
    const std::uint32_t c = add(graph, "Operator");
    CHECK(graph.connect({a, 0, b, 0}));
    CHECK(graph.connect({b, 0, c, 0}));
    // c feeds back into a: a loop.
    CHECK_FALSE(graph.connect({c, 0, a, 1}));
    // A node never feeds itself, nor a port it does not have.
    CHECK_FALSE(graph.connect({a, 0, a, 1}));
    CHECK_FALSE(graph.connect({a, 3, c, 1}));
    // A new link into an input replaces the old one.
    CHECK(graph.connect({a, 0, c, 0}));
    CHECK(graph.linkInto(c, 0)->fromNode == a);
    CHECK(std::ranges::count_if(graph.links, [&](const ShaderGraphLink& link) { return link.toNode == c; }) == 1);
    // Across functions, no.
    const std::uint32_t vertex = add(graph, "Operator", ShaderFunction::Vertex);
    CHECK_FALSE(graph.connect({vertex, 0, a, 0}));
    // Removing a node removes its links.
    graph.removeNode(a);
    CHECK(graph.linkInto(c, 0) == nullptr);
    CHECK(graph.linkInto(b, 0) == nullptr);
}

TEST_CASE("Mistakes of shader graphs name their nodes", "[asset][shader][graph]")
{
    ShaderGraphData graph = devex::asset::makeShaderGraph(ShaderKind::Spatial);
    const std::uint32_t output = graph.outputOf(ShaderFunction::Fragment)->id;

    // An expression whose code does not compile.
    const std::uint32_t expression = add(graph, "Expression");
    graph.find(expression)->setText("code", "result = a + undefinedThing;");
    link(graph, expression, 0, output, outputPort(graph, ShaderFunction::Fragment, "Roughness"));
    ShaderData shader = devex::asset::compileShaderGraph(graph, "broken.dvxshadergraph");
    CHECK_FALSE(shader.compiled());
    REQUIRE_FALSE(shader.diagnostics.empty());
    CHECK(std::ranges::any_of(shader.diagnostics, [&](const devex::asset::ShaderDiagnostic& diagnostic) {
        return diagnostic.error && diagnostic.node == expression && diagnostic.message.find("undefinedThing") != std::string::npos;
    }));

    // The same expression fixed compiles, with its two lines.
    graph.find(expression)->setText("code", "float twice = a * 2.0;\nresult = twice + b;");
    shader = devex::asset::compileShaderGraph(graph, "fixed.dvxshadergraph");
    INFO(messages(shader));
    CHECK(shader.compiled());

    // Two parameters of one name, and a texture node without a texture.
    const std::uint32_t first = add(graph, "Float Parameter");
    const std::uint32_t second = add(graph, "Float Parameter");
    graph.find(first)->setText("name", "speed");
    graph.find(second)->setText("name", "speed");
    const std::uint32_t sample = add(graph, "Texture");
    link(graph, sample, 1, output, outputPort(graph, ShaderFunction::Fragment, "Metallic"));
    shader = devex::asset::compileShaderGraph(graph, "twice.dvxshadergraph");
    CHECK_FALSE(shader.compiled());
    const auto names = [&](std::uint32_t node) {
        return std::ranges::any_of(shader.diagnostics,
                                   [&](const devex::asset::ShaderDiagnostic& diagnostic) { return diagnostic.error && diagnostic.node == node; });
    };
    CHECK(names(second));
    CHECK(names(sample));
}

TEST_CASE("Canvas, particle and sky graphs read what they draw", "[asset][shader][graph]")
{
    // A sprite: the colour of its own texture, darkened.
    ShaderGraphData canvas = devex::asset::makeShaderGraph(ShaderKind::CanvasItem);
    const std::uint32_t sample = add(canvas, "Texture");
    const std::uint32_t darker = add(canvas, "Vector Operator");
    canvas.find(darker)->inputs[1] = devex::math::Vec4{0.5f};
    link(canvas, sample, 0, darker, 0);
    link(canvas, darker, 0, canvas.outputOf(ShaderFunction::Fragment)->id, outputPort(canvas, ShaderFunction::Fragment, "Color"));
    link(canvas, sample, 1, canvas.outputOf(ShaderFunction::Fragment)->id, outputPort(canvas, ShaderFunction::Fragment, "Alpha"));
    const devex::asset::GeneratedShader generated = devex::asset::generateShaderGraph(canvas, "sprite.dvxshadergraph");
    CHECK(generated.code.find("texture(TEXTURE, UV)") != std::string::npos);
    CHECK(generated.code.find("COLOR.rgb = ") != std::string::npos);
    CHECK(generated.code.find("COLOR.a = ") != std::string::npos);
    ShaderData shader = devex::asset::compileShaderGraph(canvas, "sprite.dvxshadergraph");
    INFO(messages(shader));
    CHECK(shader.compiled());

    // Particles that flicker by their instance.
    ShaderGraphData particles = devex::asset::makeShaderGraph(ShaderKind::Particles);
    const std::uint32_t instance = add(particles, "Input");
    particles.find(instance)->setText("name", "INSTANCE_ID");
    const std::uint32_t sine = add(particles, "Function");
    link(particles, instance, 0, sine, 0);
    link(particles, sine, 0, particles.outputOf(ShaderFunction::Fragment)->id, outputPort(particles, ShaderFunction::Fragment, "Alpha"));
    shader = devex::asset::compileShaderGraph(particles, "particles.dvxshadergraph");
    INFO(messages(shader));
    CHECK(shader.compiled());

    // A sky from the height of the direction looked at.
    ShaderGraphData sky = devex::asset::makeShaderGraph(ShaderKind::Sky);
    const std::uint32_t direction = add(sky, "Input", ShaderFunction::Sky);
    sky.find(direction)->setText("name", "EYEDIR");
    const std::uint32_t split = add(sky, "Decompose Vector", ShaderFunction::Sky);
    link(sky, direction, 0, split, 0);
    const std::uint32_t blend = add(sky, "Mix", ShaderFunction::Sky);
    link(sky, split, 1, blend, 2);
    link(sky, blend, 0, sky.outputOf(ShaderFunction::Sky)->id, 0);
    shader = devex::asset::compileShaderGraph(sky, "sky.dvxshadergraph");
    INFO(messages(shader));
    CHECK(shader.compiled());
}

TEST_CASE("Shader graphs survive their files, and import as shaders", "[asset][shader][graph]")
{
    ShaderGraphData graph = devex::asset::makeShaderGraph(ShaderKind::Spatial);
    graph.renderModes = {"cull_disabled", "unshaded"};
    const std::uint32_t parameter = add(graph, "Float Parameter");
    graph.find(parameter)->setText("name", "speed");
    graph.find(parameter)->setText("hint", "range");
    graph.find(parameter)->setVector("range", devex::math::Vec4{0.0f, 4.0f, 0.5f, 0.0f});
    graph.find(parameter)->position = {-200.0f, 40.0f};
    const std::uint32_t expression = add(graph, "Expression");
    graph.find(expression)->setText("code", "float x = \"quoted\" == \"\" ? 0.0 : 1.0;\nresult = a;");
    link(graph, parameter, 0, expression, 0);
    const std::string text = devex::asset::writeShaderGraphFile(graph);
    const auto read = devex::asset::parseShaderGraphFile(text);
    INFO(text);
    INFO((read ? std::string{} : read.error().message));
    REQUIRE(read.has_value());
    CHECK(*read == graph);
    CHECK_FALSE(devex::asset::parseShaderGraphFile("[shader_graph format=1 type=\"volume\"]\n"));
    CHECK_FALSE(devex::asset::parseShaderGraphFile("[shader_graph format=1 type=\"sky\"]\n\n[node id=0 type=\"input\" function=\"sky\"]\n"));

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-shader-graphs-" + devex::core::Uuid::generate().toString());
    {
        const devex::asset::Project project = *devex::asset::createProject(root, "Graphs");
        ShaderGraphData glow = devex::asset::makeShaderGraph(ShaderKind::Spatial);
        const std::uint32_t color = add(glow, "Color Parameter");
        glow.find(color)->setText("name", "glow");
        link(glow, color, 0, glow.outputOf(ShaderFunction::Fragment)->id, outputPort(glow, ShaderFunction::Fragment, "Emission"));
        REQUIRE(devex::core::writeTextFile(project.assetsDirectory() / "glow.dvxshadergraph", devex::asset::writeShaderGraphFile(glow)));
        devex::core::JobSystem jobs(2);
        auto database = devex::asset::AssetDatabase::open(project, jobs, {.watchFiles = false});
        REQUIRE(database.has_value());
        (*database)->waitForImports();
        static_cast<void>((*database)->update());
        const auto id = (*database)->findByPath("res://assets/glow.dvxshadergraph");
        REQUIRE(id.has_value());
        CHECK((*database)->find(*id)->type == devex::asset::AssetType::Shader);
        const auto bytes = (*database)->loadArtifact(*id);
        REQUIRE(bytes.has_value());
        const auto shader = devex::asset::decodeShader(*bytes);
        REQUIRE(shader.has_value());
        CHECK(shader->compiled());
        REQUIRE(shader->parameters.size() == 1);
        CHECK(shader->parameters.front().name == "glow");
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("The shader graphs of the sandbox compile without warnings", "[asset][shader][graph]")
{
    const std::filesystem::path folder = std::filesystem::path{DEVEX_SANDBOX_DIRECTORY} / "assets" / "shaders";
    std::size_t count = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() != devex::asset::shaderGraphExtension)
        {
            continue;
        }
        const auto text = devex::core::readTextFile(entry.path());
        REQUIRE(text.has_value());
        const auto graph = devex::asset::parseShaderGraphFile(*text);
        REQUIRE(graph.has_value());
        const ShaderData shader = devex::asset::compileShaderGraph(*graph, entry.path());
        INFO(entry.path().filename().string() << "\n" << messages(shader));
        CHECK(shader.compiled());
        CHECK(shader.diagnostics.empty());
        ++count;
    }
    CHECK(count == 1);
}
