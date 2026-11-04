using System.Runtime.InteropServices;

namespace Devex;

/// <summary>How far a game is into a session.</summary>
public enum MultiplayerState
{
    /// <summary>In no session.</summary>
    Offline = 0,
    /// <summary>Waiting for the host to answer.</summary>
    Connecting = 1,
    /// <summary>In a session, as its host or as a client.</summary>
    Online = 2,
}

/// <summary>
/// The network of the game: a player hosts a session, and the others join it by its address. The host is
/// always player 1. A session ends with the game, or with <see cref="Leave"/>.
/// </summary>
public static unsafe class Multiplayer
{
    // The kinds of events of the engine, net::NetworkEvent::Kind.
    private const int ConnectedEvent = 0;
    private const int ConnectionFailedEvent = 1;
    private const int DisconnectedEvent = 2;
    private const int PeerJoinedEvent = 3;
    private const int PeerLeftEvent = 4;
    private const int RpcEvent = 6;

    /// <summary>The number of the host in every session.</summary>
    public const uint HostPeer = 1;

    /// <summary>As the destination of a call: every other player of the session.</summary>
    public const uint Everyone = 0;

    /// <summary>This game joined the session it was joining; <see cref="LocalPeer"/> is known from now on.</summary>
    public static event Action? Connected;

    /// <summary>The host could not be reached, or refused this game.</summary>
    public static event Action? ConnectionFailed;

    /// <summary>The session ended for this game: the host left, or stopped answering.</summary>
    public static event Action? Disconnected;

    /// <summary>Another player joined the session.</summary>
    public static event Action<uint>? PeerJoined;

    /// <summary>Another player left the session.</summary>
    public static event Action<uint>? PeerLeft;

    /// <summary>Starts a session as its host; false when it cannot be, with the reason in the log.</summary>
    public static bool Host(int port, int maxClients = 16)
    {
        return Bootstrap.Native.MultiplayerHost(port, maxClients) != 0;
    }

    /// <summary>
    /// Starts joining a session; false when it cannot start, with the reason in the log.
    /// <see cref="Connected"/> or <see cref="ConnectionFailed"/> tells how it went.
    /// </summary>
    public static bool Join(string address, int port)
    {
        nint text = Marshal.StringToCoTaskMemUTF8(address);
        try
        {
            return Bootstrap.Native.MultiplayerJoin((byte*)text, port) != 0;
        }
        finally
        {
            Marshal.FreeCoTaskMem(text);
        }
    }

    /// <summary>Ends the session for this game, telling the others. A host leaving ends it for everyone.</summary>
    public static void Leave()
    {
        Bootstrap.Native.MultiplayerLeave();
    }

    /// <summary>How far this game is into a session.</summary>
    public static MultiplayerState State => (MultiplayerState)Bootstrap.Native.MultiplayerState();

    /// <summary>Whether this game hosts the session.</summary>
    public static bool IsHost => Bootstrap.Native.MultiplayerIsHost() != 0;

    /// <summary>This player's number, 0 until it is online.</summary>
    public static uint LocalPeer => Bootstrap.Native.MultiplayerLocalPeer();

    /// <summary>The other players of the session.</summary>
    public static uint[] Peers
    {
        get
        {
            int count = Bootstrap.Native.MultiplayerPeers(null, 0);
            var peers = new uint[count];
            fixed (uint* first = peers)
            {
                Bootstrap.Native.MultiplayerPeers(first, count);
            }
            return peers;
        }
    }

    /// <summary>Who called the remote method that is running; 0 outside of one.</summary>
    public static uint RemoteSender { get; internal set; }

    // The number this game calls with: its own, or the host's when it plays alone.
    internal static uint Caller => LocalPeer != 0 ? LocalPeer : HostPeer;

    /// <summary>
    /// The player that decides for an entity and what is under it, and alone may call its methods unless
    /// they say <see cref="RpcAttribute.AnyPeer"/>: the host, unless said otherwise. The host gives it, and
    /// every player follows, those who come later included; a client cannot give it. An entity goes back to
    /// the host when its player leaves, and the session forgets them when it ends.
    /// </summary>
    public static void SetAuthority(Entity entity, uint peer)
    {
        Bootstrap.Native.MultiplayerSetAuthority(GameRuntime.CurrentScene.Pointer, entity, peer);
    }

    /// <summary>The player that decides for an entity.</summary>
    public static uint AuthorityOf(Entity entity)
    {
        Uuid uuid = entity.Uuid;
        return Bootstrap.Native.MultiplayerAuthority(&uuid);
    }

    /// <summary>Whether this game decides for an entity; always, outside a session.</summary>
    public static bool IsAuthority(Entity entity)
    {
        return State != MultiplayerState.Online || AuthorityOf(entity) == LocalPeer;
    }

    // Once a frame, when the components are loaded and before their updates, so that what handlers
    // change in them stays. Handlers do not outlive the game that subscribed them: see ForgetEvents.
    internal static void RaiseEvents()
    {
        int kind;
        uint peer;
        for (int index = 0; Bootstrap.Native.MultiplayerEvent(index, &kind, &peer) != 0; ++index)
        {
            switch (kind)
            {
                case ConnectedEvent:
                    Connected?.Invoke();
                    break;
                case ConnectionFailedEvent:
                    ConnectionFailed?.Invoke();
                    break;
                case DisconnectedEvent:
                    Disconnected?.Invoke();
                    break;
                case PeerJoinedEvent:
                    PeerJoined?.Invoke(peer);
                    break;
                case PeerLeftEvent:
                    PeerLeft?.Invoke(peer);
                    break;
                case RpcEvent:
                    GameRuntime.ReceiveRpc(index, peer);
                    break;
            }
        }
    }

    internal static void ForgetEvents()
    {
        Connected = null;
        ConnectionFailed = null;
        Disconnected = null;
        PeerJoined = null;
        PeerLeft = null;
        RemoteSender = 0;
    }
}
