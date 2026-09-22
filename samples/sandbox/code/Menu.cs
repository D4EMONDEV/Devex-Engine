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
        // The buttons change the volume of the whole game, which every group follows.
        if (Ui.WasClicked("louder"))
        {
            Audio.SetGroupVolume(Audio.Master, MathF.Min(Audio.GetGroupVolume(Audio.Master) + 0.1f, 1.0f));
        }
        if (Ui.WasClicked("softer"))
        {
            Audio.SetGroupVolume(Audio.Master, MathF.Max(Audio.GetGroupVolume(Audio.Master) - 0.1f, 0.0f));
        }
        if (Ui.WasClicked("back") || Ui.WasCancelled())
        {
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
