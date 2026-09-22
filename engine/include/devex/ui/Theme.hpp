#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/scene/Entity.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace devex::asset {
struct ThemeData;
struct ThemeStyle;
} // namespace devex::asset

namespace devex::scene {
class Scene;
}

namespace devex::serialization {
struct TextValue;
}

// The styles of a theme, written into the elements that follow them. The game applies them while
// it plays, and the editor to the scene it edits, so that both show the interface the same way.
namespace devex::ui {

// Where themes come from: the asset manager, which keeps them loaded.
using ThemeSource = std::function<std::shared_ptr<const asset::ThemeData>(asset::AssetId)>;

// The style an element follows, as the inspector shows it: the style it names, the theme of the
// canvas above it, and the style of that name in the theme, when there is one.
struct ElementStyle
{
    std::string name;
    asset::AssetId theme;
    std::shared_ptr<const asset::ThemeData> data;
    const asset::ThemeStyle* style = nullptr;

    // Whether the style writes this field, which the element then cannot keep a value of its own.
    [[nodiscard]] bool sets(std::string_view component, std::string_view field) const noexcept;
};

// The style of an element: nothing named when it follows none, no theme when no canvas above it
// has one, and no style found when the theme lacks the name.
[[nodiscard]] ElementStyle styleOf(const scene::Scene& scene, scene::Entity entity, const ThemeSource& themes);

// Writes, once a call, the values of every style into the components of the elements that name it,
// through reflection and without adding a component an element does not have. The values a style
// names therefore belong to the theme: a script or the inspector that changes them is overruled.
class ThemeApplier
{
public:
    void setThemes(ThemeSource themes);
    [[nodiscard]] const ThemeSource& themes() const noexcept;

    void apply(scene::Scene& scene);

private:
    ThemeSource m_themes;
    // Every value a theme has written, read from its text once: themes apply every frame, and
    // reading text is the slow part. Keyed by the text, which stays right when a theme is reloaded.
    std::unordered_map<std::string, std::shared_ptr<const serialization::TextValue>> m_values;
};

} // namespace devex::ui
