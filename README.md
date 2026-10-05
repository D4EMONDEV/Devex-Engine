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
- éditeur au style inspiré de Godot : gestionnaire de projets, onglets de scènes, viewport,
  gizmos et mode Play ; il passe sur l'interface des jeux, pour n'avoir qu'un seul système
  d'interface : tous les panneaux, le dock (emplacements, onglets, barres) et les modales le sont,
  l'éditeur lit lui-même le clavier et la souris et décide qui les reçoit, et le renderer compose
  ses images avec le pipeline de cette interface, aux espacements du thème de Godot. Dear ImGui
  n'est plus une dépendance.

Le détail, l'architecture des modules et les jalons sont dans
[docs/decisions.md](docs/decisions.md).

## État actuel

- `Devex::Core` : `Result`/`Error`, journal `DEVEX_LOG_*`, assertions, `SlotMap`, `Uuid`,
  hachage XXH64, pool de jobs, profileur (zones nommées par thread, rassemblées par image) ;
- `Devex::Math` : types GLM sous `devex::math`, projection reverse-Z infinie, TRS ;
- `Devex::Platform` : fenêtre, événements et entrées clavier, souris et manette (sources nommées
  pour les actions), saisie de texte et presse-papiers, dialogues de fichiers, bibliothèques
  partagées et processus sur SDL3 ;
- `Devex::Reflection` : description des champs des composants (`DEVEX_REFLECT`), listes et
  références d'entités comprises ;
- `Devex::Serialization` : format texte commun des fichiers `.dvx*`, flux binaires ;
- `Devex::Asset` : `AssetId`, maillages (avec leur boîte englobante), textures, matériaux, modèles,
  clips audio, animations, polices (avec leur crénage), thèmes d'interface et courbes, fichiers
  `.dvxasset`, projets `.dvxproj`, paquets de jeux exportés `.dvxpak` ;
- `Devex::AssetImport` : base d'assets (`.dvxmeta`, cache `.devex/`, imports en arrière-plan,
  réimport à chaud), importeurs de textures (BC7/BC5), de `.dvxmat`, de modèles glTF, FBX et OBJ
  (ufbx, convertis en mètres et Y-up avec une échelle réglable), de scènes, de sons
  (WAV, FLAC, MP3, Ogg Vorbis), de polices (`.ttf`, `.otf` cuites en atlas de distances), de
  thèmes `.dvxtheme` et de courbes `.dvxcurve` ;
- `Devex::Render` : renderer Vulkan 1.4 (volk, VMA), shaders Slang, render graph, PBR
  forward+ clustered avec prépasse de profondeur, culling par tronc de vue, ombres en cascades pour
  le soleil et en atlas pour les lumières locales, ciel HDR et IBL, surfaces transparentes triées,
  anticrénelage temporel, occlusion ambiante en espace écran, bloom, exposition automatique,
  tonemapping AgX, table de couleurs, vignette et grain, particules et rubans (face à la caméra,
  étirés, doux au contact des surfaces, éclairés ou non, triés avec la transparence), sprites
  (simples, découpés en neuf, répétés, retournés, éclairés ou non, triés par couche et par
  ordre), cartes de tuiles en un lot, caméra orthographique, textures filtrées au pixel près,
  rendu dans
  une texture, sélection sur le
  GPU, contours et lignes d'outils, temps GPU de chaque passe, copies vers le GPU sans attente et
  dans un budget par image ;
- `Devex::Scene` : entités à UUID, composants en sparse sets, hiérarchie, `.dvxscene`,
  instanciation de modèles, composants de physique, d'animation et d'interface, cartes de
  tuiles (`Tilemap`, cellules par blocs, tilesets `.dvxtileset`), préfabs liés
  (scènes imbriquées avec leurs modifications) ;
- `Devex::Physics` : simulation Jolt Physics des corps rigides, colliders (primitives, maillages,
  déclencheurs) et personnages, couches de collision, requêtes, forces, contacts, interpolation ;
- `Devex::Physics2D` : simulation Box2D dans le plan XY des corps rigides 2D, colliders (boîte,
  cercle, capsule, polygone, déclencheurs, à sens unique), collider généré des tuiles et
  personnages « move and slide » (pentes, marches, corniches, plateformes mobiles, poussée),
  requêtes, forces et contacts, partagés avec le C# ;
- `Devex::Navigation` : Recast & Detour, maillage de navigation cuit depuis les colliders statiques
  (`.dvxnavmesh`, tuiles compressées), agents `NavMeshAgent` qui marchent en foule en s'évitant,
  obstacles `NavMeshObstacle` qui découpent le maillage pendant le jeu, chemins, échantillonnage
  et rayons le long du maillage, partagés avec le C# ;
- `Devex::Audio` : miniaudio, sons spatialisés ou 2D, sources et écouteur dans la scène (sinon la
  caméra principale), lecture ponctuelle par le code, groupes de volume, clips décodés au
  chargement ou pendant la lecture ;
- `Devex::Animation` : clips d'animation importés des modèles glTF et FBX, squelettes faits d'entités,
  `Animator` qui joue un clip avec fondu croisé ou une machine à états `.dvxanimator` (paramètres,
  transitions conditionnelles, arbres de mélange 1D et 2D, états de sprites), root motion
  optionnel, skinning des maillages
  dans le vertex shader ; tweens de tout champ numérique, vectoriel, de couleur ou de rotation
  d'un composant (délai, boucles, aller-retour, séquences), par code ou par le composant
  `Tweener`, avec 22 courbes classiques ou une courbe dessinée (`.dvxcurve`) ; sprites animés
  image par image (`SpriteAnimator` et animations nommées `.dvxframes`) ;
- `Devex::Particles` : émetteurs `ParticleEmitter` simulés sur les workers (émission par seconde,
  par mètre et en salves, formes, gravité, bruit, courbes sur la vie, collisions avec la physique,
  sous-émetteurs, espace du monde ou local) et traînées des particules et des entités
  (`TrailRenderer`) ;
- `Devex::Ui` : interfaces faites d'entités (`Canvas`, `UiRect`, `UiImage`, `UiText`, `UiButton`,
  `UiInput`, `UiSlider`, `UiToggle`, `UiScroll`, `UiLayout`, `UiBinding`, `UiNumberField`,
  `UiColorPicker`, `UiPlot`, `UiLine`, `UiTextArea`), placement par ancrages
  et marges puis par conteneurs, texte tiré d'un atlas de distances signées avec crénage et texte
  riche, champs de saisie avec sélection, presse-papiers et mot de passe, curseurs et cases à
  cocher, listes qui défilent et se découpent avec leur barre de défilement, images en neuf
  parts ou tirées d'un sprite d'atlas, graphes en ligne ou en barres, liaison d'un texte à un champ de composant, thèmes `.dvxtheme` de styles nommés, popups,
  menus et modales (`UiPopup`), menus contextuels au clic droit, infobulles, listes déroulantes,
  séparateurs déplaçables, dépliants, listes virtuelles et tableaux aux colonnes redimensionnables
  et triables, champs numériques glissés ou tapés (sommes comprises), sélecteur de couleur,
  double clic, glisser-déposer (`UiDragSource`, `UiDropTarget`, et depuis les autres
  fenêtres d'un outil), zones de texte colorées par morceaux, surlignées et marquées par ligne,
  en C++ comme en C# (`Ui.SetTextColors`), dessin en une passe après le tonemapping ou dans une image à part
  (les panneaux de l'éditeur), survol, clic et focus au clavier comme à la manette ;
- `Devex::Tools` : interface au thème réglable inspiré de Godot (Noto Sans, JetBrains Mono,
  icônes Lucide), panneaux rangés par un dock aux emplacements de Godot (onglets glissés d'un
  emplacement à l'autre, gardés avec le projet) : arbre de la scène, inspecteur, FileSystem,
  sortie, statistiques, annulation, en
  overlay (F1) ou dans l'éditeur : écrans 2D, 3D et Script au centre de la barre de menus comme
  dans Godot : chaque scène est 2D ou 3D et se voit dans l'écran de son type (l'autre reste vide),
  et les interfaces s'éditent dans l'écran 2D (canevas rendus pour de vrai dans le cadre de la
  caméra du jeu, éléments choisis, déplacés et redimensionnés avec leur thème appliqué ;
  l'inspecteur grise les champs qu'un style écrit et ouvre le thème) et se montrent par-dessus
  l'écran 3D d'une scène 3D,
  fenêtre *Create Entity* / *Add Component* en palette comme le *Create New Node* de Godot
  (composants et préréglages, recherche, catégories, fiche, favoris et récents par projet),
  gestionnaire de projets, arbre de scène (guides fins, entités glissées avant, dans ou après une
  autre, œil, préfab et code au bout des lignes), inspecteur (cartes repliables, nombres glissés
  ou tapés, vecteurs aux axes colorés, sélecteur de couleur maison, assets et entités lâchés sur
  leurs champs, une page par asset avec ses réglages d'import appliqués par *Reimport* comme dans
  Godot), Editor Settings et Project Settings (sections à gauche et filtre comme dans Godot,
  matrice de collision, Input Map dont chaque liaison écoute la prochaine touche), fenêtre Export,
  dialogues New Script, modifications non enregistrées et About, moniteurs de Statistics (une
  courbe par mesure) et Profiler (barres des images, timeline zoomable des zones, arbre des temps
  CPU, passes GPU, mémoire des assets), cadre de l'éditeur (barre de menus et ses menus, barre
  d'état, onglets des scènes que l'on glisse pour les ranger et dont le clic droit ferme les autres
  ou montre le fichier dans FileSystem, barre d'outils de la vue), panneaux
  Animation (timeline qui zoome et défile) et Animator (graphe des états, paramètres suivis en
  direct), écran Script (fichiers ouverts, menus File, Edit et Search, recherche, complétion, sur
  une zone de texte qui ne place que les lignes en vue), FileSystem et Output déjà écrits
  avec
  `Devex::Ui` (menus contextuels,
  infobulles, modales, listes virtuelles, texte de la sortie choisi et copié à la souris, fichiers
  glissés vers la vue et l'inspecteur, entités lâchées sur un dossier qui deviennent des préfabs),
  onglets de scènes, viewport et sa
  barre d'outils, caméra libre, sélection multiple (Ctrl et Maj dans l'arbre, rectangle dans la
  vue), gizmos et inspecteur sur plusieurs entités, copier, coller et dupliquer par le
  presse-papiers, renommage dans l'arbre, entités masquées dans la vue, matériaux glissés sur les
  objets, préfabs (glisser-déposer, valeurs
  modifiées, Revert, Make Local, Save as Prefab, mise à jour en direct), fichiers de code du projet
  avec éditeur de texte intégré à onglets (coloration syntaxique, numéros de ligne, recherche et
  remplacement, autocomplétion, erreurs de compilation dans la marge), ouverture dans l'IDE,
  *New Script…*, réglages de l'éditeur, aperçu sonore et forme
  d'onde des clips, éditeur de courbes (clés et pentes à déplacer, préréglages, *New Curve*),
  aperçu des particules dans la vue et contrôles de l'émetteur dans l'inspecteur, réglages des
  composants rangés en sections repliables, grille et gizmos 2D dans l'écran 2D, textures
  découpées en sprites dans l'inspecteur, éditeur des animations
  de sprites, couches de tri du projet, peinture des tuiles dans la vue (pinceau, gomme,
  rectangle, remplissage, pipette) et inspecteur des tilesets, formes de la physique 2D
  dessinées dans la vue, cuisson du maillage de navigation depuis l'inspecteur et son dessin dans
  la vue avec les agents, les obstacles et les chemins,
  volumes du projet, icônes et distances des sources audio, panneau Animation
  avec piste temporelle et images clés, panneau Animator (graphe de nœuds des machines à états,
  suivi en direct pendant le jeu), panneau Profiler (barres des images, chronologie par
  thread et GPU, tableaux des zones, des passes et de la mémoire des assets) ;
- `Devex::Runtime` : `Application`, boucle à pas fixe, mode éditeur et mode Play, modules de jeu
  (composants et systèmes rechargeables à chaud), code C# sur .NET hébergé (composants, systèmes,
  compilation et rechargement à chaud), chargement des maillages et textures en arrière-plan,
  préchargement, changement de scène immédiat ou en arrière-plan avec progression, actions
  d'entrée du projet (boutons, axes, vecteurs, contextes, réaffectation gardée pour le joueur),
  sauvegardes dans le dossier du joueur (objets réfléchis, scène restaurée, miniature), réglages
  du joueur gardés (volumes, fenêtre, valeurs du jeu), coroutines C++20 (`co_await` du temps, des
  frames, d'une condition ou d'un tween, arrêtées avec leur entité), rendu automatique de la
  scène, export d'un jeu ;
- `Devex.Managed` : l'API C# du moteur (`Component`, `Entity`, `Scene`, `Input`, `Physics`, `Audio`,
  `Animation`, `Navigation`, `Tween`, `Particles`, `Tilemaps`, `Ui`, `Prefabs`, `Assets`, `Saves`, `PlayerSettings`, `Time`, `Log`,
  `Profiler`, maths), les coroutines `async Coroutine` (`Wait.Seconds`, `Wait.Until`, tweens et
  tâches attendus sur le thread du jeu) et les vues des composants du
  moteur, compilée dans `bin/managed` quand le SDK .NET est installé ;
- `Devex::Engine` : tous les modules dans une bibliothèque partagée, `devex-engine.dll` ;
- `devex-editor` : l'éditeur, qui compile et recharge à chaud le code des projets ;
- `devex-player` : lance un projet hors de l'éditeur, en recompilant d'abord son code s'il est
  périmé, ou le paquet d'un jeu exporté (scène de démarrage, réglages de fenêtre et code du jeu) ;
  le lecteur, les jeux exportés et l'éditeur tiennent un journal dans un fichier et, s'ils
  plantent, y ajoutent la pile d'appels avec un minidump à côté ;
- `devex-bindgen` : outil de build qui génère les vues C# des composants C++ ;
- `samples/sandbox` : le bac à sable, un projet dont le gameplay mêle un module C++ et du C# dans
  `code/` :
  la scène `arena` (scène de démarrage), où un personnage marche, saute, lance des balles et
  renverse des caisses entre rampe, marches, plateforme mobile et zones qui allument des lampes,
  faite en partie de préfabs (`assets/prefabs` : caisse, pyramide de caisses, balle qui laisse
  une traînée, zone de lampe) ; le C# y ajoute des cibles qui comptent les balles reçues, font
  clignoter leur lampe et jaillir des étincelles là où la balle frappe (`code/Target.cs`, avec
  le score), une porte qui s'ouvre quand le joueur approche (`code/Door.cs`), un distributeur de
  caisses (`code/Dispenser.cs`) et un cube qui flotte
  (`code/Bobber.cs`) ; le lanceur C++ et les cibles C# jouent leurs sons, la porte sa source audio,
  le cube flottant émet un bourdonnement spatialisé et une ambiance Ogg tourne en boucle dans le
  groupe Music ; un robot rigué patrouille par le maillage de navigation, attend et salue le
  joueur par sa machine à états (`assets/animators/robot.dvxanimator`, `code/Robot.cs`), deux
  drones suivent le joueur en se contournant (`code/Follower.cs`), et la porte fermée comme les
  caisses poussées découpent le maillage ;
  Tab passe à la scène `platformer`, un jeu de plateformes en pixel art (un chevalier animé qui
  court et saute sur un niveau de tuiles simulé par la physique 2D, traverse les corniches
  par-dessous, pousse des caisses, prend une plateforme mobile au-dessus de l'eau animée et
  revient au départ s'il y tombe, des pièces qui tournent ramassées par déclencheur, un
  coucher de soleil qui défile plus lentement, un mur éclairé par une torche,
  `code/Platformer.cs`), d'où Tab mène à la
  scène `sandbox` ; la
  scène `sandbox` (caisse et balises glTF, sphères or et plastique, ciel HDR, plateau tournant dont
  les satellites brillent et dont la caisse flotte par un `Tweener` et la courbe
  `assets/curves/Hover.dvxcurve`, panneaux de verre teinté, jour et nuit en fondu avec N par une
  coroutine C++, feu et fumée sur une balise, fontaine d'étincelles à traînées sur l'autre), qui
  ouvre sur un menu
  principal, un écran de réglages (nom, mot de passe, curseur de volume lié à son étiquette, case
  plein écran, liste déroulante de la difficulté, infobulles, touche de saut à réaffecter et à
  réinitialiser, gardés d'une partie à l'autre, aide
  en texte riche qui défile dans un cadre en neuf parts), un menu de pause appelé par Échap, avec
  un sac dont on glisse les objets d'une case à l'autre, qui sauvegarde la partie (le bouton le
  dit par une coroutine C# qui attend ses fondus) que le menu principal reprend avec sa
  miniature, et un HUD, tous habillés
  par le thème `assets/ui/sandbox.dvxtheme` et pilotés par `code/Menu.cs`.

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

Sous Linux (Ubuntu 24.04, GCC 14), avec les paquets de développement d'X11, de Wayland et du son
qu'installe la CI :

```bash
CC=gcc-14 CXX=g++-14 cmake --preset linux-debug
cmake --build --preset build-linux-debug
ctest --preset test-linux-debug --label-exclude gpu
```

Chaque push sur `main` est construit et testé par GitHub Actions (Windows et Linux, Debug et
Release, sans les tests qui demandent un GPU) : voir [.github/workflows/ci.yml](.github/workflows/ci.yml)
et l'onglet *Actions* du dépôt. Les tests marqués `[gpu]` se lancent localement avec `ctest`.

Les programmes sont produits dans `out/build/x64-debug/bin` ; dans l'arène du bac à sable, un clic
capture la souris, ZQSD (WASD) ou le stick gauche marchent, le stick droit regarde, Maj court,
Espace ou le bouton du bas saute (ce sont les actions du projet), un clic lance une balle, C passe à
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
dans `third_party` avec leurs licences, comme ufbx (MIT ou domaine public), qui lit les fichiers FBX
et OBJ.

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
