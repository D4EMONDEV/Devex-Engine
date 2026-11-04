# Faire un jeu multijoueur

Un joueur héberge une partie, et les autres la rejoignent par son adresse. Celui qui héberge est toujours le
joueur 1. Chaque entité a une **autorité**, le joueur qui la gère : l'hôte, tant qu'il ne l'a pas donnée à un
autre. Rien n'est prédit : chacun bouge tout de suite ce qu'il gère, et voit le reste avec un léger retard.

La connexion se fait par adresse IP ou en réseau local. Il n'y a ni chiffrement, ni relais pour traverser les
box Internet.

L'exemple complet est dans le bac à sable : la scène `assets/scenes/arena_online.dvxscene` et son code
`code/Online.cs`. Ouvrez-la, choisissez **2 players** avec la flèche à côté de Play, puis lancez Play.

## Essayer à plusieurs dans l'éditeur

La flèche à côté de **Play** lance jusqu'à quatre jeux côte à côte, déjà connectés entre eux : voir
[Tester à plusieurs](editor-play.md#tester-à-plusieurs).

## Héberger et rejoindre

```csharp
Multiplayer.Host(7777);                  // false si le port est pris
Multiplayer.Join("192.168.1.20", 7777);  // Connected ou ConnectionFailed dira comment ça s'est passé
Multiplayer.Leave();
```

`Multiplayer.State`, `IsHost`, `LocalPeer` et `Peers` disent où en est la partie. Les événements `Connected`,
`ConnectionFailed`, `Disconnected`, `PeerJoined` et `PeerLeft` préviennent de ce qui change. Un joueur muet depuis
30 secondes compte comme parti (`setSilenceLimit` en C++), ce qui laisse le temps de charger un niveau.

## Faire apparaître des objets chez tous

Posez un composant **NetworkSpawner** sur une entité. Tout prefab instancié sous elle par son autorité apparaît
chez tous les joueurs, et disparaît chez tous quand il est détruit. Le code reste celui d'un jeu solo :

```csharp
Entity player = Prefabs.Instantiate(playerPrefab, position, spawner);
Multiplayer.SetAuthority(player, peer);   // sur l'hôte : tous suivent
```

Ceux qui arrivent en cours de partie reçoivent ce qui existe déjà. Les objets posés dans la scène existent déjà
partout, mais un objet de la scène détruit en jeu ne disparaît pas chez les autres : passez par un spawner.

## Garder des champs identiques

Ajoutez un composant **NetworkSync** à l'entité, et cochez dans l'inspecteur les champs de ses composants, C++ ou
C#. La position et la rotation sont cochées d'origine. L'autorité de l'entité envoie, et les autres reçoivent.

- **Rate** : le nombre d'envois par seconde au plus, seulement quand quelque chose change. Les dernières valeurs
  arrivent à coup sûr.
- **Smoothing** : le retard avec lequel les autres voient le `Transform`, en secondes. Ils interpolent entre ce qu'ils
  reçoivent, pour un mouvement fluide même si des paquets se perdent. Les autres champs s'appliquent à l'arrivée.

Chez ceux qui n'ont pas l'autorité, un `RigidBody` dynamique ne simule plus et suit ce qui arrive, et un
`CharacterController` ne marche ni ne tombe. Le code de contrôle, lui, doit vérifier l'autorité :

```csharp
public override void Update(float delta)
{
    if (!IsAuthority)
    {
        return;
    }
    // Lire les touches et bouger le joueur.
}
```

La liste des champs doit être la même sur tous les jeux : c'est le cas quand ils jouent le même projet.

## Appeler une méthode chez les autres

`[Rpc]` sur une méthode d'un composant, avec le nom de la méthode pour l'appeler. L'appel retrouve la même entité sur
les autres jeux.

```csharp
[Rpc]                                  // seule l'autorité de l'entité peut l'appeler
private void Hit(int damage) { ... }

[Rpc(AnyPeer = true, CallLocal = true)]  // tout le monde peut l'appeler, et elle tourne aussi ici
private void Wave() { ... }

Rpc(nameof(Hit), 25);                  // chez tous les autres
RpcId(peer, nameof(Hit), 25);          // chez un seul
RpcId(Multiplayer.HostPeer, nameof(Hit), 25);
```

- **`Reliable = false`** : l'appel peut se perdre, ce qui convient à ce que le suivant remplace.
- **`Multiplayer.RemoteSender`** dit qui a appelé la méthode en cours.
- Les arguments possibles sont les booléens, nombres, enums, textes, vecteurs, quaternions, entités, identifiants et
  tableaux d'octets.

Un appel que la méthode n'autorise pas, ou qui ne correspond pas à ses paramètres, n'est pas envoyé, et le journal dit
pourquoi. Celui qui reçoit vérifie de nouveau : un tricheur n'obtient rien, et l'hôte ne transmet pas ce qu'il refuse.

Un joueur peut ainsi demander à l'hôte de faire quelque chose qu'il est seul à pouvoir faire. Dans l'exemple, seule
l'autorité du joueur peut lui demander de lancer une balle, que l'hôte simule ensuite pour tous :

```csharp
RpcId(Multiplayer.HostPeer, nameof(Throw), from, direction);

[Rpc(CallLocal = true)]   // CallLocal pour le joueur de l'hôte, dont l'appel reste ici
private void Throw(Vec3 from, Vec3 direction) { ... }
```

## L'autorité

`Multiplayer.SetAuthority(entité, joueur)` donne une entité et ce qui est sous elle à un joueur. Seul l'hôte le peut, et
tous suivent, y compris ceux qui arrivent après. Une entité revient à l'hôte quand son joueur part. `IsAuthority` dit
si ce jeu la gère, et `Multiplayer.AuthorityOf` qui la gère.

## En C++

Les composants sont les mêmes. Le reste passe par le contexte des systèmes :

```cpp
void hit(SystemContext& context, Entity entity, int damage) { ... }

DEVEX_GAME_MODULE(game)
{
    game.rpc("Player.Hit", &hit);
    game.rpc("Player.Wave", &wave, {.anyPeer = true, .callLocal = true});
}

devex::runtime::rpc(context, entity, "Player.Hit", 25);
devex::runtime::rpcId(context, peer, entity, "Player.Hit", 25);
devex::runtime::setAuthority(context.scene, *context.multiplayer, entity, peer);
context.multiplayer->host(7777);
```

Le nom d'une méthode C# est `Composant.Méthode`, et un appel peut aller d'un langage à l'autre, mais un nom appartient
à un seul langage. Vers l'autre langage, `callLocal` attend une image.

## Ce qui reste au jeu

- **Changer de scène** : la session continue, mais chaque jeu charge la scène lui-même, par un appel à distance par
  exemple. Pour un client, ce qui est apparu pendant la session disparaît avec elle.
- **Ce que le moteur ne synchronise pas** : ce qui n'a pas de `NetworkSync` vit sa vie sur chaque jeu. Dans l'exemple, la
  plateforme et le cube bougent selon le temps de chaque jeu.
