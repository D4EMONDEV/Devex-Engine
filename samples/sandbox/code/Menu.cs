using Devex;

// What the sandbox saves besides its scene, whose entities keep the rest: the time of the game,
// which the controller keeps to itself, and the score shown in the list of saves.
public class SandboxSave
{
    public float Time;
    public int Score;
}

// The interface of the sandbox: a main menu, a settings panel, a pause menu and a HUD, all built
// from Canvas and UiRect entities in the scene. This script only shows one of them at a time and
// answers the buttons by the action they carry. The settings the player changes stay from one game
// to the next, and the pause menu saves the game, which the main menu continues.
public class MenuController : Component
{
    // The only slot of the sandbox.
    private const string Slot = "1";

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
    public Entity FullscreenToggle;

    // The last save beside the main menu, its picture and what it holds; the button that continues
    // it, and the text of the one that saves.
    public Entity SavePanel;
    public Entity SaveImage;
    public Entity SaveInfo;
    public Entity ContinueButton;
    public Entity SaveLabel;

    // The name the player answered, kept for the game to greet them with.
    public string PlayerName = "";

    // Ten points for every click that lands in the world rather than on the interface.
    public int Score;

    private float _time;
    private bool _playing;
    // How long the save button still says the game was saved.
    private float _savedFor;

    public override void Start()
    {
        ReadSettings();
        // Back from a save: the scene is as it was saved, the score with it, and the game goes on.
        if (Saves.RestoredSlot is { } slot)
        {
            _time = Saves.Load<SandboxSave>(slot, restoreScene: false)?.Time ?? 0.0f;
            _playing = true;
            Show(MainMenu, false);
            Show(Settings, false);
            Show(PauseMenu, false);
            Show(Hud, true);
            return;
        }
        _playing = false;
        _time = 0.0f;
        Score = 0;
        Show(MainMenu, true);
        Show(Settings, false);
        Show(PauseMenu, false);
        Show(Hud, false);
        ShowLastSave();
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
        else if (Ui.WasClicked("continue") && Saves.Exists(Slot))
        {
            // The scene of the save replaces this one at the end of the frame, and starts again.
            Saves.Load<SandboxSave>(Slot);
        }
        else if (Ui.WasClicked("settings"))
        {
            Show(MainMenu, false);
            Show(Settings, true);
        }
    }

    private void UpdateSettings()
    {
        // The slider drives the volume of the whole game, which every group follows; the player
        // keeps it for the next games. The value itself lives in the component: the interface only
        // says that it moved.
        if (Ui.WasChanged("volume") && VolumeSlider.IsAlive &&
            VolumeSlider.TryGet(out UiSlider volume))
        {
            PlayerSettings.SetVolume(Audio.Master, Math.Clamp(volume.Value / 100.0f, 0.0f, 1.0f));
        }
        if (Ui.WasChanged("fullscreen") && FullscreenToggle.IsAlive && FullscreenToggle.TryGet(out UiToggle fullscreen))
        {
            PlayerSettings.Fullscreen = fullscreen.Value;
        }
        // Enter ends the edit of the field, and hands over what was typed.
        if (Ui.WasSubmitted("name") && NameField.IsAlive && NameField.TryGet(out UiText typed))
        {
            PlayerName = typed.Text;
            PlayerSettings.SetString("player_name", PlayerName);
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
            ShowLastSave();
        }
    }

    private void UpdatePauseMenu()
    {
        _savedFor = Math.Max(_savedFor - Time.Delta, 0.0f);
        if (SaveLabel.IsAlive && SaveLabel.TryGet(out UiText label))
        {
            label.Text = _savedFor > 0.0f ? "Sauvegardé" : "Sauvegarder";
        }
        if (Ui.WasClicked("save"))
        {
            // The scene is saved as it plays, with a picture of it without the menus.
            Saves.Save(Slot, new SandboxSave { Time = _time, Score = Score }, label: $"Score {Score}");
            _savedFor = 1.5f;
        }
        else if (Ui.WasClicked("resume") || Ui.WasCancelled())
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
            ShowLastSave();
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

    // Puts the controls of the settings screen as the player left them.
    private void ReadSettings()
    {
        if (VolumeSlider.IsAlive && VolumeSlider.TryGet(out UiSlider volume))
        {
            volume.Value = PlayerSettings.GetVolume(Audio.Master) * 100.0f;
        }
        if (FullscreenToggle.IsAlive && FullscreenToggle.TryGet(out UiToggle fullscreen))
        {
            fullscreen.Value = PlayerSettings.Fullscreen;
        }
        PlayerName = PlayerSettings.GetString("player_name", PlayerName);
        if (NameField.IsAlive && NameField.TryGet(out UiText name))
        {
            name.Text = PlayerName;
        }
    }

    // The last save, its picture and what it holds, beside the main menu; Continuer only answers
    // when there is one.
    private void ShowLastSave()
    {
        SaveSlot? last = Saves.Find(Slot);
        Show(SavePanel, last != null);
        if (ContinueButton.IsAlive && ContinueButton.TryGet(out UiButton button))
        {
            button.Interactable = last != null;
        }
        if (last == null)
        {
            return;
        }
        if (SaveImage.IsAlive && SaveImage.TryGet(out UiImage image))
        {
            image.Texture = last.Thumbnail;
        }
        if (SaveInfo.IsAlive && SaveInfo.TryGet(out UiText info))
        {
            var played = TimeSpan.FromSeconds(last.PlayTime);
            info.Text = $"{last.Label}, le {last.Time:dd/MM à HH:mm}\n{(int)played.TotalMinutes} min {played.Seconds:00} s de jeu";
        }
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
