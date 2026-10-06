using Devex;

public class FinishZone : Component
{
    public Entity Player;
    private bool _won;

    public override void OnTriggerEnter(Entity other)
    {
        if (_won || !Player.IsAlive || other != Player)
            return;

        _won = true;
        Log.Info("Victoire ! Tu as atteint l'arrivee.");
    }
}
