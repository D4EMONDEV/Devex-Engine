// An older ABI must be rejected before its registration callback is called.
#include <devex/runtime/Game.hpp>

namespace {

struct IncompatibleGameComponent
{
    std::int32_t value = 0;
};

DEVEX_DECLARE_REFLECTION(IncompatibleGameComponent);
DEVEX_REFLECT(IncompatibleGameComponent)
{
    type.field("value", &IncompatibleGameComponent::value);
}

} // namespace

extern "C" DEVEX_GAME_EXPORT std::uint32_t devexGameApiVersion()
{
    return devex::runtime::gameApiVersion - 1;
}

extern "C" DEVEX_GAME_EXPORT void devexRegisterGameModule(devex::runtime::GameRegistry* game)
{
    game->component<IncompatibleGameComponent>();
}
