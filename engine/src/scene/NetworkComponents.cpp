#include <devex/scene/NetworkComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(NetworkSpawner)
{
    static_cast<void>(type);
}

DEVEX_REFLECT(NetworkSync)
{
    type.field("fields", &NetworkSync::fields, {.hidden = true})
        .field("rate", &NetworkSync::rate, {.group = "Network"})
        .field("smoothing", &NetworkSync::smoothing);
}

} // namespace devex::scene
