#include <devex/asset/Artifact.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/import/MaterialFile.hpp>
#include <devex/asset/import/ShaderFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace {

using devex::asset::ShaderBlend;
using devex::asset::ShaderCull;
using devex::asset::ShaderData;
using devex::asset::ShaderDiagnostic;
using devex::asset::ShaderHint;
using devex::asset::ShaderKind;
using devex::asset::ShaderParameterType;
using devex::asset::ShaderSource;

constexpr std::string_view water = R"(shader_type spatial;
render_mode cull_disabled, shadows_disabled;

// The colours of the water.
uniform float3 deep : source_color = float3(0.05, 0.2, 0.4);
uniform float speed : hint_range(0.0, 4.0, 0.5) = 1.5;
uniform int waves = 3;
uniform bool foam = true;
uniform float2 scale = float2(2.0);
uniform sampler2D noise : hint_default_black, filter_nearest;
varying float height;

void vertex()
{
    height = sin(TIME * speed + VERTEX.x) * 0.1;
    VERTEX.y += height;
}

void fragment()
{
    ALBEDO = deep + texture(noise, UV * scale).rgb * float(waves) * (foam ? 1.0 : 0.0) + height;
    ROUGHNESS = 0.1;
}
)";

[[nodiscard]] bool hasError(const ShaderData& shader, std::uint32_t line)
{
    return std::ranges::any_of(shader.diagnostics,
                               [&](const ShaderDiagnostic& diagnostic) { return diagnostic.error && diagnostic.line == line; });
}

[[nodiscard]] bool hasEntry(const ShaderData& shader, std::string_view name)
{
    // Entry points are named in the module by null-terminated strings.
    const std::string_view bytes(reinterpret_cast<const char*>(shader.code.data()), shader.code.size() * 4);
    return bytes.find(std::string(name) + '\0') != std::string_view::npos;
}

} // namespace

TEST_CASE("Shaders read their type, render modes, uniforms and varyings", "[asset][shader]")
{
    const ShaderSource source = devex::asset::parseShaderSource(water);
    CHECK_FALSE(source.hasErrors());
    // filter_nearest is ignored with a warning: the texture keeps its own sampler.
    REQUIRE(source.shader.diagnostics.size() == 1);
    CHECK_FALSE(source.shader.diagnostics.front().error);
    CHECK(source.shader.diagnostics.front().line == 10);

    CHECK(source.shader.kind == ShaderKind::Spatial);
    CHECK(source.shader.cull == ShaderCull::Disabled);
    CHECK_FALSE(source.shader.castsShadows);
    CHECK_FALSE(source.shader.transparent);
    CHECK_FALSE(source.shader.discards);
    REQUIRE(source.shader.parameters.size() == 6);
    const devex::asset::ShaderParameter& deep = source.shader.parameters[0];
    CHECK(deep.name == "deep");
    CHECK(deep.type == ShaderParameterType::Float3);
    CHECK(deep.hint == ShaderHint::Color);
    CHECK_THAT(deep.defaultValue.y, Catch::Matchers::WithinAbs(0.2, 1e-6));
    const devex::asset::ShaderParameter& speed = source.shader.parameters[1];
    CHECK(speed.hint == ShaderHint::Range);
    CHECK(speed.range == devex::math::Vec3{0.0f, 4.0f, 0.5f});
    CHECK(speed.defaultValue.x == 1.5f);
    CHECK(source.shader.parameters[2].type == ShaderParameterType::Int);
    CHECK(source.shader.parameters[2].defaultValue.x == 3.0f);
    CHECK(source.shader.parameters[3].defaultValue.x == 1.0f);
    // One number fills every component.
    CHECK(source.shader.parameters[4].defaultValue == devex::math::Vec4{2.0f, 2.0f, 0.0f, 0.0f});
    CHECK(source.shader.parameters[5].type == ShaderParameterType::Texture);
    CHECK(source.shader.parameters[5].hint == ShaderHint::Black);
    REQUIRE(source.varyings.size() == 1);
    CHECK(source.varyings.front().name == "height");

    CHECK(source.defines("vertex"));
    CHECK(source.defines("fragment"));
    CHECK_FALSE(source.defines("light"));
    CHECK(source.names("ROUGHNESS"));
    CHECK_FALSE(source.names("ALPHA"));
    // The statements Devex handles are blanked, and the lines of the rest stay where they were.
    CHECK(source.code.find("uniform") == std::string::npos);
    CHECK(source.code.find("shader_type") == std::string::npos);
    CHECK(std::ranges::count(source.code, '\n') == std::ranges::count(water, '\n'));
    CHECK(source.code.find("void vertex()") == std::string(water).find("void vertex()"));
}

TEST_CASE("Spatial shaders blend when they write ALPHA, and discard with a threshold", "[asset][shader]")
{
    const ShaderSource glass = devex::asset::parseShaderSource("shader_type spatial;\nvoid fragment() { ALPHA = 0.5; }\n");
    CHECK(glass.shader.transparent);
    CHECK_FALSE(glass.shader.castsShadows);

    const ShaderSource cutout = devex::asset::parseShaderSource(
        "shader_type spatial;\nvoid fragment() { ALPHA = 0.5; ALPHA_SCISSOR_THRESHOLD = 0.4; }\n");
    CHECK_FALSE(cutout.shader.transparent);
    CHECK(cutout.shader.discards);
    CHECK(cutout.shader.castsShadows);

    const ShaderSource glow = devex::asset::parseShaderSource("shader_type spatial;\nrender_mode blend_add;\n");
    CHECK(glow.shader.transparent);
    CHECK(glow.shader.blend == ShaderBlend::Add);

    const ShaderSource sprite = devex::asset::parseShaderSource("shader_type canvas_item;\nrender_mode unshaded;\n");
    CHECK(sprite.shader.kind == ShaderKind::CanvasItem);
    CHECK(sprite.unshaded);
    CHECK(sprite.shader.transparent);
}

TEST_CASE("Mistakes in the statements of a shader are reported on their lines", "[asset][shader]")
{
    const auto errorsOf = [](std::string_view text) { return devex::asset::parseShaderSource(text).shader; };

    CHECK(hasError(errorsOf("void fragment() {}\n"), 1));
    CHECK(hasError(errorsOf("shader_type volume;\n"), 1));
    CHECK(hasError(errorsOf("shader_type spatial;\nrender_mode wireframe;\n"), 2));
    CHECK(hasError(errorsOf("shader_type sky;\nrender_mode unshaded;\n"), 2));
    CHECK(hasError(errorsOf("shader_type spatial;\n\nuniform mat4 m;\n"), 3));
    CHECK(hasError(errorsOf("shader_type spatial;\nuniform sampler2D t = 1.0;\n"), 2));
    CHECK(hasError(errorsOf("shader_type spatial;\nuniform float3 c : hint_range(0, 1);\n"), 2));
    CHECK(hasError(errorsOf("shader_type spatial;\nuniform float a;\nuniform float2 a;\n"), 3));
    CHECK(hasError(errorsOf("shader_type spatial;\nuniform float ALBEDO;\n"), 2));
    CHECK(hasError(errorsOf("shader_type spatial;\nuniform float3 c = float3(1.0, 2.0);\n"), 2));
    CHECK(hasError(errorsOf("shader_type spatial;\nuniform float x : hint_glow;\n"), 2));
    CHECK(hasError(errorsOf("shader_type spatial;\nvarying sampler2D v;\n"), 2));
    CHECK(hasError(errorsOf("shader_type spatial;\nuniform float speed\nvoid vertex() {}\n"), 2));
    // A shader of particles warns about the functions of a GPU simulation it does not run.
    const ShaderData particles = errorsOf("shader_type particles;\nvoid process() {}\n");
    CHECK_FALSE(std::ranges::any_of(particles.diagnostics, &ShaderDiagnostic::error));
    CHECK(particles.diagnostics.size() == 1);
}

TEST_CASE("Every kind of shader compiles into the entry points of its passes", "[asset][shader]")
{
    const ShaderData spatial = devex::asset::compileShader(water, "water.dvxshader");
    INFO((spatial.diagnostics.empty() ? std::string{} : spatial.diagnostics.front().message));
    REQUIRE(spatial.compiled());
    for (const std::string_view entry : {"sceneVertex", "sceneFragment", "prepassVertex", "prepassFragment", "shadowVertex",
                                         "localShadowVertex", "pickVertex", "pickFragment", "maskVertex", "maskFragment"})
    {
        CHECK(hasEntry(spatial, entry));
    }
    CHECK_FALSE(hasEntry(spatial, "shadowFragment"));
    CHECK(spatial.parameters.size() == 6);

    // A light model, a cut-out that discards in the depth passes too, normal maps and world coordinates.
    const ShaderData toon = devex::asset::compileShader(R"(shader_type spatial;
render_mode world_vertex_coords, ambient_light_disabled;
uniform sampler2D mask : hint_default_white;
void vertex() { VERTEX += NORMAL * 0.01 * texture(mask, UV).r; }
void fragment()
{
    ALBEDO = float3(1.0, 0.5, 0.2);
    ALPHA = texture(mask, UV).a;
    ALPHA_SCISSOR_THRESHOLD = 0.5;
    NORMAL_MAP = float3(0.5, 0.5, 1.0);
}
void light()
{
    DIFFUSE_LIGHT += step(0.5, dot(NORMAL, LIGHT)) * ATTENUATION * LIGHT_COLOR / PI;
}
)",
                                                       "toon.dvxshader");
    INFO((toon.diagnostics.empty() ? std::string{} : toon.diagnostics.front().message));
    REQUIRE(toon.compiled());
    CHECK(hasEntry(toon, "shadowFragment"));

    const ShaderData canvas = devex::asset::compileShader(R"(shader_type canvas_item;
uniform float amount : hint_range(0.0, 1.0) = 0.5;
varying float2 offset;
void vertex() { offset = VERTEX * 0.1; VERTEX.x += sin(TIME) * 0.1; }
void fragment()
{
    COLOR.rgb = lerp(COLOR.rgb, float3(1.0) - COLOR.rgb, amount) + float3(offset, 0.0) * 0.0;
    COLOR.a *= texture(TEXTURE, UV + TEXTURE_PIXEL_SIZE).a;
}
)",
                                                         "invert.dvxshader");
    INFO((canvas.diagnostics.empty() ? std::string{} : canvas.diagnostics.front().message));
    REQUIRE(canvas.compiled());
    for (const std::string_view entry : {"spriteVertex", "spriteFragment", "spritePickVertex", "spritePickFragment",
                                         "spriteMaskVertex", "spriteMaskFragment"})
    {
        CHECK(hasEntry(canvas, entry));
    }

    const ShaderData particles = devex::asset::compileShader(R"(shader_type particles;
render_mode blend_add;
void vertex() { VERTEX.y += sin(TIME + float(INSTANCE_ID)) * 0.1; }
void fragment() { COLOR.rgb *= float3(1.0, 0.6, 0.2); }
)",
                                                            "embers.dvxshader");
    INFO((particles.diagnostics.empty() ? std::string{} : particles.diagnostics.front().message));
    REQUIRE(particles.compiled());
    CHECK(particles.blend == ShaderBlend::Add);
    CHECK(hasEntry(particles, "particleVertex"));
    CHECK(hasEntry(particles, "particleFragment"));

    const ShaderData sky = devex::asset::compileShader(devex::asset::shaderTemplate(ShaderKind::Sky), "sky.dvxshader");
    INFO((sky.diagnostics.empty() ? std::string{} : sky.diagnostics.front().message));
    REQUIRE(sky.compiled());
    for (const std::string_view entry : {"skyVertex", "skyFragment", "bakeFragment"})
    {
        CHECK(hasEntry(sky, entry));
    }

    // The new shaders of the editor compile as they are.
    for (const ShaderKind kind : {ShaderKind::Spatial, ShaderKind::CanvasItem, ShaderKind::Particles})
    {
        const ShaderData created = devex::asset::compileShader(devex::asset::shaderTemplate(kind), "new.dvxshader");
        INFO(devex::asset::toString(kind));
        CHECK(created.compiled());
        CHECK(created.diagnostics.empty());
    }
}

TEST_CASE("Errors of the compiler land on the lines of the shader", "[asset][shader]")
{
    const ShaderData shader = devex::asset::compileShader(R"(shader_type spatial;
uniform float speed = 1.0;

void fragment()
{
    ALBEDO = float3(sped);
}
)",
                                                          "broken.dvxshader");
    CHECK_FALSE(shader.compiled());
    REQUIRE_FALSE(shader.diagnostics.empty());
    CHECK(shader.diagnostics.front().line == 6);
    CHECK(shader.diagnostics.front().error);
    CHECK(shader.diagnostics.front().message.find("sped") != std::string::npos);
    // The kind and the uniforms are known all the same, for the materials using it.
    CHECK(shader.parameters.size() == 1);

    // A compiler that is not there is an error of the whole shader.
    const ShaderData missing =
        devex::asset::compileShader("shader_type spatial;\n", "missing.dvxshader", {.slangc = "no-such-slangc"});
    CHECK_FALSE(missing.compiled());
    REQUIRE(missing.diagnostics.size() == 1);
    CHECK(missing.diagnostics.front().line == 0);
}

TEST_CASE("Shaders and the parameters of materials survive encoding and their files", "[asset][shader]")
{
    ShaderData shader = devex::asset::compileShader(water, "water.dvxshader");
    REQUIRE(shader.compiled());
    shader.diagnostics.push_back({.line = 3, .column = 2, .message = "a warning", .error = false});
    const auto decoded = devex::asset::decodeShader(devex::asset::encodeShader(shader));
    REQUIRE(decoded);
    CHECK(*decoded == shader);

    const devex::asset::AssetId shaderId = devex::asset::AssetId::generate();
    const devex::asset::AssetId noise = devex::asset::AssetId::generate();
    devex::asset::MaterialData material;
    material.shader = shaderId;
    material.parameters = {
        {.name = "speed", .value = {2.5f, 0.0f, 0.0f, 0.0f}},
        {.name = "deep", .value = {0.1f, 0.2f, 0.3f, 0.0f}, .components = 3},
        {.name = "noise", .texture = noise},
    };
    CHECK(devex::asset::decodeMaterial(devex::asset::encodeMaterial(material)) == material);
    const std::string text = devex::asset::writeMaterialFile(material);
    INFO(text);
    CHECK(text.find("[parameters]") != std::string::npos);
    CHECK(text.find("deep = vec3(") != std::string::npos);
    const auto read = devex::asset::parseMaterialFile(text);
    REQUIRE(read);
    CHECK(*read == material);

    // Booleans read as 1 and 0; a material without a shader writes none.
    const auto flags = devex::asset::parseMaterialFile("[material format=2]\n\n[parameters]\nfoam = true\n");
    REQUIRE(flags);
    CHECK(flags->parameters.front().value.x == 1.0f);
    CHECK(devex::asset::writeMaterialFile({}).find("shader") == std::string::npos);
    CHECK_FALSE(devex::asset::parseMaterialFile("[material format=2]\n\n[parameters]\nx = vec3(1, 2)\n"));
    CHECK_FALSE(devex::asset::parseMaterialFile("[material format=2]\n\n[other]\n"));
}

TEST_CASE("The asset database imports shaders, with their errors", "[asset][database][shader]")
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-shaders-" + devex::core::Uuid::generate().toString());
    {
        const devex::asset::Project project = *devex::asset::createProject(root, "Shaded");
        const std::filesystem::path assets = project.assetsDirectory();
        REQUIRE(devex::core::writeTextFile(assets / "water.dvxshader", water));
        REQUIRE(devex::core::writeTextFile(assets / "broken.dvxshader",
                                           "shader_type spatial;\nvoid fragment() { ALBEDO = 1.0 + ; }\n"));
        devex::core::JobSystem jobs(2);
        auto database = devex::asset::AssetDatabase::open(project, jobs, {.watchFiles = false});
        REQUIRE(database.has_value());
        (*database)->waitForImports();
        static_cast<void>((*database)->update());

        for (const std::string_view name : {"water", "broken"})
        {
            const auto id = (*database)->findByPath(std::format("res://assets/{}.dvxshader", name));
            REQUIRE(id.has_value());
            CHECK((*database)->find(*id)->type == devex::asset::AssetType::Shader);
            const auto bytes = (*database)->loadArtifact(*id);
            REQUIRE(bytes.has_value());
            const auto shader = devex::asset::decodeShader(*bytes);
            REQUIRE(shader.has_value());
            // A shader that does not compile still imports, with its errors.
            CHECK(shader->compiled() == (name == "water"));
            CHECK(hasError(*shader, 2) == (name == "broken"));
        }
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("The shaders of the sandbox compile without warnings", "[asset][shader]")
{
    const std::filesystem::path folder = std::filesystem::path{DEVEX_SANDBOX_DIRECTORY} / "assets" / "shaders";
    std::size_t count = 0;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() != devex::asset::shaderExtension)
        {
            continue;
        }
        const auto text = devex::core::readTextFile(entry.path());
        REQUIRE(text.has_value());
        const ShaderData shader = devex::asset::compileShader(*text, entry.path());
        INFO(entry.path().filename().string());
        INFO((shader.diagnostics.empty() ? std::string{} : shader.diagnostics.front().message));
        CHECK(shader.compiled());
        CHECK(shader.diagnostics.empty());
        ++count;
    }
    CHECK(count == 7);
}
