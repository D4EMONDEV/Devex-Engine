using Devex;

/// <summary>Hosts a session from its first update, and notes who comes and goes.</summary>
public sealed class NetworkProbe : Component
{
    public float Hosted;
    public float Joined;
    public float Left;
    public float LocalPeer;
    public float Peers;
    public float IsHost;
    public float Online;

    private bool _started;

    public override void Update(float delta)
    {
        if (!_started)
        {
            _started = true;
            Multiplayer.PeerJoined += peer => Joined = peer;
            Multiplayer.PeerLeft += peer => Left = peer;
            Hosted = Multiplayer.Host(47395, 4) ? 1 : 0;
        }
        LocalPeer = Multiplayer.LocalPeer;
        Peers = Multiplayer.Peers.Length;
        IsHost = Multiplayer.IsHost ? 1 : 0;
        Online = Multiplayer.State == MultiplayerState.Online ? 1 : 0;
    }
}
