#include "GameCodeBuilder.hpp"
#include "ManagedCodeBuilder.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/asset/Package.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Executable.hpp>
#include <devex/runtime/GameExport.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <format>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace devex::runtime {
namespace {

constexpr std::string_view exportMarker = "devex-export.txt";
constexpr std::uint32_t packageIconSize = 256;

#ifdef _WIN32
constexpr std::string_view executableSuffix = ".exe";
constexpr std::string_view libraryExtension = ".dll";
#else
constexpr std::string_view executableSuffix = "";
constexpr std::string_view libraryExtension = ".so";
#endif

[[nodiscard]] bool equalsIgnoringCase(std::string_view left, std::string_view right)
{
    return std::ranges::equal(left, right, [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

// The configuration an engine build was made in, as its CMake package records it.
[[nodiscard]] std::optional<std::string> readConfiguration(const std::filesystem::path& cmakeDirectory)
{
    const core::Result<std::string> text = core::readTextFile(cmakeDirectory / "DevexConfig.cmake");
    constexpr std::string_view marker = "set(DEVEX_BUILD_TYPE \"";
    const std::size_t start = text ? text->find(marker) : std::string::npos;
    if (start == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t end = text->find('"', start + marker.size());
    if (end == std::string::npos || end == start + marker.size())
    {
        return std::nullopt;
    }
    return text->substr(start + marker.size(), end - start - marker.size());
}

[[nodiscard]] std::optional<EngineBuild> engineBuildAt(const std::filesystem::path& directory)
{
    std::error_code error;
    std::filesystem::path player = directory / "bin" / "devex-player";
    player += executableSuffix;
    if (!std::filesystem::is_regular_file(player, error))
    {
        return std::nullopt;
    }
    std::optional<std::string> configuration = readConfiguration(directory / "cmake");
    if (!configuration)
    {
        return std::nullopt;
    }
    return EngineBuild{.name = core::toUtf8(directory.filename()), .configuration = std::move(*configuration),
                       .directory = directory};
}

void collectAssetIds(const serialization::TextValue& value, std::vector<asset::AssetId>& ids)
{
    const serialization::TextCall* const call = std::get_if<serialization::TextCall>(&value);
    if (call == nullptr)
    {
        return;
    }
    if (call->name == "asset" && call->arguments.size() == 1)
    {
        const std::string* const text = serialization::asString(call->arguments.front());
        if (const std::optional<core::Uuid> uuid = text != nullptr ? core::Uuid::parse(*text) : std::nullopt)
        {
            ids.push_back(asset::AssetId{*uuid});
        }
        return;
    }
    for (const serialization::TextValue& argument : call->arguments)
    {
        collectAssetIds(argument, ids);
    }
}

// Built-in assets, such as the primitive meshes, come with the engine.
[[nodiscard]] bool isBuiltin(asset::AssetId id) noexcept
{
    return std::ranges::all_of(std::span(id.uuid.bytes()).first(8), [](std::uint8_t byte) { return byte == 0; });
}

[[nodiscard]] core::Result<void> checkCancelled(const std::atomic<bool>* cancel)
{
    if (cancel != nullptr && cancel->load())
    {
        return core::makeError(core::ErrorCode::InvalidState, "the export was cancelled");
    }
    return {};
}

// Empties the output folder, when it is missing, empty or a previous export.
[[nodiscard]] core::Result<void> prepareOutput(const std::filesystem::path& output)
{
    std::error_code error;
    if (std::filesystem::exists(output, error))
    {
        if (!std::filesystem::is_directory(output, error))
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "'{}' is not a folder", core::toUtf8(output));
        }
        const bool empty = std::filesystem::is_empty(output, error);
        if (!empty && !std::filesystem::exists(output / exportMarker, error))
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "'{}' is not empty and does not hold a previous export: choose another folder",
                                   core::toUtf8(output));
        }
        std::vector<std::filesystem::path> entries;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(output, error))
        {
            entries.push_back(entry.path());
        }
        for (const std::filesystem::path& entry : entries)
        {
            std::filesystem::remove_all(entry, error);
            if (error)
            {
                return core::makeError(core::ErrorCode::Io, "cannot remove '{}' ({}): is the game still running?",
                                       core::toUtf8(entry), error.message());
            }
        }
    }
    std::filesystem::create_directories(output, error);
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "cannot create '{}': {}", core::toUtf8(output), error.message());
    }
    return {};
}

[[nodiscard]] core::Result<void> copyFile(const std::filesystem::path& from, const std::filesystem::path& to)
{
    std::error_code error;
    std::filesystem::copy(from, to,
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                          error);
    if (error)
    {
        return core::makeError(core::ErrorCode::Io, "cannot copy '{}': {}", core::toUtf8(from), error.message());
    }
    return {};
}

// Runs a program and waits for it, its output going to the log.
[[nodiscard]] core::Result<void> runAndWait(const std::vector<std::string>& arguments,
                                            const std::filesystem::path& directory, std::string_view failure)
{
    core::Result<platform::Process> process = platform::Process::start(arguments, directory);
    if (!process)
    {
        return core::makeError(process.error().code, "{}: {}", failure, process.error().message);
    }
    std::optional<int> exitCode;
    while (!exitCode)
    {
        for (const std::string& line : process->readLines())
        {
            if (!line.empty())
            {
                DEVEX_LOG_INFO("{}", line);
            }
        }
        exitCode = process->exitCode();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    for (const std::string& line : process->readLines())
    {
        if (!line.empty())
        {
            DEVEX_LOG_INFO("{}", line);
        }
    }
    if (*exitCode != 0)
    {
        return core::makeError(core::ErrorCode::InvalidState, "{} (code {})", failure, *exitCode);
    }
    return {};
}

// Publishes .NET next to the game: the runtime, the engine's C# assembly and nothing to install.
[[nodiscard]] core::Result<void> copyDotnet(const ExportPlan& plan, const std::filesystem::path& scripts)
{
    constexpr std::string_view hostTemplate = R"(<!-- Generated by the Devex editor to publish .NET with an exported game. -->
<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>Devex.Host</AssemblyName>
    <OutputType>Exe</OutputType>
    <RuntimeIdentifier>win-x64</RuntimeIdentifier>
    <SelfContained>true</SelfContained>
    <InvariantGlobalization>true</InvariantGlobalization>
    <EnableDynamicLoading>true</EnableDynamicLoading>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>

  <ItemGroup>
    <Compile Include="Program.cs" />
    <Reference Include="Devex.Managed">
      <HintPath>{0}/Devex.Managed.dll</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>

</Project>
)";
    // The runtime is what matters; this program never runs.
    constexpr std::string_view programTemplate = R"(internal static class Program
{
    private static void Main()
    {
    }
}
)";

    const std::filesystem::path host = plan.project.cacheDirectory() / "export" / "dotnet";
    const std::filesystem::path managedSource = plan.engine.binDirectory() / "managed";
    if (core::Result<void> written =
            core::writeTextFile(host / "Devex.Host.csproj", std::format(hostTemplate, core::toUtf8(managedSource)));
        !written)
    {
        return written;
    }
    if (core::Result<void> written = core::writeTextFile(host / "Program.cs", programTemplate); !written)
    {
        return written;
    }

    const std::array<std::string, 10> arguments{
        "dotnet",
        "publish",
        core::toUtf8(host / "Devex.Host.csproj"),
        "-c",
        "Release",
        "-o",
        core::toUtf8(plan.output / "managed"),
        std::format("-p:BaseIntermediateOutputPath={}/obj/", core::toUtf8(host)),
        "--nologo",
        "-v:quiet",
    };
    core::Result<platform::Process> process = platform::Process::start(arguments, plan.project.root);
    if (!process)
    {
        return core::makeError(process.error().code, "the .NET SDK is needed to export a game written in C#: {}",
                               process.error().message);
    }
    std::optional<int> exitCode;
    while (!exitCode)
    {
        for (const std::string& line : process->readLines())
        {
            if (!line.empty())
            {
                DEVEX_LOG_DEBUG("{}", line);
            }
        }
        exitCode = process->exitCode();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (*exitCode != 0)
    {
        return core::makeError(core::ErrorCode::InvalidState, "cannot publish .NET with the game (code {})", *exitCode);
    }
    return copyFile(scripts, plan.output / scripts.filename());
}

// Copies the player, the libraries, the shaders and the game module into the output folder.

[[nodiscard]] core::Result<std::filesystem::path> copyEngine(const ExportPlan& plan, const std::string& name,
                                                             const std::optional<std::filesystem::path>& library)
{
    const std::filesystem::path bin = plan.engine.binDirectory();
    std::filesystem::path player = bin / "devex-player";
    player += executableSuffix;
    std::filesystem::path executable = plan.output / core::pathFromUtf8(name);
    executable += executableSuffix;
    if (core::Result<void> copied = copyFile(player, executable); !copied)
    {
        return std::unexpected(copied.error());
    }

    std::error_code error;
    const auto copyLibraries = [&](const std::filesystem::path& folder) -> core::Result<void> {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error))
        {
            const std::string fileName = core::toUtf8(entry.path().filename());
            const bool test = fileName.starts_with("devex_test") || fileName.find("_tests") != std::string::npos;
            if (entry.is_regular_file(error) && !test &&
                equalsIgnoringCase(core::toUtf8(entry.path().extension()), libraryExtension))
            {
                if (core::Result<void> copied = copyFile(entry.path(), plan.output / entry.path().filename()); !copied)
                {
                    return copied;
                }
            }
        }
        return {};
    };
    if (core::Result<void> copied = copyLibraries(bin); !copied)
    {
        return std::unexpected(copied.error());
    }
    // The C++ runtime, which the engine build copies there when it may be redistributed.
    if (std::filesystem::is_directory(bin / "redist", error))
    {
        if (core::Result<void> copied = copyLibraries(bin / "redist"); !copied)
        {
            return std::unexpected(copied.error());
        }
    }
    if (core::Result<void> copied = copyFile(bin / "shaders", plan.output / "shaders"); !copied)
    {
        return std::unexpected(copied.error());
    }
    // Debug builds have the tools overlay (F1), which draws with the fonts and icons of resources/.
    if (equalsIgnoringCase(plan.engine.configuration, "Debug") && std::filesystem::is_directory(bin / "resources", error))
    {
        if (core::Result<void> copied = copyFile(bin / "resources", plan.output / "resources"); !copied)
        {
            return std::unexpected(copied.error());
        }
    }
    if (library)
    {
        if (core::Result<void> copied = copyFile(*library, plan.output / detail::GameCodeBuilder::libraryFileName()); !copied)
        {
            return std::unexpected(copied.error());
        }
    }
    return executable;
}

[[nodiscard]] core::Result<asset::Image> loadIcon(const std::filesystem::path& path)
{
    const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    core::Result<asset::Image> image = asset::decodeImage(*bytes);
    if (!image)
    {
        return image;
    }
    return asset::resizeImage(*image, packageIconSize, packageIconSize);
}

[[nodiscard]] core::Result<void> setIcon(const std::filesystem::path& executable, const asset::Image& icon)
{
    std::vector<platform::IconImage> images;
    for (const std::uint32_t size : {256u, 48u, 32u, 16u})
    {
        asset::Image resized = size == icon.width ? icon : asset::resizeImage(icon, size, size);
        platform::IconImage image{.size = size, .rgba = std::move(resized.rgba)};
        if (size == 256)
        {
            image.png = asset::encodePng(icon);
        }
        images.push_back(std::move(image));
    }
    return platform::setExecutableIcon(executable, images);
}

} // namespace

std::vector<EngineBuild> findEngineBuilds(const std::filesystem::path& binDirectory)
{
    std::vector<EngineBuild> builds;
    std::filesystem::path bin = binDirectory.lexically_normal();
    if (!bin.has_filename())
    {
        bin = bin.parent_path();
    }
    const std::filesystem::path running = bin.parent_path();
    if (std::optional<EngineBuild> build = engineBuildAt(running))
    {
        builds.push_back(std::move(*build));
    }
    std::vector<std::filesystem::path> siblings;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(running.parent_path(), error))
    {
        if (entry.is_directory(error) && entry.path() != running)
        {
            siblings.push_back(entry.path());
        }
    }
    std::ranges::sort(siblings);
    for (const std::filesystem::path& sibling : siblings)
    {
        if (std::optional<EngineBuild> build = engineBuildAt(sibling))
        {
            builds.push_back(std::move(*build));
        }
    }
    return builds;
}

std::optional<EngineBuild> findEngineBuild(std::span<const EngineBuild> builds, std::string_view configuration)
{
    const auto found = std::ranges::find_if(
        builds, [&](const EngineBuild& build) { return equalsIgnoringCase(build.configuration, configuration); });
    return found != builds.end() ? std::optional(*found) : std::nullopt;
}

std::string executableName(std::string_view gameName)
{
    std::string name;
    for (const char character : gameName)
    {
        const auto byte = static_cast<unsigned char>(character);
        const bool forbidden = byte < 0x20 || std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos;
        name.push_back(forbidden ? '_' : character);
    }
    const auto trimmed = [](char character) { return character == ' ' || character == '.'; };
    while (!name.empty() && trimmed(name.back()))
    {
        name.pop_back();
    }
    while (!name.empty() && trimmed(name.front()))
    {
        name.erase(name.begin());
    }
    return name.empty() ? std::string("Game") : name;
}

core::Result<std::vector<asset::AssetId>> assetReferences(asset::AssetType type, std::span<const std::byte> artifact)
{
    std::vector<asset::AssetId> ids;
    switch (type)
    {
    case asset::AssetType::Scene: {
        const core::Result<std::string> text = asset::decodeScene(artifact);
        const core::Result<serialization::TextDocument> document =
            text ? serialization::parseText(*text)
                 : core::Result<serialization::TextDocument>(std::unexpected(text.error()));
        if (!document)
        {
            return std::unexpected(document.error());
        }
        for (const serialization::TextSection& section : document->sections)
        {
            for (const serialization::TextProperty& property : section.attributes)
            {
                collectAssetIds(property.value, ids);
            }
            for (const serialization::TextProperty& property : section.properties)
            {
                collectAssetIds(property.value, ids);
            }
        }
        break;
    }
    case asset::AssetType::Material: {
        const core::Result<asset::MaterialData> material = asset::decodeMaterial(artifact);
        if (!material)
        {
            return std::unexpected(material.error());
        }
        for (const asset::AssetId texture : {material->baseColorTexture, material->metallicRoughnessTexture,
                                             material->normalTexture, material->occlusionTexture, material->emissiveTexture})
        {
            ids.push_back(texture);
        }
        break;
    }
    case asset::AssetType::Mesh: {
        const core::Result<asset::MeshData> mesh = asset::decodeMesh(artifact);
        if (!mesh)
        {
            return std::unexpected(mesh.error());
        }
        for (const asset::Submesh& submesh : asset::submeshesOf(*mesh))
        {
            ids.push_back(submesh.material);
        }
        break;
    }
    case asset::AssetType::Model: {
        const core::Result<asset::ModelData> model = asset::decodeModel(artifact);
        if (!model)
        {
            return std::unexpected(model.error());
        }
        for (const asset::ModelNode& node : model->nodes)
        {
            ids.push_back(node.mesh);
        }
        break;
    }
    case asset::AssetType::Texture:
        break;
    }
    std::erase_if(ids, [](asset::AssetId id) { return !id.isValid(); });
    std::ranges::sort(ids);
    const auto [first, last] = std::ranges::unique(ids);
    ids.erase(first, last);
    return ids;
}

core::Result<ExportPlan> planExport(const asset::AssetDatabase& database, const EngineBuild& engine)
{
    if (database.pendingImports() > 0)
    {
        return core::makeError(core::ErrorCode::InvalidState, "assets are still importing");
    }
    ExportPlan plan{.project = database.project(), .engine = engine};
    const asset::Project& project = plan.project;
    const std::filesystem::path output = core::pathFromUtf8(project.exportSettings.output);
    plan.output = (output.is_absolute() ? output : project.root / output).lexically_normal();

    for (const asset::SourceFile& source : database.sources())
    {
        if (source.status == asset::ImportStatus::Failed)
        {
            DEVEX_LOG_WARNING("{} failed to import and cannot be exported: {}", source.path, source.error);
        }
    }
    for (const asset::AssetInfo& info : database.assets())
    {
        std::string path;
        if (info.source == info.id)
        {
            if (const std::optional<asset::SourceFile> source = database.sourceOf(info.id))
            {
                path = source->path;
            }
        }
        plan.assets.push_back({.info = info, .path = std::move(path), .artifact = database.artifactPath(info.id)});
    }

    const auto importedScene = [&](const std::string& path) -> core::Result<asset::AssetId> {
        const std::optional<asset::AssetId> id = database.findByPath(path);
        const asset::AssetInfo* const info = id ? database.find(*id) : nullptr;
        if (info == nullptr || info->type != asset::AssetType::Scene)
        {
            return core::makeError(core::ErrorCode::NotFound, "the scene {} does not exist or is not imported", path);
        }
        return *id;
    };
    if (!project.startupScene.empty())
    {
        core::Result<asset::AssetId> startup = importedScene(project.startupScene);
        if (!startup)
        {
            return std::unexpected(startup.error());
        }
        plan.scenes.push_back(*startup);
    }
    else
    {
        for (const asset::SourceFile& source : database.sources())
        {
            if (const asset::AssetInfo* const info = database.find(source.id); info != nullptr && info->type == asset::AssetType::Scene)
            {
                plan.scenes.push_back(source.id);
                break;
            }
        }
        if (plan.scenes.empty())
        {
            return core::makeError(core::ErrorCode::NotFound, "{} has no scene to export", project.name);
        }
    }
    for (const std::string& path : project.exportSettings.scenes)
    {
        core::Result<asset::AssetId> scene = importedScene(path);
        if (!scene)
        {
            return std::unexpected(scene.error());
        }
        if (std::ranges::find(plan.scenes, *scene) == plan.scenes.end())
        {
            plan.scenes.push_back(*scene);
        }
    }

    for (std::string folder : project.exportSettings.includeFolders)
    {
        while (folder.ends_with('/'))
        {
            folder.pop_back();
        }
        const std::optional<std::filesystem::path> directory = project.absolutePath(folder);
        std::error_code error;
        if (!directory || !std::filesystem::is_directory(*directory, error))
        {
            return core::makeError(core::ErrorCode::NotFound, "the folder {} does not exist", folder);
        }
        for (const asset::SourceFile& source : database.sources())
        {
            if (!source.path.starts_with(folder + "/"))
            {
                continue;
            }
            for (const asset::AssetId id : source.assets)
            {
                if (database.find(id) != nullptr)
                {
                    plan.includedAssets.push_back(id);
                }
            }
        }
    }

    plan.managed = detail::ManagedCodeBuilder::hasCode(project);
    if (project.window.icon.isValid())
    {
        const std::optional<asset::SourceFile> source = database.sourceOf(project.window.icon);
        plan.icon = source ? project.absolutePath(source->path) : std::nullopt;
        if (!plan.icon)
        {
            DEVEX_LOG_WARNING("The icon of {} does not exist: the game is exported without one", project.name);
        }
    }
    return plan;
}

core::Result<ExportResult> exportGame(const ExportPlan& plan, const std::function<void(const ExportProgress&)>& progress,
                                     const std::atomic<bool>* cancel)
{
    const auto start = std::chrono::steady_clock::now();
    const auto report = [&progress](std::string step, float fraction) {
        if (progress)
        {
            progress({.step = std::move(step), .fraction = fraction});
        }
    };
    const asset::Project& project = plan.project;
    const std::string name = executableName(project.name);

    std::optional<std::filesystem::path> library;
    if (!plan.packageOnly && detail::GameCodeBuilder::hasCode(project))
    {
        report(std::format("Building the game code ({})", plan.engine.configuration), 0.02f);
        detail::GameCodeBuilder builder(project, plan.engine.cmakeDirectory(), plan.engine.configuration);
        if (core::Result<void> built = builder.buildAndWait(); !built)
        {
            return std::unexpected(built.error());
        }
        library = detail::GameCodeBuilder::libraryPath(project, plan.engine.configuration);
    }
    std::optional<std::filesystem::path> scripts;
    if (!plan.packageOnly && plan.managed)
    {
        report("Building the C# code", 0.2f);
        // Built apart from the editor's, against the engine build of the export, with views of the C++
        // components made from the module built for it: layouts differ between builds.
        const std::filesystem::path managedBuild = project.cacheDirectory() / "export" / "csharp";
        const std::filesystem::path generated = managedBuild / "generated";
        std::error_code error;
        std::filesystem::remove_all(generated, error);
        std::filesystem::create_directories(generated, error);
        if (library)
        {
            std::filesystem::path bindgen = plan.engine.binDirectory() / "devex-bindgen";
            bindgen += executableSuffix;
            if (core::Result<void> generatedViews =
                    runAndWait({core::toUtf8(bindgen), "--game", core::toUtf8(*library),
                                core::toUtf8(generated / "GameComponents.g.cs")},
                               project.root, "cannot generate the C# views of the C++ components");
                !generatedViews)
            {
                return std::unexpected(generatedViews.error());
            }
        }
        detail::ManagedCodeBuilder builder(project, plan.engine.binDirectory() / "managed",
                                           detail::ManagedCodeBuilder::Target{.buildDirectory = managedBuild,
                                                                              .generatedDirectory = generated});
        if (core::Result<void> built = builder.buildAndWait(); !built)
        {
            return std::unexpected(built.error());
        }
        scripts = builder.builtAssembly();
    }
    if (core::Result<void> running = checkCancelled(cancel); !running)
    {
        return std::unexpected(running.error());
    }

    // The scenes and every asset they need, found by following references.
    report("Gathering the assets", 0.4f);
    std::unordered_map<asset::AssetId, const ExportPlan::Asset*> byId;
    for (const ExportPlan::Asset& entry : plan.assets)
    {
        byId.emplace(entry.info.id, &entry);
    }
    struct Pending
    {
        asset::AssetId id;
        const ExportPlan::Asset* referrer = nullptr;
    };
    std::deque<Pending> pending;
    for (const asset::AssetId id : plan.scenes)
    {
        pending.push_back({id});
    }
    for (const asset::AssetId id : plan.includedAssets)
    {
        pending.push_back({id});
    }
    std::vector<const ExportPlan::Asset*> exported;
    std::unordered_set<asset::AssetId> seen;
    while (!pending.empty())
    {
        const Pending next = pending.front();
        pending.pop_front();
        if (!seen.insert(next.id).second)
        {
            continue;
        }
        const auto found = byId.find(next.id);
        if (found == byId.end())
        {
            if (!isBuiltin(next.id))
            {
                DEVEX_LOG_WARNING("{} refers to asset {}, which is not imported: it is not exported",
                                  next.referrer != nullptr ? next.referrer->info.name : std::string("The game"), next.id.uuid);
            }
            continue;
        }
        const ExportPlan::Asset& entry = *found->second;
        exported.push_back(&entry);
        if (entry.info.type == asset::AssetType::Texture)
        {
            continue;
        }
        const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(entry.artifact);
        core::Result<std::vector<asset::AssetId>> references =
            bytes ? assetReferences(entry.info.type, *bytes)
                  : core::Result<std::vector<asset::AssetId>>(std::unexpected(bytes.error()));
        if (!references)
        {
            return core::makeError(references.error().code, "cannot read {}: {}", entry.info.name, references.error().message);
        }
        for (const asset::AssetId reference : *references)
        {
            pending.push_back({reference, &entry});
        }
    }

    if (core::Result<void> prepared = prepareOutput(plan.output); !prepared)
    {
        return std::unexpected(prepared.error());
    }

    std::filesystem::path packagePath = plan.output / core::pathFromUtf8(name);
    packagePath += asset::packageExtension;
    core::Result<asset::PackageWriter> writer = asset::PackageWriter::create(packagePath);
    if (!writer)
    {
        return std::unexpected(writer.error());
    }
    // The package names its startup scene, which stays the first scene whatever the project says.
    asset::Project settings = project;
    const auto startup = byId.find(plan.scenes.front());
    settings.startupScene = startup != byId.end() ? startup->second->path : std::string();
    settings.exportSettings = {};
    writer->setProject(settings);

    std::optional<asset::Image> icon;
    if (plan.icon)
    {
        if (core::Result<asset::Image> loaded = loadIcon(*plan.icon))
        {
            icon = std::move(*loaded);
            writer->setIcon({.width = icon->width, .height = icon->height, .rgba = icon->rgba});
        }
        else
        {
            DEVEX_LOG_WARNING("The game is exported without its icon: {}", loaded.error());
        }
    }

    for (std::size_t index = 0; index < exported.size(); ++index)
    {
        const ExportPlan::Asset& entry = *exported[index];
        report(std::format("Packing {}", entry.info.name), 0.5f + 0.35f * static_cast<float>(index) / static_cast<float>(exported.size()));
        const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(entry.artifact);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        if (core::Result<void> added = writer->add(entry.info, entry.path, *bytes); !added)
        {
            return std::unexpected(added.error());
        }
        if (core::Result<void> running = checkCancelled(cancel); !running)
        {
            return std::unexpected(running.error());
        }
    }
    const core::Result<asset::PackageStatistics> statistics = writer->finish();
    if (!statistics)
    {
        return std::unexpected(statistics.error());
    }

    ExportResult result{.output = plan.output, .assets = statistics->assets, .packageBytes = statistics->fileBytes};
    if (!plan.packageOnly)
    {
        report("Copying the engine", 0.88f);
        core::Result<std::filesystem::path> executable = copyEngine(plan, name, library);
        if (!executable)
        {
            return std::unexpected(executable.error());
        }
        result.executable = std::move(*executable);
        if (scripts)
        {
            report("Copying .NET", 0.92f);
            if (core::Result<void> copied = copyDotnet(plan, *scripts); !copied)
            {
                return std::unexpected(copied.error());
            }
        }
        if (icon)
        {
            if (core::Result<void> set = setIcon(result.executable, *icon); !set)
            {
                DEVEX_LOG_WARNING("The executable keeps the default icon: {}", set.error());
            }
        }
    }
    if (core::Result<void> marked = core::writeTextFile(
            plan.output / exportMarker,
            std::format("{} exported with Devex {} ({}), from {}\n", project.name, core::version(), plan.engine.configuration,
                        core::toUtf8(project.file)));
        !marked)
    {
        return std::unexpected(marked.error());
    }
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    report("Exported", 1.0f);
    return result;
}

int exportFromCommandLine(std::span<const std::string_view> arguments, const std::filesystem::path& binDirectory)
{
    constexpr std::string_view usage =
        "usage: devex-editor --export <project.dvxproj> [--output <folder>] [--configuration <Release|Debug>]";
    if (arguments.size() < 2 || arguments.front() != "--export")
    {
        DEVEX_LOG_ERROR("{}", usage);
        return 2;
    }
    std::optional<std::filesystem::path> output;
    std::optional<std::string> configuration;
    for (std::size_t index = 2; index < arguments.size(); ++index)
    {
        if (index + 1 < arguments.size() && arguments[index] == "--output")
        {
            output = core::pathFromUtf8(arguments[++index]);
        }
        else if (index + 1 < arguments.size() && arguments[index] == "--configuration")
        {
            configuration = std::string(arguments[++index]);
        }
        else
        {
            DEVEX_LOG_ERROR("Unknown argument '{}'\n{}", arguments[index], usage);
            return 2;
        }
    }

    const core::Result<asset::Project> project = asset::loadProject(core::pathFromUtf8(arguments[1]));
    if (!project)
    {
        DEVEX_LOG_ERROR("Cannot open the project: {}", project.error());
        return 1;
    }
    core::JobSystem jobs(0);
    core::Result<std::unique_ptr<asset::AssetDatabase>> database =
        asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    if (!database)
    {
        DEVEX_LOG_ERROR("Cannot open the assets of {}: {}", project->name, database.error());
        return 1;
    }
    DEVEX_LOG_INFO("Importing the assets of {}...", project->name);
    (*database)->waitForImports();
    static_cast<void>((*database)->update());

    const std::vector<EngineBuild> builds = findEngineBuilds(binDirectory);
    const std::string wanted = configuration.value_or(project->exportSettings.configuration);
    const std::optional<EngineBuild> engine = findEngineBuild(builds, wanted);
    if (!engine)
    {
        std::string available;
        for (const EngineBuild& build : builds)
        {
            available += std::format("{}{} ({})", available.empty() ? "" : ", ", build.configuration, build.name);
        }
        DEVEX_LOG_ERROR("No {} build of the engine next to this one; available: {}", wanted,
                        available.empty() ? "none" : available);
        return 1;
    }
    core::Result<ExportPlan> plan = planExport(**database, *engine);
    if (!plan)
    {
        DEVEX_LOG_ERROR("Cannot export {}: {}", project->name, plan.error());
        return 1;
    }
    if (output)
    {
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(*output, error);
        plan->output = (error ? *output : absolute).lexically_normal();
    }

    std::string lastStep;
    const core::Result<ExportResult> result = exportGame(*plan, [&lastStep](const ExportProgress& step) {
        // Packing reports every asset: those become one line, the other steps print once each.
        const std::string kind = step.step.starts_with("Packing ") ? "Packing" : step.step;
        if (kind != lastStep)
        {
            lastStep = kind;
            DEVEX_LOG_INFO("[{:3.0f}%] {}", step.fraction * 100.0f, step.step);
        }
    });
    if (!result)
    {
        DEVEX_LOG_ERROR("Cannot export {}: {}", project->name, result.error());
        return 1;
    }
    DEVEX_LOG_INFO("Exported {} to {} in {:.1f} s: {} assets, {:.1f} MB", project->name, core::toUtf8(result->output),
                   result->seconds, result->assets, static_cast<double>(result->packageBytes) / (1024.0 * 1024.0));
    return 0;
}

} // namespace devex::runtime
