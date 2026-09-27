#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Animator controllers: the state machines Animator components play, as Unity's Animator
// Controller and Godot's AnimationTree. States play clips, blend trees or sprite animations, and
// transitions move between them when the parameters that game code sets meet their conditions.
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

struct AnimatorParameter
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
struct AnimatorMotion
{
    AssetId clip;
    // Along the line of a linear blend.
    float threshold = 0.0f;
    // On the plane of a planar blend.
    math::Vec2 position{0.0f};

    bool operator==(const AnimatorMotion&) const = default;
};

struct AnimatorState
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

struct AnimatorCondition
{
    std::string parameter;
    AnimatorTest test = AnimatorTest::Greater;
    float value = 0.0f;

    bool operator==(const AnimatorCondition&) const = default;
};

struct AnimatorTransition
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

struct AnimatorData
{
    // The state the animator starts in.
    std::string entry;
    std::vector<AnimatorParameter> parameters;
    std::vector<AnimatorState> states;
    // Checked in this order: the first one whose conditions are met is taken.
    std::vector<AnimatorTransition> transitions;
    // Where the Entry and Any State nodes sit in the graph of the editor.
    math::Vec2 entryPosition{-240.0f, 0.0f};
    math::Vec2 anyStatePosition{-240.0f, 120.0f};

    [[nodiscard]] const AnimatorState* findState(std::string_view name) const noexcept;
    [[nodiscard]] const AnimatorParameter* findParameter(std::string_view name) const noexcept;

    bool operator==(const AnimatorData&) const = default;
};

[[nodiscard]] std::string_view toString(AnimatorParameterType type) noexcept;
[[nodiscard]] std::optional<AnimatorParameterType> parseAnimatorParameterType(std::string_view text) noexcept;
[[nodiscard]] std::string_view toString(AnimatorBlend blend) noexcept;
[[nodiscard]] std::optional<AnimatorBlend> parseAnimatorBlend(std::string_view text) noexcept;
[[nodiscard]] std::string_view toString(AnimatorTest test) noexcept;
[[nodiscard]] std::optional<AnimatorTest> parseAnimatorTest(std::string_view text) noexcept;

// Whether a test applies to a type of parameter: greater and less to numbers, equals to integers,
// is true and is false to bools, triggered to triggers.
[[nodiscard]] bool testFits(AnimatorTest test, AnimatorParameterType type) noexcept;

// States and parameters with unique names that are not empty, an entry state that exists when there
// are states, transitions between existing states, conditions and blends on existing parameters of a
// fitting type, and durations that are not negative.
[[nodiscard]] core::Result<void> validate(const AnimatorData& animator);

} // namespace devex::asset
