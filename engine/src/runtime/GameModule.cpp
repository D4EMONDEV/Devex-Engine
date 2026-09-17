#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/runtime/GameModule.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <system_error>
#include <utility>

namespace devex::runtime {
namespace {

using ApiVersionFunction = std::uint32_t (*)();
using RegisterFunction = void (*)(GameRegistry* registry);

// Removes the copies of the library that are no longer loaded; loaded ones cannot be deleted.
void removeStaleCopies(const std::filesystem::path& library, const std::filesystem::path& copyDirectory)
{
    std::error_code error;
    const std::string prefix = core::toUtf8(library.stem()) + "-";
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(copyDirectory, error))
    {
        const std::string name = core::toUtf8(entry.path().filename());
        if (name.starts_with(prefix) && entry.path().extension() == library.extension())
        {
            std::error_code ignored;
            std::filesystem::remove(entry.path(), ignored);
        }
    }
}

} // namespace

core::Result<std::unique_ptr<GameModule>> GameModule::load(const std::filesystem::path& library,
                                                           const std::filesystem::path& copyDirectory)
{
    std::error_code error;
    if (!std::filesystem::exists(library, error))
    {
        return core::makeError(core::ErrorCode::NotFound, "'{}' does not exist", core::toUtf8(library));
    }
    if (copyDirectory.empty())
    {
        return loadLibrary(library, library);
    }
    std::filesystem::create_directories(copyDirectory, error);
    removeStaleCopies(library, copyDirectory);

    // Process-wide: every load gets a name no earlier copy used, even one still loaded.
    static std::atomic<std::uint32_t> nextCopy{0};
    std::filesystem::path copy;
    do
    {
        copy = copyDirectory / core::pathFromUtf8(std::format("{}-{}{}", core::toUtf8(library.stem()), nextCopy.fetch_add(1),
                                                              core::toUtf8(library.extension())));
    } while (std::filesystem::exists(copy, error));
    if (!std::filesystem::copy_file(library, copy, std::filesystem::copy_options::overwrite_existing, error))
    {
        return core::makeError(core::ErrorCode::Io, "cannot copy '{}': {}", core::toUtf8(library), error.message());
    }

    return loadLibrary(library, copy);
}

core::Result<std::unique_ptr<GameModule>> GameModule::loadLibrary(const std::filesystem::path& library,
                                                                  const std::filesystem::path& file)
{
    core::Result<platform::SharedLibrary> loaded = platform::SharedLibrary::load(file);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    const auto apiVersion = static_cast<ApiVersionFunction>(loaded->function("devexGameApiVersion"));
    const auto registerGame = static_cast<RegisterFunction>(loaded->function("devexRegisterGameModule"));
    if (apiVersion == nullptr || registerGame == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "'{}' is not a game module: it has no DEVEX_GAME_MODULE entry point",
                               core::toUtf8(library.filename()));
    }
    if (const std::uint32_t version = apiVersion(); version != gameApiVersion)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "'{}' was built for game API version {}, this engine needs version {}: rebuild it",
                               core::toUtf8(library.filename()), version, gameApiVersion);
    }

    std::unique_ptr<GameModule> module(new GameModule(library, std::move(*loaded)));
    registerGame(&module->m_registry);
    return module;
}

GameModule::GameModule(std::filesystem::path source, platform::SharedLibrary library) noexcept
    : m_source(std::move(source))
    , m_library(std::move(library))
{
}

GameModule::~GameModule()
{
    for (const std::string& component : m_registry.components())
    {
        scene::componentRegistry().remove(component);
    }
}

const std::filesystem::path& GameModule::source() const noexcept
{
    return m_source;
}

const GameRegistry& GameModule::registry() const noexcept
{
    return m_registry;
}

std::size_t GameModule::release(scene::Scene& scene) const
{
    std::size_t preserved = 0;
    for (std::size_t index = 0; index < scene.componentPoolCount(); ++index)
    {
        const scene::ComponentPoolBase* const pool = scene.componentPool(index);
        if (pool != nullptr && m_library.contains(pool->moduleAnchor()))
        {
            preserved += scene::preserveComponentPool(scene, index);
        }
    }
    const auto definedHere = [this](std::string_view typeName) {
        return std::ranges::find(m_registry.components(), typeName) != m_registry.components().end();
    };
    return preserved - scene::restorePreservedComponents(scene, [&](std::string_view typeName) { return !definedHere(typeName); });
}

} // namespace devex::runtime
