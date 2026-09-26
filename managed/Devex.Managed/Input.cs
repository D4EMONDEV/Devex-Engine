namespace Devex;

/// <summary>A mouse button, as the engine numbers them.</summary>
public enum MouseButton
{
    Left = 0,
    Middle = 1,
    Right = 2,
    X1 = 3,
    X2 = 4,
}

/// <summary>Keyboard and mouse of the current frame. "Pressed" is true only on the frame of the press.</summary>
public static unsafe class Input
{
    public static bool IsKeyDown(Key key) => Bootstrap.Native.IsKeyDown((int)key) != 0;

    public static bool WasKeyPressed(Key key) => Bootstrap.Native.WasKeyPressed((int)key) != 0;

    public static bool IsMouseButtonDown(MouseButton button) => Bootstrap.Native.IsMouseButtonDown((int)button) != 0;

    public static bool WasMouseButtonPressed(MouseButton button)
        => Bootstrap.Native.WasMouseButtonPressed((int)button) != 0;

    /// <summary>How far the mouse moved since the previous frame, in pixels.</summary>
    public static Vec2 MouseDelta
    {
        get
        {
            float* values = stackalloc float[2];
            Bootstrap.Native.MouseDelta(values);
            return new Vec2(values[0], values[1]);
        }
    }

    /// <summary>Where the mouse is in the window, in pixels.</summary>
    public static Vec2 MousePosition
    {
        get
        {
            float* values = stackalloc float[2];
            Bootstrap.Native.MousePosition(values);
            return new Vec2(values[0], values[1]);
        }
    }

    /// <summary>A captured mouse is hidden and reports unbounded motion, as a first-person camera needs.</summary>
    public static bool MouseCaptured
    {
        get => Bootstrap.Native.IsMouseCaptured() != 0;
        set => Bootstrap.Native.SetMouseCaptured(value ? 1 : 0);
    }

    // Actions: what games read rather than keys, named in the project settings. The keyboard and
    // the mouse play them, as any gamepad does; an unknown action is an error.

    /// <summary>A button held down; an axis or a vector pushed at least halfway.</summary>
    public static bool IsActionDown(string action) => ActionState(action, 0);

    /// <summary>During the frame the action went down. Read it from Update rather than FixedUpdate.</summary>
    public static bool WasActionPressed(string action) => ActionState(action, 1);

    /// <summary>During the frame the action went up.</summary>
    public static bool WasActionReleased(string action) => ActionState(action, 2);

    /// <summary>From -1 to 1 for an axis, 0 or 1 for a button, 0 for a vector.</summary>
    public static float ActionAxis(string action)
    {
        using var text = new Utf8Buffer(action);
        float value = 0.0f;
        return Bootstrap.Native.ActionAxis(text.Pointer, &value) != 0 ? value : throw UnknownAction(action);
    }

    /// <summary>A direction of length 1 at most, with Y up; an axis along X; zero for a button.</summary>
    public static Vec2 ActionVector(string action)
    {
        using var text = new Utf8Buffer(action);
        float* values = stackalloc float[2];
        return Bootstrap.Native.ActionVector(text.Pointer, values) != 0 ? new Vec2(values[0], values[1]) : throw UnknownAction(action);
    }

    /// <summary>
    /// Turns a context of actions on or off, such as the actions of the game while a menu is open:
    /// the actions of a context that is off read as released. Contexts start as the project says,
    /// and keep their state from scene to scene.
    /// </summary>
    public static void SetContextActive(string context, bool active)
    {
        using var text = new Utf8Buffer(context);
        if (Bootstrap.Native.SetInputContextActive(text.Pointer, active ? 1 : 0) == 0)
        {
            throw new ArgumentException($"no input context {context}");
        }
    }

    public static bool IsContextActive(string context)
    {
        using var text = new Utf8Buffer(context);
        int active = Bootstrap.Native.IsInputContextActive(text.Pointer);
        return active >= 0 ? active != 0 : throw new ArgumentException($"no input context {context}");
    }

    /// <summary>How many bindings the action has, in the order of the project settings.</summary>
    public static int BindingCount(string action)
    {
        using var text = new Utf8Buffer(action);
        int count = Bootstrap.Native.BindingCount(text.Pointer);
        return count >= 0 ? count : throw UnknownAction(action);
    }

    /// <summary>What to show for a binding: the key as the keyboard prints it, or the name of the button.</summary>
    public static string BindingLabel(string action, int binding)
    {
        using var text = new Utf8Buffer(action);
        return Utf8.ToString(Bootstrap.Native.BindingLabel(text.Pointer, binding)) ?? string.Empty;
    }

    /// <summary>
    /// Waits for the next key or button the player presses and binds it to the action, in place of
    /// one of its bindings: a keyboard binding takes a key or a mouse button, a gamepad one a
    /// gamepad button. Escape gives up. The bindings are saved for the next games.
    /// </summary>
    public static void ListenForBinding(string action, int binding)
    {
        using var text = new Utf8Buffer(action);
        if (Bootstrap.Native.ListenForBinding(text.Pointer, binding) == 0)
        {
            throw new ArgumentException($"no binding {binding} for input action {action}");
        }
    }

    /// <summary>Whether a binding waits for a key.</summary>
    public static bool IsListeningForBinding => Bootstrap.Native.IsListeningForBinding() != 0;

    public static void StopListeningForBinding() => Bootstrap.Native.StopListeningForBinding();

    /// <summary>Every binding back to those of the project settings.</summary>
    public static void ResetBindings() => Bootstrap.Native.ResetBindings();

    private static bool ActionState(string action, int query)
    {
        using var text = new Utf8Buffer(action);
        int state = Bootstrap.Native.ActionState(text.Pointer, query);
        return state >= 0 ? state != 0 : throw UnknownAction(action);
    }

    private static ArgumentException UnknownAction(string action) => new($"no input action {action}");
}

