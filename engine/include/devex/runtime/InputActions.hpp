#pragma once

#include <devex/asset/Project.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/InputSource.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace devex::runtime {

// The actions of a game ("Jump", "Move"), read from the devices once per frame, which game code
// asks for instead of keys. Every device plays: the keyboard and the mouse, and any gamepad.
// Actions are named in the project settings; the player may bind them to other keys, which
// writeOverrides and readOverrides keep from one game to the next.
class InputActions
{
public:
    // The text a key shows under the layout of the keyboard; its name when none is given.
    using KeyLabeler = std::function<std::string(platform::Key)>;

    InputActions() = default;
    explicit InputActions(const asset::InputSettings& settings);

    // Takes the actions and contexts of a project; bindings that cannot be read are logged and
    // left out, and the bindings of the player are forgotten.
    void setSettings(const asset::InputSettings& settings);
    void setKeyLabeler(KeyLabeler labeler);

    // Reads the devices, once per frame before the updates. The keyboard counts for nothing while
    // text is typed into the interface.
    void update(const platform::Input& input, bool typing);

    [[nodiscard]] bool hasAction(std::string_view action) const;
    // A button held down; an axis or a vector pushed at least halfway.
    [[nodiscard]] bool isDown(std::string_view action) const;
    // During the frame the action went down, or up. Read them from Update rather than FixedUpdate,
    // as the transitions of Input.
    [[nodiscard]] bool wasPressed(std::string_view action) const;
    [[nodiscard]] bool wasReleased(std::string_view action) const;
    // From -1 to 1 for an axis, 0 or 1 for a button, 0 for a vector.
    [[nodiscard]] float axis(std::string_view action) const;
    // A direction of length 1 at most for a vector, with y up; an axis along x; zero for a button.
    [[nodiscard]] math::Vec2 vector(std::string_view action) const;

    // An action of a context that is not active reads as released and centred. Contexts start as
    // the project says. Returns false for an unknown context.
    bool setContextActive(std::string_view context, bool active);
    [[nodiscard]] bool isContextActive(std::string_view context) const;

    // The bindings of an action as the player has them, in the order of the project.
    [[nodiscard]] std::size_t bindingCount(std::string_view action) const;
    [[nodiscard]] std::optional<platform::InputSource> binding(std::string_view action, std::size_t index) const;
    // What to show for a binding: the key as the keyboard prints it, or the name of the control.
    [[nodiscard]] std::string bindingLabel(std::string_view action, std::size_t index) const;
    // Binds the action to another control, in place of one of its bindings.
    bool rebind(std::string_view action, std::size_t index, platform::InputSource source);
    // Waits for the next key or button the player presses, and binds it in place of one binding:
    // a binding on the keyboard or the mouse takes a key or a mouse button, one on a gamepad takes
    // a gamepad button. Escape gives up. Actions read as released while it waits.
    bool listen(std::string_view action, std::size_t index);
    [[nodiscard]] bool isListening() const noexcept;
    void stopListening() noexcept;
    // Whether a key or a button answered the binding waited for, or gave it up, during this frame:
    // it does nothing else, such as pressing the button of the interface that has the focus.
    [[nodiscard]] bool tookInput() const noexcept;
    // Every binding back to the project's.
    void resetBindings();

    // Whether the player changed bindings since the last call, and they should be saved.
    [[nodiscard]] bool takeChanges() noexcept;
    // The bindings that differ from the project's, as the text of a file.
    [[nodiscard]] std::string writeOverrides() const;
    // Takes back bindings written by writeOverrides; those whose action or binding the project no
    // longer has are skipped.
    void readOverrides(std::string_view text);

private:
    struct StringHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const noexcept
        {
            return std::hash<std::string_view>{}(text);
        }
    };

    struct Binding
    {
        // Nothing for a binding of the project that could not be read.
        std::optional<platform::InputSource> source;
        std::optional<platform::InputSource> projectSource;
        asset::InputDirection direction = asset::InputDirection::Positive;
    };

    struct Action
    {
        std::string name;
        asset::InputActionKind kind = asset::InputActionKind::Button;
        // Index of the context, or nothing for an action that is always read.
        std::optional<std::size_t> context;
        float deadZone = 0.0f;
        std::vector<Binding> bindings;
        bool down = false;
        bool pressed = false;
        bool released = false;
        float axis = 0.0f;
        math::Vec2 vector{0.0f};
    };

    struct Context
    {
        std::string name;
        bool active = true;
    };

    struct Listening
    {
        std::size_t action = 0;
        std::size_t binding = 0;
        // Which devices may answer.
        bool keyboard = true;
        bool gamepad = true;
    };

    [[nodiscard]] const Action* find(std::string_view action) const;
    [[nodiscard]] Action* find(std::string_view action);
    void evaluate(Action& action, const platform::Input& input, bool typing, bool active) const;
    // The control pressed this frame that answers the binding waited for, if any.
    [[nodiscard]] std::optional<platform::InputSource> answer(const platform::Input& input) const;

    std::vector<Action> m_actions;
    std::unordered_map<std::string, std::size_t, StringHash, std::equal_to<>> m_actionIndices;
    std::vector<Context> m_contexts;
    std::optional<Listening> m_listening;
    KeyLabeler m_keyLabeler;
    bool m_changed = false;
    bool m_tookInput = false;
};

} // namespace devex::runtime
