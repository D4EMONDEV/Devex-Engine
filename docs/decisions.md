# Décisions fondatrices de Devex

Ce document fixe les choix structurants du moteur. Chaque décision peut être révisée,
mais seulement explicitement ici : le code suit ce document, pas l'inverse.

## Résumé

| Sujet                    | Décision                                                           |
| ------------------------ | ------------------------------------------------------------------ |
| Priorité                 | Runtime d'abord, le bac à sable est le premier « jeu »             |
| Plateformes              | Windows x64 d'abord, code portable (aucun Win32 hors `platform`)   |
| Licence                  | MIT                                                                |
| Modèle de scène          | Hybride : entités + composants visibles, stockage ECS contigu      |
| Fenêtre / entrées        | SDL3                                                               |
| Graphique                | Vulkan 1.4 minimum, API C + volk + VMA, sans RHI pour l'instant    |
| Shaders                  | Slang → SPIR-V                                                     |
| Architecture de rendu    | Forward+ clustered PBR (cible)                                     |
| Gameplay                 | C++ (DLL rechargeable) d'abord, C# (.NET hosting) ensuite          |
| Format source            | Texte maison lisible, extensions `.dvx*`, binaire cooké à l'export |
| Import 3D                | glTF 2.0 (fastgltf) + FBX (ufbx)                                   |
| UI éditeur               | Dear ImGui (branche docking), UI retenue maison plus tard          |
| Modules C++              | Headers classiques                                                 |
| Erreurs                  | `std::expected`, pas d'exceptions dans le moteur                   |
| Dépendances              | vcpkg en mode manifeste (`vcpkg.json`)                             |
| Maths                    | GLM derrière `devex::math`                                         |
| Coordonnées              | Y-up, main droite, -Z avant, 1 unité = 1 mètre                     |
| Tests                    | Catch2 v3 via CTest                                                |
| Boucle de jeu            | Pas fixe (60 Hz par défaut) + mise à jour variable par frame       |
| Point d'entrée           | Le moteur possède la boucle, le jeu dérive de `Application`        |
| Entrées                  | État interrogeable + événements, actions nommées plus tard         |
| Clavier                  | `Key` = position physique (WASD devient ZQSD en AZERTY)            |
| Présentation             | VSync (FIFO) par défaut, Mailbox/Immediate en option               |
| Thread de rendu          | Thread principal, rendu découplé par un instantané `RenderWorld`   |
| Redimensionnement        | Rendu continu pendant le redimensionnement modal de Windows        |
| Passes de rendu          | Manuelles jusqu'au PBR, render graph introduit à ce moment         |
| Compilation des shaders  | Au build par `slangc`, fichiers `.spv` à côté de l'exécutable      |
| Accès aux données GPU    | Bindless + vertex pulling par buffer device address                |
| Projection               | Reverse-Z, far plane infini                                        |
| glTF (jalon 3)           | Import direct temporaire, pipeline d'assets plus tard              |

## Architecture cible

Chaque module est une cible CMake `Devex::<Module>` avec son API publique dans
`engine/include/devex/<module>/` et son implémentation dans `engine/src/<module>/`.
Les dépendances sont strictement descendantes : un module ne connaît jamais un module
situé au-dessus de lui, et le graphe reste sans cycle.

| Module          | Rôle                                                                      | Dépend de                     |
| --------------- | ------------------------------------------------------------------------- | ----------------------------- |
| `Core`          | types, `Result<T>`/`Error`, assert, log, handles, UUID, allocateurs, jobs | —                             |
| `Math`          | alias `Vec3`, `Mat4`, `Quat`…, conventions de repère et de profondeur     | Core, GLM                     |
| `Platform`      | fenêtre, entrées, temps, système de fichiers, chargement de DLL           | Core, SDL3                    |
| `Serialization` | lecture/écriture du format texte `.dvx*` et des archives binaires cookées | Core, Math                    |
| `Asset`         | données CPU (`MeshData`, primitives) ; plus tard UUID, `.dvxmeta`, cache  | Core, Math (+ Serialization)  |
| `AssetImport`   | importeurs de formats sources (glTF, FBX), réservés aux outils            | Asset, fastgltf               |
| `Render`        | façade `Renderer` / `RenderWorld` ; tout `Vk*` reste dans `src/render/vulkan` | Core, Math, Platform, Asset, Vulkan |
| `Scene`         | entités, hiérarchie, composants, systèmes, prefabs                        | Core, Math, Serialization, Asset |
| `Runtime`       | `Application`, boucle de jeu, extraction Scene → Render, code gameplay    | tous les modules ci-dessus    |

Applications au sommet : `apps/sandbox`, `apps/editor` et la DLL gameplay d'un jeu
dépendent de `Runtime`.

Règles :

- aucun type `Vk*` ou `SDL_*` n'apparaît dans un header public hors de son module
  propriétaire, et les autres modules écrivent `devex::math::Vec3`, jamais `glm::vec3` ;
- `Scene` ne dépend pas de `Render` : `Runtime` extrait chaque frame les données de
  rendu depuis la scène vers le `RenderWorld` ;
- les importeurs FBX/glTF vivent dans `AssetImport` et appartiennent aux outils (éditeur,
  cooker), jamais au jeu exporté ; le bac à sable s'en sert temporairement ;
- `Render` ne dépend d'`Asset` que pour les données CPU (`MeshData`), jamais de la base
  d'assets ;
- un module nouveau arrive avec ses tests et une démonstration dans le bac à sable.

### Dépôt

```text
engine/        modules du moteur (include/ + src/)
apps/sandbox/  bac à sable des jalons
apps/editor/   éditeur ImGui (à venir)
shaders/       sources Slang du moteur, compilées dans bin/shaders
tests/         tests Catch2, un dossier par module, données dans tests/data
cmake/         fonctions CMake partagées
docs/          décisions et documentation
```

## Détails des décisions

### Rendu

- **Vulkan 1.4** : dynamic rendering, synchronization2, descriptor indexing et push
  descriptors sans extension optionnelle. Aucun `VkRenderPass` legacy.
- **volk** charge Vulkan, **VMA** gère la mémoire GPU, des wrappers RAII minces maison
  encapsulent les objets Vulkan.
- Couche de validation Khronos active hors Release, **validation de synchronisation
  comprise** ; ses erreurs passent par le journal Devex et font échouer les tests `[gpu]`.
  Les avertissements généraux du loader (overlays tiers) sont rétrogradés en debug.
- **Découplage** : le gameplay remplit un instantané `RenderWorld` dans `onRender`, entre
  `Renderer::beginFrame` et `Renderer::endFrame`. Le renderer ne lit jamais l'état du jeu :
  un thread de rendu dédié pourra consommer ces instantanés sans changer l'API.
- **Présentation** : FIFO (VSync) par défaut ; Mailbox ou Immediate sur demande, avec
  repli sur FIFO si l'écran ne les propose pas.
- **GPU** : un GPU compatible (Vulkan 1.4, swapchain, dynamic rendering, synchronization2,
  buffer device address, shader draw parameters, une file qui dessine et présente) est choisi par ordre discret > intégré > virtuel >
  CPU, ou par nom via `preferredGpu`. Un seul `Renderer` existe à la fois (volk charge les
  fonctions du device globalement).
- **Frames** : 2 frames en vol, chacune avec son pool de commandes, sa fence et son
  sémaphore d'acquisition ; un sémaphore de présentation par image du swapchain.
- **Swapchain** : format sRGB (le renderer écrit des couleurs linéaires), recréé quand la
  taille en pixels change ou quand Vulkan le signale périmé ; aucune image n'est rendue
  tant que la fenêtre n'a pas de surface visible.
- **Redimensionnement en direct** : pendant la boucle modale de Windows, SDL envoie des
  `SDL_EVENT_WINDOW_EXPOSED` (live resize) sur le thread principal ; `Platform` les
  transmet à un callback et `Runtime` y exécute une frame complète.
- **Barrières** : pour l'instant, transitions explicites entre états d'image
  (`AcquiredBackbuffer`, `ColorAttachment`, `DepthAttachment`, `Present`) avec leurs
  stages de synchronisation ; un render graph les calculera quand les passes se
  multiplieront (forward+ PBR).
- **Profondeur** : reverse-Z à far plane infini (`math::perspectiveReverseZ`), image
  `D32_SFLOAT` effacée à 0 et test `GREATER_OR_EQUAL`. L'image de profondeur reste dans
  son layout d'attachement toute sa vie pour qu'aucune barrière ne la partage entre
  frames en vol. La projection de `Math` garde Y vers le haut ; le renderer applique la
  correction du clip space Vulkan (Y vers le bas).
- **Slang** : `cmake/DevexShaders.cmake` compile chaque `shaders/*.slang` en un `.spv`
  contenant tous ses points d'entrée (`-fvk-use-entrypoint-name`), avec des matrices
  column-major comme GLM et un depfile pour les includes. Une erreur de shader est une
  erreur de build. L'API Slang servira plus tard au rechargement à chaud dans l'éditeur.
- **Données GPU** : vertex pulling. Les shaders lisent sommets et données de scène via des
  *buffer device addresses* passées en push constants (80 octets, sous le minimum garanti
  de 128) ; pas de vertex input state. Les dispositions mémoire C++ et Slang sont
  vérifiées par `static_assert` (`src/render/vulkan/GpuData.hpp`). Le descriptor set
  global bindless (textures, samplers) arrivera avec les premières textures.
- **Maillages** : `Renderer::createMesh` valide les données et les transfère de façon
  synchrone (buffer de staging VMA) ; il renvoie un `MeshHandle` générationnel
  (`core::SlotMap`). `destroyMesh` retarde la libération jusqu'à ce qu'aucune frame en
  vol ne puisse encore l'utiliser. Faces avant dans le sens antihoraire, back-face culling.
- Une RHI ne sera extraite que lorsqu'un second backend (DX12, Metal) aura un besoin
  réel.

### Monde et scènes

- L'utilisateur manipule des `Entity` avec des composants (`Transform`, `Camera`,
  `MeshRenderer`…) organisés en hiérarchie.
- En interne, chaque type de composant est stocké dans un tableau contigu parcouru par
  des systèmes. Pas d'EnTT/Flecs imposé tant que le premier prototype n'en montre pas le
  besoin.
- Repère : **Y-up, main droite**, +X droite, -Z avant, mètres, angles en radians.
  glTF s'importe sans conversion ; FBX est converti à l'import.

### Formats de fichiers

Projet utilisateur :

```text
MyGame/
  project.dvxproj
  assets/
    levels/level01.dvxscene
    prefabs/hero.dvxprefab
    materials/rock.dvxmat
    models/hero.glb
    models/hero.glb.dvxmeta   # UUID + options d'import
  .devex/                     # cache d'import, ignoré par Git
```

Format texte source (sections + clés, diffable et fusionnable) :

```text
[scene format=1 uuid="8f2c…"]

[entity id=1 name="Player"]

[component entity=1 type="Transform"]
position = vec3(0, 1, 0)
rotation = quat(0, 0, 0, 1)

[component entity=1 type="MeshRenderer"]
mesh = asset("b41e…")
material = asset("77ac…")
```

- Les références entre fichiers passent par **UUID**, jamais par chemin : renommer ou
  déplacer un asset ne casse rien.
- Les chemins affichés utilisent le schéma `res://`.
- L'export d'un jeu produit des données **binaires cookées** chargées sans parsing.

### Boucle de jeu et application

- Le moteur possède la boucle : un programme dérive de `devex::runtime::Application` et
  se lance avec `devex::runtime::run<MonApplication>(config)`. Les services (plateforme,
  fenêtre, entrées) sont disponibles de `onStartup` à `onShutdown`, pas dans le
  constructeur.
- Chaque frame : événements (`onEvent`), puis zéro ou plusieurs `onFixedUpdate` au pas
  fixe (60 Hz par défaut), puis un `onUpdate` avec le temps réel écoulé.
- Le pas fixe accumule le temps en nanosecondes entières (aucune dérive) et borne une
  frame à 250 ms pour éviter la spirale de rattrapage. `interpolationAlpha()` donne la
  progression vers le pas suivant pour interpoler le rendu.
- Fermer la fenêtre principale ou recevoir une demande de l'OS termine la boucle.
  `maxFrameRate` limite les FPS ; une fenêtre minimisée tourne à 20 Hz au plus.

### Entrées

- `Input` expose un état interrogé par le gameplay : `isKeyDown`, `wasKeyPressed`,
  `wasKeyReleased`, boutons de souris, `mouseDelta`, `mouseWheel`. Les transitions
  durent exactement une frame : on les lit dans `onUpdate`, pas dans `onFixedUpdate`.
- Les événements (`KeyPressed`, `WindowResized`, `FileDropped`…) forment un
  `std::variant` reçu par `onEvent`.
- `Key` désigne une **position physique**, nommée d'après le QWERTY US, avec les valeurs
  des usages clavier USB HID. `Platform::keyLabel(Key::W)` renvoie « Z » sur AZERTY pour
  l'affichage.
- Perdre le focus relâche toutes les touches et boutons.
- Les actions nommées (InputMap, rebinding, manettes) viendront par-dessus plus tard.

### Gameplay

- Premier temps : gameplay en **C++** compilé dans une DLL chargée par le runtime et
  rechargeable à chaud par l'éditeur.
- L'API moteur est conçue pour être exposée plus tard en **C#** (hébergement .NET via
  `hostfxr`) : handles plutôt que pointeurs bruts, durées de vie explicites.

### Code C++

- C++23 obligatoire, C++26 seulement en expérimentation (`DEVEX_CXX_STANDARD`).
- Headers classiques, `#pragma once`, `.clang-format` du dépôt.
- Nommage : types et fichiers en `PascalCase`, fonctions et variables en `camelCase`,
  membres privés `m_camelCase`, macros `DEVEX_UPPER_CASE`, espaces de noms
  `devex::<module>`.
- Erreurs récupérables : `devex::core::Result<T>` (alias de
  `std::expected<T, devex::core::Error>`), créées avec `makeError(code, format, ...)`.
- Bugs de programmation : `DEVEX_ASSERT` / `DEVEX_ASSERT_MSG`, actifs hors Release et
  MinSizeRel ; désactivés, leurs arguments ne sont jamais évalués.
- Journal : `DEVEX_LOG_TRACE` … `DEVEX_LOG_FATAL` avec la syntaxe `std::format` ; un
  message sous le niveau courant n'évalue pas ses arguments.
- Le moteur ne lance pas d'exceptions ; elles restent activées au compilateur pour la STL
  MSVC et Catch2.
- MSVC compile en `/utf-8` : sources et messages sont en UTF-8.

### Dépendances prévues (vcpkg)

| Bibliothèque          | Usage                | Jalon    |
| --------------------- | -------------------- | -------- |
| SDL3 (feature `vulkan`) | fenêtre, entrées   | 1 ✅     |
| GLM (header-only)     | maths                | 1 ✅     |
| volk (+ vulkan-headers) | Vulkan             | 2 ✅     |
| VMA                   | mémoire GPU          | 3 ✅     |
| fastgltf              | import glTF          | 3 ✅     |
| Dear ImGui (docking)  | outils, éditeur      | 5        |
| Catch2                | tests (feature `tests`) | 0 ✅  |

Hors vcpkg :

- **Slang** est fourni par le SDK Vulkan (`C:\VulkanSDK\1.4.350.0`) ;
- **ufbx** n'existe pas dans vcpkg : ses deux fichiers (`ufbx.c`, `ufbx.h`) seront
  intégrés dans `third_party/ufbx` lors du support FBX.

CMake trouve vcpkg via la variable `VCPKG_ROOT`, sinon via l'exécutable `vcpkg` du
`PATH` (voir `cmake/DevexVcpkg.cmake`). La version des ports est figée par
`builtin-baseline` dans `vcpkg.json`.

## Jalons

Chaque jalon se termine par une démo observable dans `devex-sandbox` et des tests.

0. ✅ **Fondations** — `vcpkg.json`, Catch2, `Core` (log, assert, `Result`), dépôt Git.
1. ✅ **Fenêtre** — `Platform` sur SDL3 : fenêtre redimensionnable, événements clavier et
   souris ; `Runtime` : `Application` et boucle à pas fixe ; `Math` sur GLM.
2. ✅ **Vulkan** — instance 1.4, validation, choix du GPU, device, swapchain, couleur de
   fond, redimensionnement en direct.
3. ✅ **Premier maillage** — shaders Slang, buffers VMA, caméra (Y-up, reverse-Z),
   primitives procédurales et import glTF.
4. **Scène** — `Entity`, `Transform`, `MeshRenderer`, hiérarchie, lecture et écriture
   `.dvxscene`.
5. **Outils** — ImGui docking : statistiques du renderer, arbre de scène, inspecteur.

Ensuite, sans ordre figé : PBR forward+ clustered, base d'assets et cache d'import, DLL
gameplay rechargeable, éditeur, CI Linux.

## Questions ouvertes

À trancher le moment venu, pas avant :

- **Réflexion des composants** (inspecteur, sérialisation, bindings C#) : macros
  d'enregistrement maintenant, réflexion statique C++26 quand MSVC la supportera ?
- **Physique** : Jolt Physics est le candidat naturel (MIT, utilisé par Godot 4).
- **Audio** : SDL3 audio, miniaudio ou FMOD/Wwise en option.
- **UI retenue maison** pour l'éditeur et les jeux, qui remplacera ImGui.
- **CI** : GitHub Actions Windows, puis Linux.
