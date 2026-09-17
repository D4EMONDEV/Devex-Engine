# Devex Engine

Devex est un moteur de jeu open source conçu d'abord pour la 3D, écrit en C++ moderne
et rendu avec Vulkan. Il est distribué sous licence [MIT](LICENSE).

## Direction technique

- C++23 (C++26 en expérimentation), CMake + Ninja, dépendances via vcpkg ;
- Vulkan 1.4 (volk + VMA), shaders Slang, cible de rendu forward+ clustered PBR ;
- SDL3 pour la fenêtre et les entrées, Windows d'abord avec un code portable ;
- scènes en entités + composants, stockées de façon data-oriented ;
- repère Y-up main droite, formats de projet texte `.dvx*` ;
- gameplay en C++ (composants et systèmes) compilé et rechargé à chaud par l'éditeur, C# prévu
  ensuite ;
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
- `Devex::Reflection` : description des champs des composants (`DEVEX_REFLECT`) ;
- `Devex::Serialization` : format texte commun des fichiers `.dvx*`, flux binaires ;
- `Devex::Asset` : `AssetId`, maillages, textures, matériaux et modèles, fichiers `.dvxasset`,
  projets `.dvxproj`, paquets de jeux exportés `.dvxpak` ;
- `Devex::AssetImport` : base d'assets (`.dvxmeta`, cache `.devex/`, imports en arrière-plan,
  réimport à chaud), importeurs de textures (BC7/BC5), de `.dvxmat`, de glTF et de scènes ;
- `Devex::Render` : renderer Vulkan 1.4 (volk, VMA), shaders Slang, render graph, PBR
  forward+ clustered, ombres en cascades, ciel HDR et IBL, MSAA, exposition automatique,
  tonemapping AgX, rendu dans une texture, sélection sur le GPU, contours et lignes d'outils ;
- `Devex::Scene` : entités à UUID, composants en sparse sets, hiérarchie, `.dvxscene`,
  instanciation de modèles, composants de physique, préfabs liés (scènes imbriquées avec leurs
  modifications) ;
- `Devex::Physics` : simulation Jolt Physics des corps rigides, colliders (primitives, maillages,
  déclencheurs) et personnages, couches de collision, requêtes, forces, contacts, interpolation ;
- `Devex::Tools` : interface ImGui au thème réglable inspiré de Godot (Noto Sans, JetBrains Mono,
  icônes Lucide) : arbre de la scène, inspecteur, FileSystem, sortie, statistiques, annulation, en
  overlay (F1) ou dans l'éditeur : gestionnaire de projets, onglets de scènes, viewport et sa
  barre d'outils, caméra libre, sélection à la souris, gizmos, préfabs (glisser-déposer, valeurs
  modifiées, Revert, Make Local, Save as Prefab, mise à jour en direct), réglages de l'éditeur ;
- `Devex::Runtime` : `Application`, boucle à pas fixe, mode éditeur et mode Play, modules de jeu
  (composants et systèmes rechargeables à chaud), chargement des assets à la demande, changement de
  scène, rendu automatique de la scène, export d'un jeu ;
- `Devex::Engine` : tous les modules dans une bibliothèque partagée, `devex-engine.dll` ;
- `devex-editor` : l'éditeur, qui compile et recharge à chaud le code des projets ;
- `devex-player` : lance un projet hors de l'éditeur, ou le paquet d'un jeu exporté (scène de
  démarrage, réglages de fenêtre et code du jeu) ;
- `samples/sandbox` : le bac à sable, un projet dont le gameplay est un module de jeu dans `code/` :
  la scène `arena` (scène de démarrage), où un personnage marche, saute, lance des balles et
  renverse des caisses entre rampe, marches, plateforme mobile et zones qui allument des lampes,
  faite en partie de préfabs (`assets/prefabs` : caisse, pyramide de caisses, balle, zone de
  lampe), où Tab passe à l'autre scène ; et la
  scène `sandbox` (caisse et balises glTF, sphères or et plastique, ciel HDR, plateau tournant, jour
  et nuit avec N).

Le SDK Vulkan fournit `slangc`, qui compile les shaders pendant le build. Les assets
d'exemple et les données de test sont produits par `scripts/generate_sample_assets.py`.

Les tests marqués `[gpu]` ouvrent une fenêtre masquée et nécessitent un GPU Vulkan 1.4.

## Construire

Prérequis : Visual Studio 2026 (C++), CMake 4, Ninja, [vcpkg](https://vcpkg.io) et le
SDK Vulkan. CMake utilise `VCPKG_ROOT` s'il est défini, sinon le `vcpkg` trouvé dans le
`PATH` ; les dépendances sont installées automatiquement au premier `cmake --preset`.

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
recharge à chaque fichier enregistré, même pendant une partie. Le code d'un jeu déclare des
composants et des systèmes :

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
