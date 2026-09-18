// devex-bindgen <output.cs>: writes the C# views of the engine's components, which Devex.Managed
// compiles, so that C# reaches them with the layout of this very build.
// devex-bindgen --game <module> <output.cs>: writes the views of the components of a game module
// built for this engine build, as exports do: layouts differ between builds, such as Debug and Release.
// The file is only written when it changes, to keep the C# build up to date.

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/runtime/ComponentViews.hpp>
#include <devex/runtime/GameModule.hpp>
#include <devex/scene/ComponentRegistry.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    const bool game = argc == 4 && std::string_view(argv[1]) == "--game";
    if (argc != 2 && !game)
    {
        std::fputs("usage: devex-bindgen [--game <module>] <output.cs>\n", stderr);
        return 2;
    }
    std::vector<const devex::reflection::TypeInfo*> types;
    std::string text;
    std::unique_ptr<devex::runtime::GameModule> module;
    if (game)
    {
        devex::core::Result<std::unique_ptr<devex::runtime::GameModule>> loaded =
            devex::runtime::GameModule::load(devex::core::pathFromUtf8(argv[2]), {});
        if (!loaded)
        {
            std::fprintf(stderr, "devex-bindgen: %s\n", loaded.error().message.c_str());
            return 1;
        }
        module = std::move(*loaded);
        for (const std::string& name : module->registry().components())
        {
            if (const devex::scene::ComponentType* const type = devex::scene::componentRegistry().find(name))
            {
                types.push_back(type->type);
            }
        }
        text = devex::runtime::generateComponentViews(types, {}, "the C++ code of the game");
    }
    else
    {
        for (const devex::scene::ComponentType& type : devex::scene::componentRegistry().types())
        {
            if (type.layout == nullptr)
            {
                types.push_back(type.type);
            }
        }
        text = devex::runtime::generateComponentViews(types, "Devex", "the engine");
    }

    const std::filesystem::path output = devex::core::pathFromUtf8(argv[argc - 1]);
    if (const devex::core::Result<std::string> existing = devex::core::readTextFile(output);
        existing && *existing == text)
    {
        return 0;
    }
    if (const devex::core::Result<void> written = devex::core::writeTextFile(output, text); !written)
    {
        std::fprintf(stderr, "devex-bindgen: %s\n", written.error().message.c_str());
        return 1;
    }
    return 0;
}
