# Devex Engine

Devex est un moteur de jeu open source conçu d'abord pour la 3D, écrit en C++ moderne
et rendu avec Vulkan. Il est distribué sous licence [MIT](LICENSE).

## Direction technique

- C++23 (C++26 en expérimentation), CMake + Ninja, dépendances via vcpkg ;
- Vulkan 1.4 (volk + VMA), shaders Slang, cible de rendu forward+ clustered PBR ;
- SDL3 pour la fenêtre et les entrées, Windows d'abord avec un code portable ;
- scènes en entités + composants, stockées de façon data-oriented ;
- repère Y-up main droite, formats de projet texte `.dvx*` ;
- gameplay en C++ rechargeable à chaud, C# prévu ensuite ;
- éditeur Dear ImGui (docking) puis UI maison.

Le détail, l'architecture des modules et les jalons sont dans
[docs/decisions.md](docs/decisions.md).

## État actuel

- `Devex::Core` : `Result`/`Error`, journal `DEVEX_LOG_*`, assertions, `SlotMap` ;
- `Devex::Math` : types GLM sous `devex::math`, projection reverse-Z infinie ;
- `Devex::Platform` : fenêtre, événements et entrées clavier/souris sur SDL3 ;
- `Devex::Asset` / `Devex::AssetImport` : maillages CPU, primitives, import glTF ;
- `Devex::Render` : renderer Vulkan 1.4 (volk, VMA), shaders Slang, vertex pulling,
  profondeur reverse-Z, maillages éclairés ;
- `Devex::Runtime` : classe `Application`, boucle de jeu à pas fixe et rendu par frame ;
- `devex-sandbox` : caméra libre au-dessus d'une scène procédurale ; glisser un `.gltf` ou
  un `.glb` sur la fenêtre l'importe ;
- prochain jalon : scène (entités, composants, hiérarchie, format `.dvxscene`).

Le SDK Vulkan fournit `slangc`, qui compile les shaders pendant le build.

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

Le programme de bac à sable est produit dans `out/build/x64-debug/bin`.

## Règle de conception

Chaque système est une cible CMake explicite `Devex::<Module>` avec son API publique sous
`engine/include/devex/<module>/` et son implémentation privée sous `engine/src/<module>/`.
Le bac à sable sert à intégrer et observer le système ; les tests protègent son
comportement indépendamment du rendu.
