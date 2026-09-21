# Devex Engine

Devex est un moteur de jeu open source conçu d'abord pour la 3D, écrit en C++ moderne
et rendu avec Vulkan. Il est distribué sous licence [MIT](LICENSE).

## Direction technique

- C++23 (C++26 en expérimentation), CMake + Ninja, dépendances via vcpkg ;
- Vulkan 1.4 (volk + VMA), shaders Slang, cible de rendu forward+ clustered PBR ;
- SDL3 pour la fenêtre et les entrées, Windows d'abord avec un code portable ;
- scènes en entités + composants, stockées de façon data-oriented ;
- repère Y-up main droite, formats de projet texte `.dvx*` ;
- gameplay en C++ (composants et systèmes) **ou en C#** (.NET hébergé), compilé et rechargé à
  chaud par l'éditeur ;
- éditeur Dear ImGui (docking) au style inspiré de Godot : gestionnaire de projets, onglets de
  scènes, viewport, gizmos et mode Play ; UI maison plus tard.

Le détail, l'architecture des modules et les jalons sont dans
[docs/decisions.md](docs/decisions.md).

## État actuel

- `Devex::Core` : `Result`/`Error`, journal `DEVEX_LOG_*`, assertions, `SlotMap`, `Uuid`,
  hachage XXH64, pool de jobs ;
- `Devex::Math` : types GLM sous `devex::math`, projection reverse-Z infinie, TRS ;
- `Devex::Platform` : fenêtre, événements et entrées clavier/souris, dialogues de fichiers,
  bibliothèques partagées et processus sur SDL3 ;
- `Devex::Reflection` : description des champs des composants (`DEVEX_REFLECT`), listes et
  références d'entités comprises ;
- `Devex::Serialization` : format texte commun des fichiers `.dvx*`, flux binaires ;
- `Devex::Asset` : `AssetId`, maillages, textures, matériaux, modèles, clips audio et animations, fichiers `.dvxasset`,
  projets `.dvxproj`, paquets de jeux exportés `.dvxpak` ;
- `Devex::AssetImport` : base d'assets (`.dvxmeta`, cache `.devex/`, imports en arrière-plan,
  réimport à chaud), importeurs de textures (BC7/BC5), de `.dvxmat`, de glTF, de scènes et de sons
  (WAV, FLAC, MP3, Ogg Vorbis) ;
- `Devex::Render` : renderer Vulkan 1.4 (volk, VMA), shaders Slang, render graph, PBR
  forward+ clustered, ombres en cascades, ciel HDR et IBL, MSAA, exposition automatique,
  tonemapping AgX, rendu dans une texture, sélection sur le GPU, contours et lignes d'outils ;
- `Devex::Scene` : entités à UUID, composants en sparse sets, hiérarchie, `.dvxscene`,
  instanciation de modèles, composants de physique, préfabs liés (scènes imbriquées avec leurs
  modifications) ;
- `Devex::Physics` : simulation Jolt Physics des corps rigides, colliders (primitives, maillages,
  déclencheurs) et personnages, couches de collision, requêtes, forces, contacts, interpolation ;
- `Devex::Audio` : miniaudio, sons spatialisés ou 2D, sources et écouteur dans la scène (sinon la
  caméra principale), lecture ponctuelle par le code, groupes de volume, clips décodés au
  chargement ou pendant la lecture ;
- `Devex::Animation` : clips d'animation importés des modèles glTF, squelettes faits d'entités,
  `Animator` qui joue un clip avec fondu croisé, root motion optionnel, skinning des maillages
  dans le vertex shader ;
- `Devex::Tools` : interface ImGui au thème réglable inspiré de Godot (Noto Sans, JetBrains Mono,
  icônes Lucide) : arbre de la scène, inspecteur, FileSystem, sortie, statistiques, annulation, en
  overlay (F1) ou dans l'éditeur : gestionnaire de projets, onglets de scènes, viewport et sa
  barre d'outils, caméra libre, sélection à la souris, gizmos, préfabs (glisser-déposer, valeurs
  modifiées, Revert, Make Local, Save as Prefab, mise à jour en direct), fichiers de code du projet
  avec éditeur de texte intégré à onglets (coloration syntaxique, numéros de ligne, recherche et
  remplacement, autocomplétion, erreurs de compilation dans la marge), ouverture dans l'IDE,
  *New Script…*, réglages de l'éditeur, aperçu sonore et forme
  d'onde des clips, volumes du projet, icônes et distances des sources audio, panneau Animation
  avec piste temporelle et images clés ;
- `Devex::Runtime` : `Application`, boucle à pas fixe, mode éditeur et mode Play, modules de jeu
  (composants et systèmes rechargeables à chaud), code C# sur .NET hébergé (composants, systèmes,
  compilation et rechargement à chaud), chargement des assets à la demande, changement de
  scène, rendu automatique de la scène, export d'un jeu ;
- `Devex.Managed` : l'API C# du moteur (`Component`, `Entity`, `Scene`, `Input`, `Physics`, `Audio`, `Animation`,
  `Prefabs`, `Assets`, `Time`, `Log`, maths) et les vues des composants du moteur, compilée dans
  `bin/managed` quand le SDK .NET est installé ;
- `Devex::Engine` : tous les modules dans une bibliothèque partagée, `devex-engine.dll` ;
- `devex-editor` : l'éditeur, qui compile et recharge à chaud le code des projets ;
- `devex-player` : lance un projet hors de l'éditeur, ou le paquet d'un jeu exporté (scène de
  démarrage, réglages de fenêtre et code du jeu) ;
- `devex-bindgen` : outil de build qui génère les vues C# des composants C++ ;
- `samples/sandbox` : le bac à sable, un projet dont le gameplay mêle un module C++ et du C# dans
  `code/` :
  la scène `arena` (scène de démarrage), où un personnage marche, saute, lance des balles et
  renverse des caisses entre rampe, marches, plateforme mobile et zones qui allument des lampes,
  faite en partie de préfabs (`assets/prefabs` : caisse, pyramide de caisses, balle, zone de
  lampe) ; le C# y ajoute des cibles qui comptent les balles reçues et font clignoter leur lampe
  (`code/Target.cs`, avec le score), une porte qui s'ouvre quand le joueur approche
  (`code/Door.cs`), un distributeur de caisses (`code/Dispenser.cs`) et un cube qui flotte
  (`code/Bobber.cs`) ; le lanceur C++ et les cibles C# jouent leurs sons, la porte sa source audio,
  le cube flottant émet un bourdonnement spatialisé et une ambiance Ogg tourne en boucle dans le
  groupe Music ; un robot rigué patrouille, attend et salue le joueur (`code/Robot.cs`) ;
  Tab passe à l'autre scène ; et la
  scène `sandbox` (caisse et balises glTF, sphères or et plastique, ciel HDR, plateau tournant, jour
  et nuit avec N).

Le SDK Vulkan fournit `slangc`, qui compile les shaders pendant le build. Les assets
d'exemple et les données de test sont produits par `scripts/generate_sample_assets.py`.
L'option `--audio-only` régénère seulement les sons ; leur encodage en Ogg, MP3 et FLAC demande
`ffmpeg` dans le `PATH`.

Les tests marqués `[gpu]` ouvrent une fenêtre masquée et nécessitent un GPU Vulkan 1.4.

## Construire

Prérequis : Visual Studio 2026 (C++), CMake 4, Ninja, [vcpkg](https://vcpkg.io) et le
SDK Vulkan. CMake utilise `VCPKG_ROOT` s'il est défini, sinon le `vcpkg` trouvé dans le
`PATH` ; les dépendances sont installées automatiquement au premier `cmake --preset`. Le
[SDK .NET 10](https://dotnet.microsoft.com/download) est facultatif : il n'est nécessaire que pour
écrire du gameplay en C#, et sans lui le moteur se construit et tourne normalement.

Depuis un terminal développeur Visual Studio :

```powershell
cmake --preset x64-debug
cmake --build --preset build-x64-debug
ctest --preset test-x64-debug
```

Les programmes sont produits dans `out/build/x64-debug/bin` ; dans l'arène du bac à sable, un clic
capture la souris, ZQSD (WASD) marchent, Maj court, Espace saute, un clic lance une balle, C passe à
la caméra libre et Échap libère la souris :

```powershell
out/build/x64-debug/bin/devex-editor.exe                                # gestionnaire de projets
out/build/x64-debug/bin/devex-editor.exe samples/sandbox/Sandbox.dvxproj
out/build/x64-debug/bin/devex-player.exe samples/sandbox/Sandbox.dvxproj
```

*Project > Export Game…* produit un dossier qui tourne seul : l'exécutable du jeu, son paquet
d'assets, son code et les bibliothèques du moteur. Un export en Release demande un build Release du
moteur (`cmake --build --preset build-x64-release`). La même chose sans fenêtre :

```powershell
out/build/x64-debug/bin/devex-editor.exe --export samples/sandbox/Sandbox.dvxproj
samples/sandbox/export/windows/Sandbox.exe
```

À l'ouverture d'un projet qui a un dossier `code/`, l'éditeur le compile en arrière-plan (Visual
Studio avec ses outils C++ est nécessaire, trouvé automatiquement), puis le recompile et le
recharge à chaque fichier enregistré, même pendant une partie. Après une mise à jour du moteur,
l'accueil signale *Code update required* ; *Update & Edit* reconstruit automatiquement le module
dans un nouveau cache en conservant les sources et les scènes. Play attend une compilation réussie.
Le code d'un jeu déclare des composants et des systèmes :

```cpp
struct Spinner { float speed = 1.0f; };
DEVEX_DECLARE_REFLECTION(Spinner);
DEVEX_REFLECT(Spinner) { type.field("speed", &Spinner::speed); }

void spin(devex::runtime::SystemContext& context)
{
    for (auto [entity, spinner, transform] : context.scene.view<Spinner, devex::scene::Transform>())
    {
        transform.rotation = devex::math::angleAxis(spinner.speed * float(context.delta.count()),
                                                    devex::math::Vec3{0, 1, 0}) * transform.rotation;
    }
}

DEVEX_GAME_MODULE(game)
{
    game.component<Spinner>();
    game.system("Spin", devex::runtime::SystemPhase::Update, &spin);
}
```

Le même composant en C# : un fichier `.cs` dans `code/` suffit, l'éditeur écrit le projet .NET,
compile avec le SDK .NET 10 et recharge à chaud. Les champs publics sont enregistrés dans la scène
et édités dans l'inspecteur, comme ceux d'un composant C++ ; *Add Component > New Script…* crée le
fichier, l'ouvre dans l'IDE et ajoute le composant dès qu'il compile :

```csharp
using Devex;

public class Spinner : Component
{
    // En radians par seconde, affiché en degrés dans l'inspecteur.
    [Angle]
    public float Speed = 1.0f;

    public override void Update(float delta)
    {
        Transform.Rotation = Quat.AngleAxis(Speed * delta, Vec3.Up) * Transform.Rotation;
    }
}
```

Un projet peut mélanger les deux : le C++ et le C# tournent dans la même frame, sur la même scène,
et le C# voit les composants C++, ceux du moteur comme ceux du jeu, par des vues générées :

```csharp
public class Target : Component
{
    public Entity Lamp;          // une autre entité, choisie dans l'inspecteur
    public List<Vec3> Spots = []; // une liste, éditée dans l'inspecteur
    public int Hits;

    public override void OnCollisionEnter(Entity other)
    {
        if (other.Has<Ball>())   // Ball est un composant C++ du jeu
        {
            ++Hits;
            Lamp.Get<PointLight>().Intensity = 6000.0f;   // un composant du moteur, changé sur place
            Prefabs.Instantiate(Assets.Find("res://assets/prefabs/crate.dvxscene")!.Value, Spots[0]);
        }
    }
}
```

Pour déboguer le C#, *Project > C# Debugging...* donne le processus auquel attacher Visual Studio,
Rider ou VS Code, et peut faire attendre Play jusqu'à ce qu'un débogueur soit attaché. Un jeu exporté
qui contient du C# emporte son runtime .NET, sans rien à installer chez le joueur.

Dans l'éditeur : clic gauche pour sélectionner, Q / W / E / R pour sélectionner, déplacer, tourner
ou mettre à l'échelle (Ctrl aimante), clic droit maintenu + ZQSD pour voler, Alt + clic gauche pour
tourner autour, F pour cadrer, Ctrl+S pour enregistrer la scène, Ctrl+N / Ctrl+W pour ouvrir ou
fermer un onglet de scène et F5 pour jouer (F8 arrête). Le thème, l'échelle et les polices se
règlent dans *Editor > Editor Settings*.

Les polices (Noto Sans, JetBrains Mono : SIL OFL 1.1) et les icônes (Lucide : ISC) de l'éditeur sont
dans `third_party` avec leurs licences.

Avec Visual Studio en français sans le pack de langue anglais, la configuration fait passer le
compilateur par un petit lanceur qui traduit ses notes `/showIncludes` pour Ninja (voir
[docs/decisions.md](docs/decisions.md), section *Code C++*) ; un build configuré avant cette
correction doit être reconfiguré puis reconstruit une fois entièrement.

## Règle de conception

Chaque système est un module CMake (`Devex::<Module>`, une bibliothèque d'objets) avec son API
publique sous `engine/include/devex/<module>/` et son implémentation privée sous
`engine/src/<module>/` ; tous forment la bibliothèque partagée `Devex::Engine`, que lient les
programmes, les tests et les jeux. Le projet `samples/sandbox` sert à intégrer et observer le
système ; les tests protègent son comportement indépendamment du rendu.
