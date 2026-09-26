using Devex;

// The interface of the sandbox: a main menu, a settings panel, a pause menu and a HUD, all built
// from Canvas and UiRect entities in the scene. This script only shows one of them at a time and
// answers the buttons by the action they carry.
public class MenuController : Component
{
    // The four screens of the interface, each the root of its own elements.
    public Entity MainMenu;
    public Entity Settings;
    public Entity PauseMenu;
    public Entity Hud;

    // The two texts of the HUD, written every frame while the game plays.
    public Entity ScoreText;
    public Entity TimerText;

    // The settings the player changes: the slider of the volume, the name that was typed, and the
    // text of the button that binds Jump to another key.
    public Entity VolumeSlider;
    public Entity NameField;
    public Entity JumpKey;

    // The name the player answered, kept for the game to greet them with.
    public string PlayerName = "";

    // Ten points for every click that lands in the world rather than on the interface.
    public int Score;

    private float _time;
    private bool _playing;

    public override void Start()
    {
        _playing = false;
        _time = 0.0f;
        Score = 0;
        Show(MainMenu, true);
        Show(Settings, false);
        Show(PauseMenu, false);
        Show(Hud, false);
    }

    public override void Update(float delta)
    {
        // The game only moves while no menu is open.
        Input.SetContextActive("Gameplay", !IsVisible(MainMenu) && !IsVisible(Settings) && !IsVisible(PauseMenu));
        if (Ui.WasClicked("quit"))
        {
            Game.Quit();
            return;
        }
        if (IsVisible(Settings))
        {
            UpdateSettings();
            return;
        }
        if (IsVisible(MainMenu))
        {
            UpdateMainMenu();
            return;
        }
        if (IsVisible(PauseMenu))
        {
            UpdatePauseMenu();
            return;
        }
        UpdateGame(delta);
    }

    private void UpdateMainMenu()
    {
        if (Ui.WasClicked("play"))
        {
            StartPlaying();
        }
        else if (Ui.WasClicked("settings"))
        {
            Show(MainMenu, false);
            Show(Settings, true);
        }
    }

    private void UpdateSettings()
    {
        // The slider drives the volume of the whole game, which every group follows. The value
        // itself lives in the component: the interface only says that it moved.
        if (Ui.WasChanged("volume") && VolumeSlider.IsAlive &&
            VolumeSlider.TryGet(out UiSlider volume))
        {
            Audio.SetGroupVolume(Audio.Master, Math.Clamp(volume.Value / 100.0f, 0.0f, 1.0f));
        }
        // Enter ends the edit of the field, and hands over what was typed.
        if (Ui.WasSubmitted("name") && NameField.IsAlive && NameField.TryGet(out UiText typed))
        {
            PlayerName = typed.Text;
            Log.Info($"Bonjour {PlayerName}");
        }
        // The key of Jump: the button waits for the next key, which the player keeps from one game
        // to the next. Escape gives up, and does nothing else.
        if (Ui.WasClicked("bind_jump"))
        {
            Input.ListenForBinding("Jump", 0);
        }
        else if (Ui.WasClicked("reset_keys"))
        {
            Input.ResetBindings();
        }
        if (JumpKey.IsAlive && JumpKey.TryGet(out UiText key))
        {
            key.Text = Input.IsListeningForBinding ? "Appuyez sur une touche" : Input.BindingLabel("Jump", 0);
        }
        if (Ui.WasClicked("back") || Ui.WasCancelled())
        {
            Input.StopListeningForBinding();
            Show(Settings, false);
            Show(MainMenu, true);
        }
    }

    private void UpdatePauseMenu()
    {
        if (Ui.WasClicked("resume") || Ui.WasCancelled())
        {
            Show(PauseMenu, false);
            Show(Hud, true);
            Input.MouseCaptured = false;
        }
        else if (Ui.WasClicked("menu"))
        {
            _playing = false;
            Show(PauseMenu, false);
            Show(MainMenu, true);
        }
    }

    private void UpdateGame(float delta)
    {
        if (!_playing)
        {
            return;
        }
        if (Ui.WasCancelled())
        {
            Show(Hud, false);
            Show(PauseMenu, true);
            return;
        }
        // A click that lands on the interface belongs to it; only the rest counts.
        if (!Ui.PointerOverInterface && Input.WasMouseButtonPressed(MouseButton.Left))
        {
            Score += 10;
        }

        _time += delta;
        if (ScoreText.IsAlive && ScoreText.TryGet(out UiText score))
        {
            score.Text = $"Score {Score}";
        }
        if (TimerText.IsAlive && TimerText.TryGet(out UiText timer))
        {
            timer.Text = $"{(int)(_time / 60.0f)}:{(int)(_time % 60.0f):00}";
        }
    }

    private void StartPlaying()
    {
        _playing = true;
        _time = 0.0f;
        Score = 0;
        Show(MainMenu, false);
        Show(Hud, true);
    }

    // Shows or hides a whole screen: hiding its rectangle hides everything under it.
    private static void Show(Entity entity, bool visible)
    {
        if (entity.IsAlive && entity.TryGet(out UiRect rect))
        {
            rect.Visible = visible;
        }
    }

    private static bool IsVisible(Entity entity)
        => entity.IsAlive && entity.TryGet(out UiRect rect) && rect.Visible;
}
