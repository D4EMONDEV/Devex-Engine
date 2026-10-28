#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Animator controllers: the state machines Animator components play, as Unity's Animator
// Controller and Godot's AnimationTree. States play clips, blend trees or sprite animations, and
// transitions move between them when the parameters that game code sets meet their conditions.
// Layers play state machines of their own over the base layer, on the bones of their masks.
namespace devex::asset {

inline constexpr std::string_view animatorExtension = ".dvxanimator";

// What game code sets on an animator. The values are stored in cooked files: never reorder them.
enum class AnimatorParameterType : std::uint8_t
{
    Float = 0,
    Integer = 1,
    Bool = 2,
    // Set by code, and reset by the transition it lets through.
    Trigger = 3,
};

struct DEVEX_API AnimatorParameter
{
    std::string name;
    AnimatorParameterType type = AnimatorParameterType::Float;
    // The value it starts with: 0 or 1 for a bool, whole for an integer, unused for a trigger.
    float defaultValue = 0.0f;

    bool operator==(const AnimatorParameter&) const = default;
};

// How a state mixes its clips. The values are stored in cooked files: never reorder them.
enum class AnimatorBlend : std::uint8_t
{
    // The first clip plays alone.
    None = 0,
    // A parameter moves between clips placed along a line, walk to run.
    Linear = 1,
    // Two parameters move between clips placed on a plane, the directions of a walk (Unity's 2D
    // freeform cartesian blend).
    Planar = 2,
};

// A clip of a state, and where it sits in its blend tree.
struct DEVEX_API AnimatorMotion
{
    AssetId clip;
    // Along the line of a linear blend.
    float threshold = 0.0f;
    // On the plane of a planar blend.
    math::Vec2 position{0.0f};

    bool operator==(const AnimatorMotion&) const = default;
};

struct DEVEX_API AnimatorState
{
    // Unique in its animator; transitions and code name states by it.
    std::string name;
    AnimatorBlend blend = AnimatorBlend::None;
    std::vector<AnimatorMotion> motions;
    // The parameters that place a blend: the only one of a linear blend, X and Y of a planar one.
    std::string parameter;
    std::string parameterY;
    // Played by the SpriteAnimator of the entity while the state is active: a named animation of
    // its sprite frames. Empty leaves the SpriteAnimator alone.
    std::string spriteAnimation;
    // Times the speed of the clips; a speed parameter, when named, multiplies it again.
    float speed = 1.0f;
    std::string speedParameter;
    // Starts again at its end; otherwise holds its last pose.
    bool loop = true;
    // Where the state sits in the graph of the editor.
    math::Vec2 graphPosition{0.0f};

    bool operator==(const AnimatorState&) const = default;
};

// What a condition checks on its parameter. The values are stored in cooked files: never reorder
// them.
enum class AnimatorTest : std::uint8_t
{
    Greater = 0,
    Less = 1,
    Equals = 2,
    NotEquals = 3,
    IsTrue = 4,
    IsFalse = 5,
    // A trigger that is set.
    Triggered = 6,
};

struct DEVEX_API AnimatorCondition
{
    std::string parameter;
    AnimatorTest test = AnimatorTest::Greater;
    float value = 0.0f;

    bool operator==(const AnimatorCondition&) const = default;
};

struct DEVEX_API AnimatorTransition
{
    // The state it leaves, or empty for any state ("Any State").
    std::string from;
    std::string to;
    // Seconds of the crossfade from one state to the other.
    float duration = 0.2f;
    // How far into the state it waits, from 0 (its start) to 1 (its end) and beyond for loops;
    // negative when it leaves whenever its conditions are met.
    float exitTime = -1.0f;
    // All of them must be met.
    std::vector<AnimatorCondition> conditions;

    bool operator==(const AnimatorTransition&) const = default;
};

// How a layer lays its pose over the layers before it. The values are stored in cooked files: never
// reorder them.
enum class AnimatorLayerBlend : std::uint8_t
{
    // Takes the place of the pose below, by its weight, on the bones of its mask: an arm that waves
    // while the legs walk.
    Override = 0,
    // Adds to the pose below how far its clips move from their first frame: a breath, a recoil.
    Additive = 1,
};

inline constexpr std::string_view baseLayerName = "Base Layer";

// A state machine of a controller, as Unity's layers: its states play on the bones of its mask, over
// the layers before it. The first layer is the base, which plays on the whole skeleton at full weight;
// the blending, the weight and the mask of the others say how much of their pose shows.
struct DEVEX_API AnimatorLayer
{
    // Unique in its animator; code names layers by it.
    std::string name = std::string(baseLayerName);
    // The state the layer starts in.
    std::string entry;
    std::vector<AnimatorState> states;
    // Checked in this order: the first one whose conditions are met is taken.
    std::vector<AnimatorTransition> transitions;
    AnimatorLayerBlend blending = AnimatorLayerBlend::Override;
    // How much of its pose shows, from 0 to 1, until code sets another; a weight parameter, when
    // named, multiplies it.
    float weight = 1.0f;
    std::string weightParameter;
    // The bones the layer poses, by name, each with the bones under it, and those it leaves out, with
    // the bones under them: the closest of a bone and the bones above it that a list names decides.
    // A bone that neither names, nor any bone above it, is in the mask when no bone is put in it:
    // with both lists empty, the mask holds the whole skeleton.
    std::vector<std::string> mask;
    std::vector<std::string> maskExcluded;
    // Where the Entry and Any State nodes sit in the graph of the editor.
    math::Vec2 entryPosition{-240.0f, 0.0f};
    math::Vec2 anyStatePosition{-240.0f, 120.0f};

    [[nodiscard]] const AnimatorState* findState(std::string_view stateName) const noexcept;

    bool operator==(const AnimatorLayer&) const = default;
};

struct DEVEX_API AnimatorData
{
    std::vector<AnimatorParameter> parameters;
    // The base layer first, then the layers that play over it, in the order they do.
    std::vector<AnimatorLayer> layers{AnimatorLayer{}};

    [[nodiscard]] const AnimatorParameter* findParameter(std::string_view name) const noexcept;
    // The index of a layer by its name; an empty name is the base layer.
    [[nodiscard]] std::optional<std::size_t> findLayer(std::string_view name) const noexcept;

    bool operator==(const AnimatorData&) const = default;
};

[[nodiscard]] DEVEX_API std::string_view toString(AnimatorParameterType type) noexcept;
[[nodiscard]] DEVEX_API std::optional<AnimatorParameterType> parseAnimatorParameterType(std::string_view text) noexcept;
[[nodiscard]] DEVEX_API std::string_view toString(AnimatorBlend blend) noexcept;
[[nodiscard]] DEVEX_API std::optional<AnimatorBlend> parseAnimatorBlend(std::string_view text) noexcept;
[[nodiscard]] DEVEX_API std::string_view toString(AnimatorTest test) noexcept;
[[nodiscard]] DEVEX_API std::optional<AnimatorTest> parseAnimatorTest(std::string_view text) noexcept;
[[nodiscard]] DEVEX_API std::string_view toString(AnimatorLayerBlend blending) noexcept;
[[nodiscard]] DEVEX_API std::optional<AnimatorLayerBlend> parseAnimatorLayerBlend(std::string_view text) noexcept;

// Whether the mask of a layer names a bone: true when it puts it in, false when it leaves it out,
// nothing when it names it in neither list, and the bone takes what its parent has.
[[nodiscard]] DEVEX_API std::optional<bool> maskNames(const AnimatorLayer& layer, std::string_view bone) noexcept;

// Whether a test applies to a type of parameter: greater and less to numbers, equals to integers,
// is true and is false to bools, triggered to triggers.
[[nodiscard]] DEVEX_API bool testFits(AnimatorTest test, AnimatorParameterType type) noexcept;

// A base layer, layers and parameters with unique names that are not empty, states with unique names
// in their layer, an entry state that exists in a layer with states, transitions between existing
// states of their layer, conditions, blends and weights on existing parameters of a fitting type,
// weights from 0 to 1 and durations that are not negative.
[[nodiscard]] DEVEX_API core::Result<void> validate(const AnimatorData& animator);

} // namespace devex::asset
