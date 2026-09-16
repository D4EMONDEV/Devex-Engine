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
| `Asset`         | base d'assets (UUID, `.dvxmeta`), cache d'import, données de maillage     | Core, Math, Serialization     |
| `Render`        | façade `Renderer` / `RenderWorld` ; tout `Vk*` reste dans `src/render/vulkan` | Core, Math, Platform, Vulkan |
| `Scene`         | entités, hiérarchie, composants, systèmes, prefabs                        | Core, Math, Serialization, Asset |
| `Runtime`       | `Application`, boucle de jeu, extraction Scene → Render, code gameplay    | tous les modules ci-dessus    |

Applications au sommet : `apps/sandbox`, `apps/editor` et la DLL gameplay d'un jeu
dépendent de `Runtime`.

Règles :

- aucun type `Vk*` ou `SDL_*` n'apparaît dans un header public hors de son module
  propriétaire, et les autres modules écrivent `devex::math::Vec3`, jamais `glm::vec3` ;
- `Scene` ne dépend pas de `Render` : `Runtime` extrait chaque frame les données de
  rendu depuis la scène vers le `RenderWorld` ;
- les importeurs FBX/glTF appartiennent aux outils (éditeur, cooker), jamais au jeu
  exporté ;
- un module nouveau arrive avec ses tests et une démonstration dans le bac à sable.

### Dépôt

```text
engine/        modules du moteur (include/ + src/)
apps/sandbox/  bac à sable des jalons
apps/editor/   éditeur ImGui (à venir)
shaders/       sources Slang du moteur
tests/         tests Catch2, un dossier par module
cmake/         fonctions CMake partagées
docs/          décisions et documentation
```

## Détails des décisions

### Rendu

- **Vulkan 1.4** : dynamic rendering, synchronization2, descriptor indexing et push
  descriptors sans extension optionnelle. Aucun `VkRenderPass` legacy.
- **volk** charge Vulkan, **VMA** gère la mémoire GPU, des wrappers RAII minces maison
  encapsulent les objets Vulkan.
- Couches de validation actives en Debug.
- Profondeur **reverse-Z** (0..1) pour la précision sur les grandes distances ; le
  retournement de l'axe Y de Vulkan est géré par la projection, pas par la scène.
- **Slang** : shaders compilés en SPIR-V au build par `slangc` (SDK Vulkan) ; l'API
  Slang servira plus tard au rechargement à chaud dans l'éditeur.
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
| SDL3                  | fenêtre, entrées     | 1        |
| volk, VMA             | Vulkan               | 2        |
| GLM                   | maths                | 3        |
| fastgltf              | import glTF          | 3        |
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
1. **Fenêtre** — `Platform` sur SDL3 : fenêtre redimensionnable, événements clavier et
   souris, boucle de jeu à pas de temps.
2. **Vulkan** — instance 1.4, validation, choix du GPU, device, swapchain, couleur de
   fond, redimensionnement correct.
3. **Premier maillage** — shaders Slang, buffers VMA, caméra (Y-up, reverse-Z), triangle
   puis maillage glTF.
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
