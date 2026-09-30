# Décisions fondatrices de Devex

Ce document fixe les choix structurants du moteur. Chaque décision peut être révisée,
mais seulement explicitement ici : le code suit ce document, pas l'inverse.

## Résumé

| Sujet                    | Décision                                                           |
| ------------------------ | ------------------------------------------------------------------ |
| Priorité                 | Runtime d'abord, le bac à sable est le premier « jeu »             |
| Liaison du moteur        | Une DLL `devex-engine` partagée par programmes, tests et jeux      |
| Plateformes              | Windows x64 d'abord, code portable (aucun Win32 hors `platform`)   |
| Licence                  | MIT                                                                |
| Modèle de scène          | Hybride : entités + composants visibles, stockage ECS contigu      |
| Fenêtre / entrées        | SDL3                                                               |
| Graphique                | Vulkan 1.4 minimum, API C + volk + VMA, sans RHI pour l'instant    |
| Shaders                  | Slang → SPIR-V                                                     |
| Architecture de rendu    | Forward+ clustered avec prépasse, PBR métal-rugosité (GGX)         |
| Gameplay                 | C++ (DLL rechargeable) et C# (.NET hébergé), au choix, ensemble    |
| Format source            |  Texte maison lisible, extensions `.dvx*`, binaire cooké à l'export|
| Import 3D                | glTF 2.0 (fastgltf), FBX et OBJ (ufbx), convertis au repère moteur |
| UI éditeur               | Dear ImGui, remplacé panneau par panneau par l'UI des jeux         |
| Modules C++              | Headers classiques                                                 |
| Erreurs                  | `std::expected`, pas d'exceptions dans le moteur                   |
| Dépendances              | vcpkg en mode manifeste (`vcpkg.json`)                             |
| Maths                    | GLM derrière `devex::math`                                         |
| Coordonnées              | Y-up, main droite, -Z avant, 1 unité = 1 mètre                     |
| Tests                    | Catch2 v3 via CTest                                                |
| Intégration continue     | GitHub Actions, Windows, Debug et Release, tests sans GPU          |
| Boucle de jeu            | Pas fixe (60 Hz par défaut) + mise à jour variable par frame       |
| Point d'entrée           | Le moteur possède la boucle, le jeu dérive de `Application`        |
| Entrées                  | État interrogeable + événements, clavier, souris et manettes       |
| Actions d'entrée         | Dans les réglages du projet, contextes activables, réaffectation   |
| Sauvegardes              | Objet réfléchi + scène + miniature, texte dans le dossier du joueur |
| Réglages du joueur       | Volumes, fenêtre et valeurs du jeu, gardés et appliqués par le moteur |
| Tweens                   | Tout champ d'un composant, par code ou `Tweener`, courbes dessinées |
| Coroutines               | `co_await` en C++, `async Coroutine` en C#, reprises pendant Update |
| Particules               | Simulées sur le CPU en parallèle, réglages dans `ParticleEmitter`  |
| Rendu des particules     | Billboards, étirées, traînées en ruban, triées avec la transparence |
| Sprites                  | Découpés à l'import de la texture, dessinés dans le rendu 3D       |
| Animation des sprites    | Asset `.dvxframes` d'animations nommées, joué par `SpriteAnimator` |
| Tri 2D                   | Couches de tri nommées du projet, ordre, puis distance             |
| Type de scène            | 2D ou 3D, écrit dans le fichier (`kind`), comme la racine de Godot |
| Écrans 2D et 3D          | Une scène se voit dans l'écran de son type, l'autre écran est vide |
| Interfaces dans l'éditeur | Éditées dans l'écran 2D, montrées dans l'écran 3D d'une scène 3D  |
| 2.5D                     | Une scène 3D : sprites et maillages ensemble dans l'écran 3D       |
| Caméra orthographique    | `Camera::projection`, profondeur inversée entre deux plans          |
| Filtrage des textures    | Réglage d'import, un sampler par texture à côté du tableau bindless |
| Tuiles                   | Composant `Tilemap`, cellules par blocs de 16×16 dans la scène     |
| Tilesets                 | Asset `.dvxtileset` : sprite, collision, animation, données par tuile |
| Peinture des tuiles      | Outils sous la `Tilemap` dans l'inspecteur, un trait par annulation |
| Physique 2D              | Box2D 3.1 (MIT), module `Physics2D`, à côté du monde 3D            |
| Corps 2D                 | `RigidBody2D` + colliders 2D (boîte, cercle, capsule, polygone)    |
| Collisions des tuiles    | `TilemapCollider2D` : rectangles fusionnés et corniches du tileset |
| Personnages 2D           | `CharacterController2D` « move and slide » sur le mover de Box2D   |
| Navigation               | Recast & Detour (zlib), module `Navigation`, foule de DetourCrowd  |
| Maillage de navigation   | Asset `.dvxnavmesh` cuit dans l'éditeur depuis les colliders statiques |
| Agents                   | Composant `NavMeshAgent`, destinations données par le code         |
| Obstacles mobiles        | `NavMeshObstacle` (boîte, cylindre) découpe les tuiles pendant le jeu |
| Clavier                  | `Key` = position physique (WASD devient ZQSD en AZERTY)            |
| Présentation             | VSync (FIFO) par défaut, Mailbox/Immediate en option               |
| Thread de rendu          | Thread principal, rendu découplé par un instantané `RenderWorld`   |
| Redimensionnement        | Rendu continu pendant le redimensionnement modal de Windows        |
| Passes de rendu          | Render graph léger : barrières calculées, images transitoires      |
| Compilation des shaders  | Au build par `slangc`, fichiers `.spv` à côté de l'exécutable      |
| Accès aux données GPU    | Bindless + vertex pulling par buffer device address                |
| Projection               | Reverse-Z, far plane infini                                        |
| glTF                     | Modèle (hiérarchie de nœuds) et sous-assets importés par la base   |
| Stockage ECS             | Sparse sets maison, un tableau contigu par type de composant       |
| Réflexion                | Enregistrement explicite (`DEVEX_REFLECT`) en attendant C++26      |
| Identité des entités     | UUID par entité dans les fichiers, handle générationnel en mémoire |
| Références d'assets      | `AssetId` (UUID) dès maintenant, primitives à UUID réservés        |
| Premiers outils          | Module `Tools` + overlay (F1), réutilisable par le futur éditeur   |
| Éditeur                  | Mode du runtime : `devex-editor`, ou un jeu lancé dans l'éditeur   |
| Mode Play                | Copie de la scène (mêmes handles), une vue, Pause et pas à pas     |
| Sélection à la souris    | Identifiants d'objets lus sur le GPU, icônes des lumières sur CPU  |
| Gizmos                   | Maison : déplacement, rotation, échelle, local/global, magnétisme  |
| Projets dans l'éditeur   | Gestionnaire de projets dans une fenêtre compacte, ou argument     |
| Scènes                   | Assets importés (`AssetId`), ouvertes et enregistrées par l'éditeur |
| Sélection                | Multiple : Ctrl et Maj dans l'arbre, rectangle dans la vue         |
| Copier-coller d'entités  | Texte `[entities]` du presse-papiers système, UUID neufs au collage |
| Visibilité dans l'éditeur | Œil par entité, vue de l'éditeur seulement, gardé par projet      |
| Code de jeu              | Composants et systèmes dans un module de jeu (DLL) par projet      |
| Compilation du jeu       | Par l'éditeur (CMake, en arrière-plan) à chaque modification       |
| Rechargement du jeu      | Composants conservés en texte, en édition comme en Play            |
| Lancement autonome       | `devex-player Projet.dvxproj` : scène de démarrage et code du jeu  |
| Bac à sable              | Projet d'exemple `samples/sandbox`, son gameplay dans `code/`      |
| Backend ImGui            | Officiels SDL3 + Vulkan, le backend Vulkan compilé avec volk       |
| Multi-fenêtre ImGui      | Docking dans la fenêtre principale seulement                       |
| Thème de l'éditeur       | Inspiré de Godot, dérivé d'une base, d'un accent et d'un contraste |
| Polices                  | Noto Sans (interface), JetBrains Mono (code), rendues par FreeType |
| Icônes                   | Lucide (SVG) dessinées comme glyphes, colorées par type d'objet    |
| Scènes ouvertes          | Onglets, chacun avec son historique, sa sélection et sa caméra     |
| Couleurs de l'interface  | Mélangées en espace d'affichage (vue UNORM de la swapchain)        |
| Barre de titre           | Native, sombre et à la couleur du thème sous Windows               |
| Annulation               | Commandes basées sur la réflexion (UUID, composant, champ, valeurs) |
| Base d'assets            | `.dvxmeta` versionné à côté de chaque source, cache `.devex/` local |
| Sous-assets              | UUID listés dans le `.dvxmeta`, retrouvés par clé à la réimportation |
| Cache d'import           | Artefacts binaires `.dvxasset` = format chargé par le jeu          |
| Fichiers modifiés        | Dossier `assets/` surveillé (efsw), réimport et remplacement à chaud |
| Exécution des imports    | Pool de jobs `core::JobSystem`, en arrière-plan                    |
| Chargement des assets    | Maillages et textures lus sur les workers, remplaçants en attendant |
| Envoi au GPU             | Copies dans les commandes de l'image, budget par image, sans attente |
| Scènes des jeux          | Chargement en arrière-plan avec progression, ou immédiat           |
| Textures                 | BC7 (couleurs, données) et BC5 (normales) à l'import, via basisu   |
| Modèles dans une scène   | Copie d'entités ; préfabs liés dans un jalon dédié                 |
| Matériaux                | Paramètres PBR glTF ; sous-assets en lecture seule + `.dvxmat`     |
| Textures côté GPU        | Bindless : un descriptor set global, une table de matériaux        |
| Unités de lumière        | Physiques : lux, lumens, nits, exposition EV100, kelvins           |
| Lumières                 | Directionnelle, ponctuelles et spots en clusters calculés sur CPU  |
| Ombres                   | Cascades (CSM, 4 × 2048²) pour la lumière directionnelle           |
| Lumière indirecte        | Ciel HDR équirectangulaire, IBL précalculée sur GPU                |
| Exposition               | Manuelle ou automatique (mesure GPU, adaptation CPU)               |
| Tonemapping              | AgX par défaut, Khronos PBR Neutral, ACES                          |
| Réglages d'environnement | Composant `Environment`, exposition sur le composant `Camera`      |
| Tangentes                | MikkTSpace à l'import, convention glTF                             |
| Physique                 | Jolt Physics 5.6 (MIT), module `Physics`, simulée pendant le jeu   |
| Corps physiques          | Composants `RigidBody` + colliders, comme Unity ; composés par enfants |
| Formes de collision      | Boîte, sphère, capsule, cylindre, maillage (triangles ou convexe)  |
| Personnages              | `CharacterController` sur le personnage virtuel de Jolt            |
| Physique et jeu          | Requêtes, forces et contacts listés dans `SystemContext::physics`  |
| Couches de collision     | 16 couches nommées par projet, matrice de collisions               |
| Pas de simulation        | Pas fixe (60 Hz), poses interpolées pour l'affichage               |
| Préfabs                  | Scènes imbriquées liées à leur fichier, comme Godot                |
| Modifications d'instance | Champs, noms, composants et entités ajoutés ; le reste suit le préfab |
| Préfabs dans l'éditeur   | Onglet du préfab, instances mises à jour en direct, Revert, Make Local |
| Export d'un jeu          | Dossier prêt à distribuer : lecteur renommé, DLL, paquet d'assets  |
| Paquet d'un jeu          | Un fichier `.dvxpak` indexé, artefacts compressés zstd, mappé en mémoire |
| Source des assets        | `AssetSource` : base d'assets en développement, paquet une fois exporté |
| Réglages de lancement    | Fenêtre, plein écran, vsync, images, icône dans le `.dvxproj`      |
| Gameplay en C#           | Composants `Component` (Start/Update) et systèmes, .NET hébergé    |
| Composants C#            | Types décrits à l'exécution : la scène possède leurs données       |
| Compilation du C#        | SDK .NET (`dotnet build`) lancé par l'éditeur, rechargement à chaud |
| Fichiers de code         | Dossier `code/` dans FileSystem, aperçu, ouverture dans l'IDE      |
| Nouveau script           | *Add Component > New Script…*, en C# ou en C++                     |
| .NET des jeux exportés   | Runtime .NET embarqué dans `managed/`, rien à installer            |
| Références d'entités     | `EntityRef` : UUID en mémoire, suivi dans les préfabs, `resolve`   |
| Listes                   | `std::vector` de valeurs réfléchies, `list(...)` dans les fichiers |
| C++ vu du C#             | Vues `ref struct` générées de la réflexion, empreinte vérifiée     |
| Collisions en C#         | `OnCollisionEnter`, `OnTriggerEnter`... et `Physics.Contacts`      |
| Champs des composants C# | Chargés au début de chaque phase, écrits à sa fin                  |
| Rechargement du C#       | Champs non enregistrés conservés par nom, Start non rappelé        |
| Débogage du C#           | Attache depuis l'IDE, Play qui attend, symboles chargés            |
| Profileur                | Maison : zones CPU par thread, timestamps GPU par passe, panneau   |
| Mémoire des assets       | Estimée par l'`AssetManager`, par type et pour les plus lourds     |
| Audio                    | miniaudio + stb_vorbis, sons 2D ou spatialisés                    |
| Clips audio              | WAV, FLAC, MP3, Ogg Vorbis ; décodés au chargement ou à la lecture |
| Écouteur                 | `AudioListener`, sinon caméra principale                         |
| Mixage                   | Volume Master et 8 groupes nommés par projet                      |
| Animation                | Squelettes glTF, clips en sous-assets du modèle                   |
| Os                       | Une entité par os, pilotée par nom                                |
| Lecture                  | Composant `Animator` : un clip, fondu croisé, root motion en option |
| Machines à états         | Asset `.dvxanimator` (paramètres, états, transitions) joué par `Animator` |
| Arbres de mélange        | 1D (seuils) et 2D (bandes de gradient), clips synchronisés        |
| Éditeur d'animator       | Panneau de graphe de nœuds, réglages dans l'inspecteur, annulation propre |
| Skinning                 | Dans le vertex shader, matrices d'os en buffer par frame          |
| Écrans de l'éditeur      | 2D, 3D et Script au centre de la barre de menus                    |
| Culling                  | Tronc de vue sur CPU, pour la caméra, les cascades et chaque vue   |
| Boîtes englobantes       | Cuites dans le maillage à l'import, transformées par instance      |
| Ombres locales           | Un atlas de 4096², tuiles réparties par importance                 |
| Transparence             | Tri par distance, passe après l'opaque, sans écrire la profondeur  |
| Prépasse                 | Profondeur, mouvement et normales avant l'ombrage                 |
| Anticrénelage            | Temporel : projection jitterée, historique borné par le voisinage  |
| Post-traitements         | Bloom, occlusion ambiante, table de couleurs, vignette, grain     |
| Réglages de l'image      | Sur le composant Camera, à côté de l'exposition                   |
| Interfaces               | Entités et composants : `Canvas`, `UiRect`, `UiImage`, `UiText`... |
| Placement                | Ancrages et marges, puis conteneurs ligne, colonne et grille       |
| Texte                    | Police cuite en atlas de distances signées, nette à toute taille   |
| Entrées de l'interface   | Souris, clavier et manette, actions nommées lues par le jeu        |
| Saisie de texte          | `UiInput` : sélection, presse-papiers, mot de passe, multiligne    |
| Thèmes d'interface       | Asset `.dvxtheme` de styles nommés, référencé par le canevas       |
| Liaison de données       | `UiBinding` écrit un champ de composant dans un texte              |
| Journal                  | Fichier par lancement, le précédent gardé, lisible pendant le jeu  |
| Plantages                | Pile symbolisée dans le journal et minidump à côté                 |
| Manettes                 | SDL Gamepad, quatre manettes, sticks avec zone morte               |
| Éditeur de texte         | Coloration dessinée par-dessus le champ ImGui                     |
| Autocomplétion           | Mots-clés, noms du moteur et identifiants du fichier              |

## Architecture cible

Chaque module est une bibliothèque d'objets CMake avec son API publique dans
`engine/include/devex/<module>/` et son implémentation dans `engine/src/<module>/`. Tous les
modules forment **une seule bibliothèque partagée**, `devex-engine` (`Devex::Engine`), que lient
les programmes, les tests et les modules de jeu : ils partagent ainsi un seul état du moteur
(registres, journal, renderer), et un jeu voit la même API C++ que l'éditeur. Elle n'exporte
que ce que ses en-têtes marquent **`DEVEX_API`** (`devex/core/Export.hpp`) : les classes et les
fonctions de `engine/include`, et celles des en-têtes internes que les tests éprouvent. Ses
parties sont compilées avec `DEVEX_BUILDING_ENGINE`, qui exporte ; tout le reste importe. Les
types du moteur déclarent leur réflexion par `DEVEX_DECLARE_ENGINE_REFLECTION`, exportée, et les
jeux par `DEVEX_DECLARE_REFLECTION` pour les leurs. Exporter tout (`WINDOWS_EXPORT_ALL_SYMBOLS`)
avait mené à 65 667 symboles, au-delà des 65 535 qu'une DLL Windows peut exporter, dont 17 800
instanciations de la bibliothèque standard et des dépendances que personne n'importait ; il en
reste un peu plus de 3 300. Une classe exportée génère toutes ses copies implicites : celles qui
ne se copient pas le disent (`= delete`). Les avertissements C4251 et C4275, sur les membres de la
bibliothèque standard des classes exportées, sont coupés : moteur et modules sont compilés par le
même compilateur, dans la même configuration, ce qu'un module de jeu doit respecter.
Les dépendances sont strictement descendantes : un module ne connaît jamais un module
situé au-dessus de lui, et le graphe reste sans cycle.

| Module          | Rôle                                                                      | Dépend de                     |
| --------------- | ------------------------------------------------------------------------- | ----------------------------- |
| `Core`          | types, `Result<T>`/`Error`, assert, log, handles, UUID, allocateurs, jobs | —                             |
| `Math`          | alias `Vec3`, `Mat4`, `Quat`…, conventions de repère et de profondeur     | Core, GLM                     |
| `Platform`      | fenêtre, entrées, temps, dialogues, bibliothèques partagées, processus    | Core, SDL3                    |
| `Reflection`    | description des champs (`TypeInfo`, `DEVEX_REFLECT`, `ValueKind`)         | Core, Math                    |
| `Serialization` | format texte `.dvx*` (sections, valeurs) ; plus tard archives cookées     | Core                          |
| `Asset`         | `AssetId`, données CPU (maillages, textures, matériaux, modèles, clips audio et d'animation, polices), `.dvxasset`, projet, paquet `.dvxpak` | Core, Math, Reflection, Serialization, zstd |
| `AssetImport`   | base d'assets, `.dvxmeta`, importeurs (textures, `.dvxmat`, glTF, FBX, OBJ, sons, polices, courbes) | Asset, Scene, Audio, fastgltf, ufbx, basisu, stb, efsw |
| `Render`        | façade `Renderer` / `RenderWorld` ; tout `Vk*` reste dans `src/render/vulkan` | Core, Math, Platform, Asset, Vulkan |
| `Scene`         | entités, sparse sets, hiérarchie, composants intégrés, `.dvxscene`, sous-arbres, préfabs | Core, Math, Reflection, Serialization, Asset |
| `Audio`         | clips, mixage et groupes, sources et écouteur de la scène                | Core, Math, Asset, Scene, miniaudio, stb |
| `Animation`     | clips d'animation, échantillonnage, fondus, squelettes des `Animator`, tweens, images des sprites | Core, Math, Asset, Scene |
| `Particles`     | émetteurs et traînées de la scène, simulés sur les workers              | Core, Math, Asset, Scene             |
| `Physics`       | corps, colliders et personnages 3D de la scène, simulés par Jolt         | Core, Math, Asset, Scene, Jolt       |
| `Physics2D`     | corps, colliders, tuiles et personnages 2D de la scène, simulés par Box2D | Core, Math, Asset, Scene, Box2D     |
| `Navigation`    | cuisson des maillages de navigation, agents, obstacles et requêtes de chemin | Core, Math, Asset, Scene, Recast & Detour, zstd |
| `Ui`            | placement des canevas, mise en page du texte, survol et focus, dessin    | Core, Math, Asset, Scene, Render     |
| `Tools`         | panneaux ImGui, annulation, éditeur (viewport, gizmos, scènes, accueil)   | Core, Platform, Render, Scene, Audio, Animation, Particles, Physics2D, Navigation, Ui, AssetImport, ImGui |
| `Runtime`       | `Application`, boucle, mode éditeur et Play, modules de jeu, coroutines, `AssetManager`, extraction | tous les modules ci-dessus |

Au sommet : `devex-editor`, `devex-player` et les modules de jeu des projets.

Règles :

- aucun type `Vk*` ou `SDL_*` n'apparaît dans un header public hors de son module
  propriétaire, et les autres modules écrivent `devex::math::Vec3`, jamais `glm::vec3` ;
- `Scene` ne dépend pas de `Render` : `Runtime` extrait chaque frame les données de
  rendu depuis la scène vers le `RenderWorld` ;
- les importeurs vivent dans `AssetImport` et appartiennent aux outils (éditeur, cooker) ;
  `Runtime` s'en sert pendant le développement, un jeu exporté ne chargera que les `.dvxasset` ;
- `Render` ne dépend d'`Asset` que pour les données CPU (`MeshData`, `TextureData`), jamais de
  la base d'assets : `Runtime` résout les `AssetId` en handles du renderer ;
- un module nouveau arrive avec ses tests et une démonstration dans le bac à sable.
- aucune variable globale n'est partagée par un en-tête : chaque bibliothèque aurait sa copie.
  Ce qui doit être unique passe par une fonction du moteur, comme l'index des types de
  composants.

### Dépôt

```text
engine/        modules du moteur (include/ + src/)
managed/       Devex.Managed, l'API C# du moteur, compilée dans bin/managed
apps/editor/   devex-editor, l'éditeur
apps/player/   devex-player, qui lance un projet hors de l'éditeur
tools/         outils de build : devex-bindgen, qui génère les vues C# des composants C++
samples/       projets d'exemple : sandbox, le bac à sable des jalons, avec son code
shaders/       sources Slang du moteur, compilées dans bin/shaders
tests/         tests Catch2, un dossier par module, données dans tests/data
scripts/       outils de développement (assets d'exemple, liaisons C# générées)
third_party/   sources externes copiées (backend Vulkan d'ImGui), avec leur licence
cmake/         fonctions CMake partagées, outils de build (cmake/tools)
docs/          décisions et documentation
.github/       le workflow de l'intégration continue (GitHub Actions)
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
- **Render graph** (`src/render/vulkan/RenderGraph`) : une frame est une suite de passes
  qui déclarent comment elles utilisent chaque image (attachement couleur ou profondeur,
  lecture en fragment ou en compute, écriture storage, présentation). Avant chaque passe, le
  graphe émet les barrières synchronization2 depuis l'usage précédent de l'image, et nomme la
  passe pour les débogueurs quand la validation est active. Les images transitoires viennent
  d'un **pool propre à chaque contexte de frame**, gardées d'une frame à l'autre tant qu'elles
  servent : deux frames en vol ne partagent jamais une image. Pas encore d'élimination de
  passes ni de partage de mémoire entre images.
- **Frame** : ombres locales (si des lumières en projettent), prépasse (profondeur, mouvement et
  normales), occlusion ambiante (si demandée),
  ombres du soleil (4 couches d'une image `D32` 2048²), scène HDR `RGBA16F` avec les maillages
  puis le ciel, puis les surfaces transparentes triées, mesure de luminance (si exposition
  automatique), sélection (si demandée), masque des objets entourés (s'il y en a), anticrénelage
  temporel (si demandé), chaîne du bloom (si demandé), tonemapping et étalonnage vers la cible,
  overlay des outils, interface du jeu, puis ImGui sur le swapchain.
- **Cible** : sans `RenderWorld::viewport`, la scène est dessinée sur tout le swapchain.
  Avec, elle l'est dans une image de cette taille (format du swapchain, au plus 8192²) que les
  outils affichent dans un panneau : `Renderer::viewportTexture()` est un identifiant de
  texture ImGui fixe, remplacé pendant le dessin par le descriptor set ImGui de l'image de la
  frame (un par contexte de frame, recréé quand l'image change).
- **Surfaces d'interface** : `RenderWorld::uiSurfaces` porte des interfaces dessinées chacune dans
  une image à elle plutôt que sur la scène (`UiSurface` : identifiant, taille en pixels, couleur de
  fond, sommets, indices et lots, comme l'interface du jeu). Ce sont les panneaux de l'éditeur faits
  avec `Devex::Ui`. Chaque surface est une image transitoire du graphe (passe « Interface
  surface », au format de la cible avec une vue au format des outils), que les outils montrent par
  `Renderer::uiSurfaceTexture(id)`, un identifiant ImGui fixe remplacé pendant le dessin comme
  celui du viewport. Une commande ImGui dont la surface n'a pas été dessinée dans la frame est
  sautée (rectangle de découpe vide) plutôt que de lier une texture qui n'existe pas. Les surfaces
  ne sont dessinées qu'avec les outils. Elles sont écrites par la vue UNORM de leur image, le shader
  encodant les couleurs pour l'écran avant le mélange (`displaySpace`) : le mélange se fait alors
  en espace d'affichage, comme celui d'ImGui, et des lettres claires sur un panneau sombre gardent
  la même finesse que celles d'ImGui à côté (mélangées en lumière, elles paraissaient plus
  grasses). L'interface des jeux, elle, reste mélangée en lumière.
- **Sélection à la souris** (*picking*) : `RenderWorld::pick` demande les objets visibles dans un
  rectangle de l'image, un pixel pour un clic. Une passe dessine tous les maillages dans une cible
  `R32_UINT` de la taille du rectangle (réduite à 512 pixels de côté au plus) avec une projection
  qui étire ce rectangle sur toute la cible, en écrivant `MeshInstance::objectId` (0 = rien) et en
  respectant le mode alpha `mask` ; les valeurs sont copiées dans un buffer visible par le CPU et
  lues quand la frame est terminée, en général deux frames plus tard
  (`Renderer::takePickResults`) : l'objet au milieu du rectangle, et chaque objet vu, une fois.
  Un grand rectangle regardé en moins de pixels qu'il n'en couvre peut manquer un objet de
  quelques pixels. `Runtime` identifie chaque instance par l'index de son entité plus un.
- **Overlay des outils** : lignes et triangles colorés (`RenderWorld::sceneLines`,
  `overlayLines`, `overlayTriangles`) dessinés en mélange alpha après le tonemapping. Les
  lignes de scène sont cachées par les surfaces plus proches : elles testent la profondeur de la
  scène, et leur profondeur est légèrement avancée pour rester
  visibles sur les surfaces où elles reposent. Les autres passent devant tout. Les instances
  `outlined` sont dessinées dans un masque `R8`, puis un plein écran trace le contour orange des
  pixels hors masque à moins de deux pixels de lui. Lignes d'un pixel de large : pas encore de
  lignes épaisses ni anticrénelées.
- **Profondeur** : reverse-Z à far plane infini (`math::perspectiveReverseZ`), `D32_SFLOAT`
  effacée à 0 et test `GREATER_OR_EQUAL`. La projection de `Math` garde Y vers le haut ; le
  renderer applique la correction du clip space Vulkan (Y vers le bas).
- **Slang** : `cmake/DevexShaders.cmake` compile chaque shader d'entrée (`prepass`, `mesh`,
  `shadow`, `sky`, `ao`, `taa`, `bloom`, `tonemap`, `luminance`, `ibl`, `pick`, `overlay`, `ui`)
  en un `.spv` contenant tous ses points d'entrée
  (`-fvk-use-entrypoint-name`), avec des matrices column-major comme GLM ; les modules
  importés (`common`, `pbr`) entrent dans le depfile. Une erreur de shader est une erreur de
  build. L'API Slang servira plus tard au rechargement à chaud dans l'éditeur. En Vulkan,
  `SV_VertexID` ne compte pas le premier sommet du dessin : un dessin qui commence au milieu
  d'un buffer reçoit l'adresse de ce premier sommet.
- **Données GPU** : vertex pulling. Les shaders lisent sommets et données de scène via des
  *buffer device addresses* passées en push constants (112 octets pour un dessin, 184 pour la
  prépasse qui ajoute la place précédente de l'instance ; Vulkan 1.4 en garantit 256) ; pas de
  vertex input state. Sommets de 48 octets : position, normale, UV et
  tangente. Les dispositions mémoire C++ et Slang sont
  vérifiées par `static_assert` (`src/render/vulkan/GpuData.hpp`). Le descriptor set
  global bindless porte les textures (voir ci-dessous).
- **Maillages** : `Renderer::createMesh` valide les données, crée les buffers et remplit un buffer
  de staging VMA, puis renvoie aussitôt un `MeshHandle` générationnel (`core::SlotMap`) ; la
  copie vers le GPU vient avec les frames suivantes (voir *Envois au GPU*). `destroyMesh` retarde la libération jusqu'à ce qu'aucune frame en
  vol ne puisse encore l'utiliser. Faces avant dans le sens antihoraire, back-face culling.
  Un maillage a des **sous-maillages** (plages d'indices) ; le `RenderWorld` contient une
  instance par sous-maillage avec son matériau.
- **Envois au GPU** : ni `createMesh` ni `createTexture` n'attendent plus le GPU. Chaque création
  laisse une copie en attente ; au début d'une frame, après l'attente de sa fence, le renderer
  prend les copies qui tiennent dans son budget (`RendererConfig::uploadBytesPerFrame`, 64 Mo ;
  la première passe toujours, pour qu'une grosse texture n'attende pas indéfiniment) et les
  enregistre dans une passe *Uploads* en tête du render graph, suivie d'une barrière vers toutes
  les lectures ; les buffers de staging sont libérés quand la frame est finie. Jusqu'à sa copie,
  un maillage n'est pas dessiné (ses instances sont retirées de la frame), une texture est
  échantillonnée comme la texture par défaut (blanche ou normale plate) et les éléments
  d'interface qui la dessinent sont laissés de côté ; un ciel attend d'être copié pour être cuit
  en IBL. `Renderer::isReady` dit si c'est fait ; les statistiques donnent les copies en attente
  et ce que la dernière frame a copié. Une copie créée pendant une frame est enregistrée dans la
  même, avant ses passes : ce qui tient dans le budget s'affiche aussitôt. Les textures propres
  au renderer (blanche, normale plate) et la cuisson de l'IBL restent synchrones. Une file de
  transfert dédiée viendra plus tard.
- **Textures** : `Renderer::createTexture` prend tous les niveaux de mip d'un `TextureData`
  (RGBA8, BC5, BC7, RGBA16F) et renvoie un `TextureHandle`. Elles vivent dans **un descriptor
  set global bindless** (set 0 : tableau de `Texture2D` indexé, `PARTIALLY_BOUND` et
  `UPDATE_AFTER_BIND`, 8192 emplacements au plus, sampler linéaire, répétition, anisotrope
  x16), qui porte aussi l'IBL, la table BRDF et le ciel. Le set 1, un par contexte de frame,
  porte ce que la frame produit : carte d'ombres, couleur de scène, masque de sélection,
  mouvement et normales de la prépasse, occlusion ambiante, historique de l'anticrénelage,
  profondeur, image résolue et les cinq niveaux de la chaîne du bloom. Les
  emplacements 0 et 1 sont une texture blanche et une normale plate qui remplacent les textures
  absentes. Une texture détruite garde son emplacement jusqu'à la fin des frames en vol, puis
  l'emplacement repointe vers le blanc avant d'être réutilisé.
- **Matériaux** : `createMaterial` / `updateMaterial` / `destroyMaterial` sur un `MaterialDesc`
  (paramètres glTF avec des `TextureHandle`). Le renderer en tire une table `GpuMaterial` de 80
  octets dont **chaque frame en vol garde sa copie** (buffer adressé par `SceneData`), recopiée
  quand un matériau ou une texture change. Le shader utilise tous les paramètres : couleur de
  base, métal et rugosité (rugosité bornée à 0,045), normal map (BC5, Z reconstruit, échelle),
  occlusion (sur la lumière indirecte, multipliée par celle mesurée à l'écran), émission et mode
  alpha `mask` (discard, ombres comprises) ou `blend` (dessiné dans la passe transparente, trié,
  sans ombre). Les matériaux `doubleSided` utilisent un second pipeline sans culling et éclairent
  la face arrière avec la normale retournée. Un matériau par défaut gris clair sert aux instances
  sans matériau.
- **Normales** : transformées par la matrice des cofacteurs (signe du déterminant compris), juste
  sous échelle non uniforme ; la tangente suit la matrice du modèle et son signe s'inverse
  sous un miroir.
- **Éclairage** : BRDF GGX, visibilité de Smith corrélée en hauteur, Fresnel de Schlick,
  diffus lambertien ; pas encore de compensation multi-diffusion.
  - *Soleil* : illuminance en lux (couleur × température × intensité), ombré par cascades.
  - *Lumières locales* : intensité en candelas (lumens / 4π, y compris pour les spots),
    atténuation en carré inverse avec une fenêtre qui s'annule à la portée, cônes des spots
    lissés entre les angles intérieur et extérieur.
  - *Clusters* : grille de 16 × 9 tuiles et 24 tranches logarithmiques entre le plan proche et
    500 m ; chaque frame, le CPU teste la sphère d'influence de chaque lumière contre les boîtes
    des clusters (`src/render/Lighting`) et envoie la liste par frame.
  - *Environnement* : un ciel équirectangulaire HDR (ou blanc uniforme) est converti en
    cubemap 512², préfiltré pour 6 rugosités (GGX, échantillonnage d'importance filtré) et
    intégré en irradiance 32², par des compute shaders exécutés quand la texture du ciel
    change ; la table BRDF split-sum (128²) est calculée au démarrage. `color`, `intensity`
    (nits) et `rotation` s'appliquent sans nouveau calcul. Le ciel est dessiné derrière la
    scène depuis la texture elle-même.
- **Ombres** : 4 cascades réparties entre découpage logarithmique et uniforme (λ = 0,75) jusqu'à
  `shadowDistance`, chacune ajustée sur la sphère englobante de sa tranche et alignée sur les
  texels pour ne pas scintiller ; biais de pente dynamique, décalage le long de la normale d'un
  texel et demi, filtrage 3 × 3 par comparaisons bilinéaires, fondu sur le dernier dixième de
  la distance. Depth clamp quand le GPU le permet.
- **Exposition** : EV100 → échelle `1 / (1,2 × 2^EV100)`. Toute la lumière est **pré-exposée**
  dans le shader, ce qui garde les valeurs dans la plage des demi-flottants. L'émission des
  matériaux est relative à l'exposition : une couleur émissive de 1 s'affiche claire quelle que
  soit la lumière. En automatique, un compute shader relève la luminance sur une grille de
  64 × 36 ; le CPU la relit quand la frame est terminée, en fait la moyenne géométrique entre les
  10e et 90e centiles, vise le gris moyen à 18 %, borne entre `minEv100` et `maxEv100` et
  s'adapte exponentiellement (`adaptationSpeed`).
- **Tonemapping** : AgX (sigmoïde du look par défaut de Blender), Khronos PBR Neutral, ACES
  (approximation de Narkowicz) ou aucun, choisi sur la caméra ; sortie linéaire, encodée sRGB
  par le swapchain. La même passe ajoute le bloom et applique l'étalonnage (voir *Transparence
  et post-traitements*).
- **Limites actuelles** : culling par tronc de vue seulement (rien n'est enlevé parce qu'il est
  caché derrière autre chose, et chaque instance est testée une par une sur le CPU), pas de
  réflexions locales, transparence triée par instance (deux surfaces qui s'entrecroisent restent
  fausses), et traînées possibles derrière un objet très rapide, que l'anticrénelage temporel ne
  rattrape pas toujours.
- **GPU requis en plus** : descriptor indexing (tableaux runtime, partially bound, update after
  bind, indexation non uniforme), compression BC, et une file graphique qui fait aussi du
  compute.
- Une RHI ne sera extraite que lorsqu'un second backend (DX12, Metal) aura un besoin
  réel.

### Monde et scènes

- L'utilisateur manipule des `Entity` avec des composants (`Transform`, `Camera`,
  `MeshRenderer`…) organisés en hiérarchie.
- **Entités** : en mémoire, un handle générationnel (`scene::Entity`) qui devient invalide
  à la destruction ; chaque entité a aussi un **UUID** stable et un nom. Les fichiers et
  les références durables utilisent l'UUID (`Scene::findEntity`).
- **Stockage** : sparse sets maison. Un `ComponentPool<T>` par type garde les composants
  contigus ; retirer un composant déplace le dernier à sa place. `scene.view<A, B>()`
  parcourt le plus petit des pools concernés. Ajouter ou retirer des composants de ces
  types pendant l'itération invalide la vue.
- **Types de composants** : un index attribué au premier usage, par le moteur, à partir du nom
  décoré du type (`typeid(T).raw_name()`, qui distingue les espaces de noms anonymes) : l'éditeur
  et les modules de jeu s'accordent sur les index, qui ne sont pas stables d'une exécution à
  l'autre. Chaque pool retient la bibliothèque dont le code le gère (`moduleAnchor`), pour être
  détruit avant qu'elle ne soit déchargée.
- **Copie** : `Scene::clone` copie entités, noms, hiérarchie et composants **avec les mêmes
  handles** : un handle obtenu dans la scène éditée désigne la même entité dans la copie jouée.
  Les composants doivent être copiables. `Scene::entityAtIndex` retrouve l'entité vivante d'un
  index, comme ceux que renvoie la sélection sur le GPU.
- **Hiérarchie** : parent, premier et dernier enfant, frères précédent et suivant ; l'ordre
  des enfants et des racines est conservé. `setParent` refuse les cycles et garde la
  transformation locale. Détruire une entité détruit ses descendants.
- **Modèles** : `scene::instantiateModel` crée une entité racine puis une entité par nœud du
  modèle (`Transform`, et `MeshRenderer` si le nœud a un maillage). Ce sont des copies : maillages
  et matériaux restent liés par `AssetId` et suivent les réimports, pas la hiérarchie.
- **Préfabs** : voir la section *Préfabs*.
- **Transformations** : `Transform` (position, rotation, échelle locales) ;
  `Scene::updateTransforms` calcule `WorldTransform` parents d'abord, chaque frame, après
  `onUpdate`. Une entité sans `Transform` transmet celle de son parent.
- **Composants intégrés** : `Transform`, `WorldTransform` (calculé, jamais sauvegardé),
  `MeshRenderer` (maillage et matériau qui remplace ceux des sous-maillages s'il est valide),
  `Camera` (la première `primary` est utilisée ; exposition et tonemapping), `DirectionalLight`
  (couleur, température, illuminance en lux, ombres), `PointLight` et `SpotLight` (couleur,
  température, puissance en lumens, portée, angles des cônes), `Environment` (ciel HDR,
  couleur, luminance en nits, rotation ; le premier est utilisé).
- **Réflexion** : chaque composant déclare ses champs avec `DEVEX_DECLARE_REFLECTION` (header)
  et `DEVEX_REFLECT` (source). Types de valeurs : bool, int32, uint32, float, string, vec2,
  vec3, vec4, quat, UUID, `AssetId`, énumérations (`EnumNames<T>` liste les noms, écrits
  en texte dans les fichiers) et références d'entités ; et des listes de toutes ces valeurs sauf
  bool (`std::vector<T>`, décrit par `ListOps` : taille, élément, insertion, suppression). Des
  indications guident l'inspecteur (`FieldHints`) : type d'asset attendu, couleur, angle affiché
  en degrés ; `hidden` cache un champ de l'inspecteur, `runtime` marque l'état que le moteur tient
  pendant le jeu (ni sauvegardé, ni copié, ni montré). La réflexion connaît aussi la taille des types et l'emplacement de chaque champ,
  que les vues C# utilisent. MSVC 19.51 ne fournit pas encore `<meta>` (réflexion C++26) ; ces
  déclarations pourront alors être générées.
- **Références d'entités** : un champ `scene::EntityRef` désigne une autre entité de la même scène
  par son **UUID**, pas par son handle, si bien qu'il survit à l'enregistrement, à la copie jouée, à
  l'annulation et aux préfabs : en chargeant une instance, les références entre les entités du
  préfab sont remplacées par les UUID dérivés de l'instance (sur le texte, avant les
  modifications), celles qui sortent du préfab restent telles quelles et ne désignent rien.
  `Scene::resolve` donne l'entité (invalide quand la référence est vide ou l'entité détruite),
  `Scene::reference` la référence. Fichiers : `entity("<uuid>")`, ou `entity()`. L'inspecteur les
  choisit dans un menu de la scène ou par glisser-déposer depuis l'arbre.
- **Listes** : écrites `list(a, b, c)` ; une valeur invalide laisse la liste inchangée.
  L'inspecteur montre leur taille, un bouton pour ajouter un élément et un pour retirer chacun ;
  chaque changement est une étape d'annulation de toute la liste, et une instance de préfab
  remplace la liste entière quand elle la modifie.
- **Composants du jeu** : une struct, sa réflexion, puis `scene::registerComponent<T>()` (ou
  `GameRegistry::component<T>()` dans un module de jeu). La déclaration de réflexion doit être dans
  l'espace de noms du type, où la recherche dépendante des arguments la trouve.
- **Composants conservés** : un composant d'un type non enregistré, comme celui d'un module de
  jeu absent ou en cours de rechargement, est gardé tel qu'il a été lu (`PreservedComponents`,
  ses sections de fichier) : il est réécrit à l'enregistrement, copié avec la scène, affiché grisé
  par l'inspecteur, et recréé par `restorePreservedComponents` dès que son type est enregistré.
  `preserveComponentPool` fait l'inverse pour un pool entier.
- **Rendu de la scène** : `Runtime` extrait chaque frame la caméra, la première lumière
  directionnelle, toutes les lumières locales, le premier environnement et une instance par
  sous-maillage de chaque `MeshRenderer` dont le maillage est disponible dans l'`AssetManager`,
  convertit les unités (`render/Photometry.hpp`), puis appelle `onRender` pour les ajouts
  éventuels.
- Repère : **Y-up, main droite**, +X droite, -Z avant, mètres, angles en radians.
  glTF s'importe sans conversion ; FBX et OBJ sont convertis à l'import (voir *Assets*).

### Formats de fichiers

Projet utilisateur :

```text
MyGame/
  MyGame.dvxproj              # [project format=1 name="MyGame" startup_scene="res://…"]
  code/                       # module de jeu, facultatif
    CMakeLists.txt            # find_package(Devex) puis devex_add_game_module
    Game.cpp
  assets/
    scenes/Main.dvxscene
    scenes/Main.dvxscene.dvxmeta
    prefabs/hero.dvxprefab    # à venir
    materials/rock.dvxmat
    materials/rock.dvxmat.dvxmeta
    ui/game.dvxtheme          # styles des interfaces, référencé par les canevas
    curves/hover.dvxcurve     # courbe dessinée qui adoucit les tweens
    models/hero.glb
    models/hero.glb.dvxmeta   # UUID, options d'import et sous-assets
  .devex/                     # cache d'import local, ignoré par Git
    imported/<uuid>.dvxasset  # données cuites, une par asset
    sources/<uuid>.dvxsource  # dernier import de chaque fichier source
    editor.dvx                # dernière scène ouverte et caméra de l'éditeur
    code/<configuration>/     # build du module de jeu (bin/Game.dll)
    code/modules/             # copies chargées du module
```

Format texte commun à tous les `.dvx*` (`Devex::Serialization`) : des sections
`[type clé=valeur …]` suivies de lignes `clé = valeur`, commentaires `#`. Les valeurs sont
des booléens, entiers, réels (écrits sous leur forme la plus courte qui relit la même
valeur), chaînes avec échappements, ou appels comme `vec3(0, 1, 0)`. Une en-tête et une
propriété tiennent chacune sur une ligne ; les erreurs donnent ligne et colonne.

Scène (`.dvxscene`, format 1) :

```text
[scene format=1 kind="3d"]

[entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23" name="Player"]
parent = "b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44"

[component type="Transform"]
position = vec3(0, 1, 0)
rotation = quat(0, 0, 0, 1)
scale = vec3(1, 1, 1)

[component type="MeshRenderer"]
mesh = asset("00000000-0000-0000-0000-000000000001")
```

- `kind` dit pour quel écran de l'éditeur la scène est faite, `"2d"` ou `"3d"` (voir *Sprites et
  jeux 2D*) ; absent, la scène est classée à la lecture. Le format reste 1.
- Les sections `component` appartiennent à l'entité qui les précède. Les parents sont
  écrits avant leurs enfants, dans l'ordre de la hiérarchie ; les quaternions en `x, y, z, w`.
- Un type de composant ou un champ inconnu est ignoré avec un avertissement (un moteur
  plus ancien ouvre une scène plus récente) ; une valeur invalide est une erreur.
- Les références entre fichiers passent par **UUID**, jamais par chemin : renommer ou
  déplacer un asset ne casse rien. Les primitives intégrées ont des UUID réservés :
  `…0001` cube, `…0002` sphère, `…0003` plan.
- Les chemins affichés utilisent le schéma `res://`, relatif au dossier du `.dvxproj`
  (`res://assets/models/hero.glb`).
- L'export d'un jeu empaquettera les `.dvxasset` du cache : ce sont déjà les données **binaires
  cuites** chargées sans parsing.
- Une scène est un **asset** : son `.dvxmeta` lui donne un `AssetId`, son import vérifie qu'elle
  se charge (sans charger ses préfabs, importés à part) et range son texte dans un artefact
  `scene`, pour qu'un jeu charge un niveau par identifiant.
- Une **instance de préfab** est une section `entity` avec `prefab = asset("…")`, suivie de ses
  sections `override` (voir *Préfabs*). Le format reste 1 : un moteur plus ancien ignore
  `override` avec un avertissement et charge l'instance vide.

Import (`.dvxmeta`, format 1), écrit par la base d'assets et versionné :

```text
[asset format=1 uuid="fa17e48e-77a9-48d6-9cb6-3075791e73bf" importer="gltf"]
compress_textures = true
texture_quality = "normal"

[subasset type="material" key="Wood" uuid="7eb218af-8a08-4b1b-9029-bd3c909fd943"]
[subasset type="mesh" key="Crate" uuid="0a4571b3-aa2e-4421-a385-09466c6dee9c"]
```

Matériau (`.dvxmat`, format 1), toutes les propriétés sont facultatives :

```text
[material format=1]
base_color = vec4(1, 1, 1, 1)
base_color_texture = asset("0d958a7c-6376-440a-bd9b-4276675c6896")
metallic = 0
roughness = 0.9
emissive = vec3(0, 0, 0)
alpha_mode = "opaque"        # "mask" utilise alpha_cutoff, "blend"
double_sided = false
```

Les autres propriétés sont `metallic_roughness_texture`, `normal_texture`, `normal_scale`,
`occlusion_texture`, `occlusion_strength`, `emissive_texture` et `alpha_cutoff`. Sans texture,
métal 0 et rugosité 1 par défaut (un import glTF écrit ses propres valeurs).

Courbe (`.dvxcurve`, format 1), deux clés au moins, dans l'ordre du temps ; le temps va de 0 au
début du tween à 1 à sa fin, la valeur de 0 à la valeur de départ à 1 à la valeur d'arrivée, et
les pentes (`in`, `out`, en valeur par unité de temps, 0 par défaut) règlent la spline d'Hermite
entre deux clés :

```text
[curve format=1]
[key time=0 value=0 in=0 out=0]
[key time=0.6 value=1.08 in=0.9 out=0.9]
[key time=1 value=1 in=0 out=0]
```

Artefact (`.dvxasset`) : en-tête `DVXA`, type d'asset, version de disposition du type, puis les
données en little-endian (`serialization::BinaryWriter`). Changer la disposition d'un type
augmente sa version ; changer ce que produit un importeur augmente la version de l'importeur.
Dans les deux cas, les sources concernées sont réimportées.

Paquet d'un jeu exporté (`.dvxpak`, format 1) : en-tête `DVXPAK` avec la position de l'index, les
artefacts les uns après les autres, puis l'index. L'index donne le texte du `.dvxproj` du jeu,
son icône, puis un enregistrement par asset : identifiant, type, nom, chemin `res://` du fichier
source pour les assets principaux, asset source, et où sont ses octets. Un artefact est compressé
avec zstd quand cela lui fait gagner au moins un dixième. L'index est écrit en dernier, pour que
les assets s'écrivent au fil de leur lecture.

### Export d'un jeu

- **Résultat** : un dossier qui tourne sans l'éditeur, sans le projet et sans les sources :
  `<Jeu>.exe` (le lecteur `devex-player` renommé, son icône remplacée, et basculé en application
  fenêtrée pour qu'aucune console ne s'ouvre derrière le jeu), `<Jeu>.dvxpak`,
  `Game.dll`, `devex-engine.dll` et les autres bibliothèques du build du moteur, les shaders, le
  runtime C++ quand le build du moteur le fournit (`bin/redist`, copié par CMake en Release), et
  `devex-export.txt` qui marque le dossier comme un export. Un export Debug emporte aussi
  `resources/` pour la surcouche d'outils (F1).
- **Source des assets** : `asset::AssetSource` est l'interface que le jeu voit (réglages du projet,
  `find`, `assets`, `findByPath`, `loadArtifact`, `sceneText`). La base d'assets l'implémente
  pendant le développement, `asset::PackageReader` une fois le jeu exporté. Le lecteur mappe le
  paquet en mémoire (`core::MappedFile`) et décompresse un asset au chargement ; rien n'est
  importé au lancement du jeu.
- **Ce qui est exporté** : la scène de démarrage, les scènes cochées, et tout ce qu'elles
  référencent, de proche en proche (préfabs, maillages, matériaux, textures, autres scènes) en
  suivant les `asset(...)` des scènes et les références des données cuites ; plus les dossiers
  marqués « toujours inclus », pour les assets que le code charge lui-même. Un asset référencé mais
  absent est signalé par un avertissement, les assets intégrés (primitives) viennent du moteur.
- **Étapes** (`runtime/GameExport.hpp`) : le plan est fait sur le thread de la base d'assets
  (`planExport` : réglages, liste des assets et de leurs artefacts, scènes, dossiers, icône), puis
  l'export lui-même (`exportGame`) peut tourner ailleurs : compilation du code du jeu pour le build
  choisi, collecte des dépendances, écriture du paquet, copie du moteur, icône de l'exécutable. Le
  dossier de sortie doit être vide, absent ou un export précédent, qui est remplacé.
- **Builds du moteur** : un jeu exporté embarque les binaires d'un build du moteur ; son code est
  compilé contre le même (`DevexConfig.cmake` donne sa configuration). L'éditeur liste les builds
  trouvés à côté du sien (`out/build/x64-debug`, `x64-release`...) ; l'export se fait en Release par
  défaut, dans un dossier de build à part du code (`.devex/code/<configuration>`).
- **Réglages du jeu** : `[window]` du `.dvxproj` (taille, plein écran, vsync, limite d'images,
  icône) ouvre la fenêtre du lecteur et du jeu exporté ; `[export]`, `[export_scene]` et
  `[export_folder]` gardent le dossier de sortie, la configuration, les scènes et les dossiers
  inclus, pour que l'éditeur et la ligne de commande exportent la même chose.
- **Interface** : *Project > Export Game…* montre l'exécutable, le dossier, la configuration, les
  scènes, les dossiers inclus, la progression puis le résultat, avec *Open Folder* et *Run Game*.
  L'export tourne sur son propre thread, après les imports et les compilations en cours.
  `devex-editor --export <projet.dvxproj> [--output <dossier>] [--configuration <Release|Debug>]`
  fait la même chose sans fenêtre, pour les scripts et l'intégration continue.
- **Icône** : une texture du projet ; l'export la redimensionne en 256 pixels pour la fenêtre (dans
  le paquet) et écrit les tailles 16 à 256 dans les ressources de l'exécutable
  (`platform::setExecutableIcon`, `UpdateResource` sur Windows).
- **Scènes du jeu** : `SystemContext::sceneToLoad` demande une scène depuis un système, chargée à la
  fin de la frame (physique recréée, systèmes `Start` rejoués) ; `Application::loadScene` la charge
  tout de suite. La version de l'API des jeux passe à 3.
- **Sans console** : le lecteur reste une application console, pratique pendant le développement ;
  l'export change seulement le sous-système dans l'en-tête de l'exécutable copié
  (`platform::setWindowedApplication`, qui lit et écrit des octets et marche donc depuis n'importe
  quel système). Un jeu fenêtré qui ne peut pas démarrer (paquet absent, GPU refusé) montre sa
  première erreur fatale dans une boîte de dialogue, avec le chemin du journal.

### Journal et plantages

- **Le problème** : `devex-player` ne laissait aucune trace. Un jeu qui s'arrêtait net ne disait ni
  pourquoi ni où, et le journal, écrit dans un tampon, disparaissait avec le processus.
- **Journal** : `core::LogFile` reçoit le journal dans un fichier dès la première ligne, et renomme
  celui du lancement précédent en `<nom>.previous.log` : c'est celui qui dit pourquoi le jeu s'est
  arrêté quand on le relance pour regarder. Chaque ligne est écrite sur le disque aussitôt (un
  processus tué de l'extérieur garde ainsi tout son journal), et le fichier reste lisible pendant
  que le programme tourne. Un projet joué écrit dans `.devex/logs/player.log` ; un jeu exporté dans
  `%APPDATA%\<Jeu>\logs\game.log`, toujours inscriptible même si le jeu est installé dans
  Program Files ; l'éditeur dans `%APPDATA%\Devex\Editor\logs\editor.log`.
- **Plantages** : `platform::installCrashHandler` attrape, pour tout le processus, ce qui le tuerait
  sans un mot : violations d'accès et autres exceptions matérielles, `abort`, `std::terminate`,
  appels de fonctions virtuelles pures et paramètres invalides du runtime C. Le rapport nomme
  l'exception, l'adresse fautive, puis la pile d'appels (`StackWalk64` de dbghelp) avec, pour chaque
  appel, le module et son décalage, la fonction et la ligne quand les `.pdb` sont à côté des
  binaires. Il va sur la sortie d'erreur et dans le journal, directement dans le fichier, sans
  passer par le logger qui pouvait être occupé au moment du plantage. Un **minidump** (threads,
  piles et mémoire qu'elles désignent, environ un mégaoctet) est écrit à côté du journal ; Visual
  Studio l'ouvre à l'endroit où le processus s'est arrêté. Tout ce que le gestionnaire utilise est
  réservé d'avance, et 64 Kio de pile lui sont garantis pour survivre à un débordement de pile.
  Le message du runtime C sur `abort` est coupé (`_set_abort_behavior`) : le rapport le remplace,
  et en Debug c'était une boîte de dialogue qui retenait le processus avant lui.
- **Symboles** : les builds Release écrivent aussi leurs `.pdb` (`/Zi`, `/DEBUG` avec `/OPT:REF` et
  `/OPT:ICF`, donc le même code optimisé), sans que l'export les copie : les rapports des jeux livrés
  ne donnent que modules et décalages, que les `.pdb` gardés du build permettent de retrouver.
- **Code du jeu périmé** : le lecteur vérifie, comme l'éditeur, que le module C++ a été compilé
  pour ce moteur (l'empreinte tient compte de la date de `devex-engine.dll`) et que l'assemblage C#
  est plus récent que ses sources et que `Devex.Managed.dll`. Sinon il les recompile avant d'ouvrir
  la scène, et refuse de démarrer si la compilation échoue. Charger un vieux module n'était pas
  seulement faux mais dangereux : un module enregistre ses composants par un modèle C++ compilé
  chez lui (`ComponentRegistry::add<T>`), qui écrit la structure `ComponentType` telle qu'il la
  connaît dans le registre du moteur. La version de l'API des jeux passe à 9 pour la même raison.

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
- **Éditeur** (`ApplicationConfig::editor`) : l'éditeur est un mode de `Application`, pas un
  programme à part. `devex-editor` est une application vide lancée dans ce mode ; le code d'un
  jeu y arrive par son module de jeu. Une `Application` compilée avec son propre code peut aussi
  y entrer. Dans l'éditeur, `onStartup` et `onShutdown` s'exécutent normalement, mais `onEvent`,
  les mises à jour, les systèmes et `onRender` seulement **en mode Play**, entre
  `onPlayStarted` et `onPlayStopped`. `isEditor()` et `isPlaying()` le disent au jeu ;
  `requestQuit()` arrête alors le mode Play.
- **Mode Play** : Play copie la scène éditée (`Scene::clone`) et joue la copie, vue par sa caméra
  principale ; Stop la jette et retrouve la scène éditée intacte. Pause suspend les mises à jour,
  le pas à pas exécute un pas fixe. Le pas fixe repart de zéro à chaque Play.
- **Projets** : `Runtime` possède la base d'assets et en change quand l'éditeur ouvre un autre
  projet : les assets chargés sont libérés (`AssetManager::setDatabase`), la scène est vidée,
  puis la nouvelle base est ouverte. Un fichier déposé sur l'éditeur est copié dans
  `assets/imported`.

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
- **Manettes** : jusqu'à quatre manettes SDL, ouvertes et fermées quand elles sont branchées,
  interrogées comme le clavier (`isGamepadButtonDown`, `wasGamepadButtonPressed`, `gamepadAxis`,
  `gamepadLeftStick`). Les boutons portent le nom de leur place (South, East…) plutôt que la
  lettre imprimée dessus, les sticks passent une zone morte, et une manette débranchée relâche ce
  qu'elle tenait.
- **Sources d'entrée** : `platform::InputSource` nomme un contrôle dans les fichiers : `key:Space`
  (position physique, comme `Key`), `mouse:Left`, `pad:South`, `axis:LeftTrigger` (un axe de
  stick ou une gâchette), `stick:Left` (un stick entier). `displayName` donne un nom lisible
  indépendant de la disposition ; `Platform::keyLabel` reste celui qui suit le clavier.
- **Actions** : les jeux lisent des actions nommées (« Jump », « Move ») plutôt que des touches.
  Elles sont dans les **réglages du projet** (`[input_action]` du `.dvxproj`, une seule table par
  projet, comme l'Input Map de Godot) et s'éditent dans la page **Input** de *Project Settings*,
  désormais en onglets (General, Physics, Audio, Input). Une action est un **bouton** (appuyé ou
  non), un **axe** (de -1 à 1) ou un **vecteur** (une direction de longueur 1 au plus, y vers le
  haut) ; chaque liaison dit dans quelle direction elle pousse (`positive`, `negative`, `up`,
  `down`, `left`, `right`), un stick donne un vecteur entier. Plusieurs touches s'additionnent,
  bornées (deux touches en diagonale ne vont pas plus vite qu'une), et une **zone morte** par
  action s'ajoute à celle des sticks, la suite de la course étant ramenée de zéro à un. Un axe ou
  un stick poussé à mi-course enfonce une action bouton. Toutes les manettes jouent (un seul
  joueur) : pour un axe, celle qui pousse le plus.
- **Lecture** : `runtime::InputActions` lit les périphériques une fois par frame, avant les
  mises à jour fixes : `isDown`, `wasPressed`, `wasReleased` (une frame, comme `Input`), `axis`,
  `vector`. Les systèmes C++ l'ont dans `SystemContext::actions` (version de l'API des jeux 11),
  les applications dans `Application::inputActions()`, le C# dans `Input.IsActionDown`,
  `WasActionPressed`, `WasActionReleased`, `ActionAxis`, `ActionVector` (une action inconnue lève
  une exception en C# et lit zéro en C++ ; amorce C# en version 8). Le clavier ne compte plus
  pendant qu'un champ de l'interface prend du texte : taper un nom ne fait plus avancer le joueur.
- **Contextes** : chaque action appartient à un contexte (« Gameplay » par défaut, qu'un projet
  reçoit sans l'écrire) ou à aucun (toujours lue). Le code les active ou les coupe
  (`setContextActive`, `Input.SetContextActive`) ; les actions d'un contexte coupé lisent
  relâché, avec leur `wasReleased`. Ils démarrent comme le projet le dit et appartiennent au jeu,
  pas à la scène : ils gardent leur état d'une scène à l'autre.
- **Réaffectation** : `listen(action, liaison)` (`Input.ListenForBinding`) attend la prochaine
  touche ou le prochain bouton et le met à la place de la liaison : une liaison clavier ou souris
  prend une touche ou un bouton de souris, une liaison de manette un bouton de manette ; Échap
  abandonne. Pendant l'attente, les actions lisent relâché, et la touche qui répond ne fait rien
  d'autre (ni l'action, ni le bouton de l'interface qui a le focus). `rebind`, `resetBindings`,
  `bindingLabel` (la touche telle que le clavier l'imprime) complètent l'API. Les sticks et
  gâchettes se choisissent dans les réglages du projet, pas par écoute.
- **Sauvegarde** : les liaisons qui diffèrent du projet vont dans `input.dvx`, dans le dossier de
  l'utilisateur au nom du jeu (`%APPDATA%\<jeu>` sous Windows, celui des journaux d'un jeu
  exporté ; `ApplicationConfig::userDirectory` le remplace), écrit dès qu'elles changent et relu
  au lancement ; celles que le projet n'a plus sont ignorées. Un jeu sans action n'y crée rien.
  Jouer dans l'éditeur et le jeu exporté partagent donc les liaisons du joueur.
- **Éditeur** : la page Input liste les contextes (actif au départ ou non), puis les actions
  dépliables : nom, genre, contexte, zone morte, liaisons choisies dans une liste filtrable par
  appareil (les touches sous leur nom sur ce clavier, « Z (W) » en AZERTY) ou par écoute de la
  prochaine touche ou du prochain bouton de manette. Les changements s'appliquent au prochain
  lancement du jeu. Le bac à sable joue Move, Look, Jump, Run et Fly par actions, coupe
  « Gameplay » tant qu'un menu est ouvert, et son écran de réglages réaffecte Sauter.

### Sauvegardes et réglages du joueur

- **Dossier du joueur** : ce que le joueur garde va dans `%APPDATA%\<jeu>` (le nom du projet ;
  `ApplicationConfig::userDirectory` le remplace, pour les tests) : `saves/`, `settings.dvx`,
  `input.dvx`. Jouer dans l'éditeur et le jeu exporté partagent donc ce dossier, comme le
  `user://` de Godot ; *Project > Open Player Data Folder* l'ouvre, et le supprimer fait repartir
  en nouveau joueur. `platform::userDataLocation` le trouve sans le créer : rien n'est écrit
  tant que le jeu ne garde rien.
- **Sauvegardes** (`runtime::SaveGames`, `SystemContext::saves`, `Saves` en C#) : une sauvegarde
  est un **objet du jeu** décrit comme un composant (struct réfléchie en C++, classe aux champs
  publics en C#, avec listes, énumérations, vecteurs et assets), écrit par ses champs dans un
  **emplacement** nommé (`"1"`, `"auto"`, `"quick"`) : `saves/<emplacement>.dvxsave`, au format
  texte du moteur, avec un en-tête (type, version donnée par le jeu, libellé, date, temps de jeu,
  scène). Côté C#, l'objet passe par une disposition en mémoire créée pour son type, comme pour
  les composants : un seul format pour les deux langages. Les champs que le type n'a plus sont
  ignorés et les nouveaux gardent leur valeur par défaut ; la version dit au jeu le reste.
- **Scène sauvegardée** : par défaut, la sauvegarde garde aussi la **scène qui joue**, telle que son
  fichier l'écrirait (entités créées et détruites, positions, champs des composants, instances de
  préfabs). La charger la remet à la place de la scène courante à la fin de la frame ; ses systèmes
  Start voient alors l'emplacement (`SaveGames::restoredSlot`, `Saves.RestoredSlot`) pour ne pas
  remettre à zéro ce que la sauvegarde a rapporté. Ce que les champs privés du C#, la physique en
  cours (vitesses) ou les sons tiennent n'est pas gardé.
- **Miniature** : une sauvegarde demande une petite image de la frame suivante,
  `Renderer::requestCapture` : la scène est tonemappée une seconde fois dans une image de 320×180
  au plus, sans l'interface ni les outils, copiée vers le CPU et écrite en PNG à côté de la
  sauvegarde deux frames plus tard. `SaveGames::thumbnail` (`SaveSlot.Thumbnail`) en fait une
  texture qu'une `UiImage` affiche (`AssetManager::setTexture`), refaite quand la sauvegarde change.
- **Sûreté** : écriture atomique ; la sauvegarde précédente de l'emplacement reste à côté
  (`.bak`), et un fichier abîmé la fait charger à sa place. Les noms d'emplacements sont limités
  (lettres, chiffres, espaces, tirets, points, soulignés). `slots()` liste les sauvegardes, la plus
  récente d'abord ; le temps de jeu reprend celui de la sauvegarde chargée.
- **Réglages du joueur** (`runtime::PlayerSettings`, `SystemContext::settings`, `PlayerSettings` en
  C# pour ne pas se heurter aux champs nommés « Settings » des jeux) : le moteur garde lui-même le
  plein écran, la synchronisation verticale et les volumes (Master et groupes), plus les valeurs
  que le jeu range par clé (booléens, entiers, nombres, textes : langue, sensibilité…), dans
  `settings.dvx`, écrit dès qu'ils changent et relu au lancement. Les volumes du joueur
  **multiplient** ceux du projet et du code du jeu (`AudioEngine::setPlayerGroupVolume`) : un jeu
  peut baisser sa musique pendant un dialogue sans perdre le choix du joueur ; l'éditeur remet les
  siens à 1 à l'arrêt. Le plein écran s'applique aussitôt hors de l'éditeur, la synchronisation
  verticale au lancement suivant (le lecteur les lit avant d'ouvrir sa fenêtre).
- **Bac à sable** : son écran de réglages garde volume, plein écran et nom ; le menu pause
  sauvegarde (le bouton dit « Sauvegardé ») et le menu principal montre la dernière sauvegarde,
  avec sa miniature, son libellé, sa date et le temps de jeu, que « Continuer » reprend. La version
  de l'API des jeux passe à 12, celle de l'amorce C# à 9.

### Outils

- **Module `Tools`** : panneaux Dear ImGui indépendants de Vulkan, affichés en overlay dans
  toute application avec **F1** (`ApplicationConfig::enableTools`, actif hors Release), ou
  autour du viewport de l'éditeur (`ToolsMode::Editor`).
- **Panneaux** (noms de Godot) : *Scene*, l'arbre des entités (icône colorée selon les
  composants, guides fins des enfants à leur parent, filtre, bouton *+* qui ouvre la fenêtre de
  création, glisser-déposer avant, dans ou après une entité, double-clic pour cadrer, F2 pour
  renommer dans la ligne, flèches du clavier ; au bout de chaque ligne l'œil qui la masque dans la
  vue, l'icône qui ouvre le préfab d'une instance et celle qui ouvre le code d'un composant du jeu)
  ; *Inspector*, généré par la réflexion (nom, UUID, une section repliable par
  composant avec son icône et un menu pour le retirer, propriétés sur deux colonnes, vecteurs
  aux lettres x, y, z colorées, angles en degrés, *Add Component* qui ouvre la même fenêtre) ;
  *FileSystem*, l'arborescence `res://` des sources (icône par type, état d'import, menu :
  ouvrir, placer, scène de démarrage, réimporter, copier le chemin, afficher dans l'explorateur,
  créer une courbe, des animations de sprite, un tileset ou un animator dans un dossier ; les
  flèches parcourent l'arbre, Entrée ouvre ; une entité de l'arbre de scène lâchée sur un dossier
  y devient un préfab, comme Godot enregistre une branche lâchée sur son FileSystem) ; *Output*
  (police à chasse fixe, recherche, compteurs par niveau qui servent de filtres, texte choisi à la
  souris sur plusieurs lignes et copié, double-clic sur un mot, menu Copy, Select All, Clear) ;
  ces deux-là sont faits avec `Devex::Ui` (voir *Une seule interface, deux usages*) ;
  *Statistics*. Les panneaux n'ont pas de bouton de fermeture : le menu *Editor > Panels* (ou
  *View* sur l'overlay) les affiche. La disposition par défaut est construite au premier
  lancement, puis sauvegardée dans `devex-tools.ini` ou `devex-editor.ini` à côté de
  l'exécutable ; *Reset Layout* la restaure.
- **Entrées** : quand ImGui utilise le clavier ou la souris, les appuis et mouvements de ce
  périphérique n'atteignent plus `Input` (les relâchements si) ; les événements restent
  transmis à `onEvent`. Ouvrir les outils libère la souris capturée.
- **Annulation** : chaque modification est une `Command` qui désigne les entités par UUID et
  les champs par leur nom enregistré ; les valeurs sont des `TextValue`. Un glissement
  continu devient une seule étape, enregistrée au relâchement. Supprimer une entité garde
  un instantané de son sous-arbre (`saveEntityTree`) pour la restaurer à la même place avec
  ses UUID. Ctrl+Z / Ctrl+Y (ou Ctrl+Maj+Z) ; une commande devenue impossible (entité
  disparue) est retirée de l'historique.
- **Rendu d'ImGui** : backends officiels `imgui_impl_sdl3` (dans `Platform`) et
  `imgui_impl_vulkan` (dans `Render`), sans multi-viewports. Le port vcpkg compile le
  backend Vulkan contre `vulkan-1.lib`, dont les symboles entrent en conflit avec les
  pointeurs de fonctions de volk : ses deux fichiers sont donc copiés dans
  `third_party/imgui/backends` depuis la version épinglée par vcpkg et compilés avec
  `IMGUI_IMPL_VULKAN_USE_VOLK`. **À recopier lors d'une mise à jour d'ImGui.** ImGui est
  dessiné dans une seconde passe sur le backbuffer.
- **Espace des couleurs** : ImGui pense ses couleurs et le lissage de ses glyphes en sRGB. Quand
  le pilote le permet (`VK_KHR_swapchain_mutable_format`, présent sur les GPU de bureau), la
  swapchain sRGB est aussi vue en UNORM : ImGui y dessine et y mélange en espace d'affichage,
  et lit l'image du viewport par une vue UNORM de la même façon (`ImageConfig::alternateFormat`).
  Le texte garde alors sa graisse et ses bords. Sinon, les couleurs sont converties en linéaire
  comme avant (`Renderer::imGuiNeedsLinearColors`).
- **Thème** (`src/tools/Theme`) : inspiré de Godot. Tout dérive d'une couleur de base, d'un accent
  et d'un contraste : fond extérieur (barres, espaces entre panneaux, barre d'onglets) plus foncé
  que les panneaux, champs et listes plus foncés encore (plus clairs sur fond noir ou clair),
  boutons légèrement éclaircis, sélection et onglet actif à l'accent. Préréglages *Gray* (défaut),
  *Blue gray* (le thème classique de Godot 4), *Black (OLED)* et *Light*. Couleurs d'icônes par
  type comme les nœuds de Godot : entités et maillages rouges, lumières jaunes, caméras violettes,
  environnement cyan, code du jeu vert, dossiers bleus, matériaux orange. Arrondis de 4 px,
  séparateurs de 5 px entre panneaux, lignes d'arbre, onglet actif surligné. L'échelle suit celle
  de l'écran (`Window::displayScale`) ou un réglage, et s'applique entre deux frames, aussi quand
  la fenêtre change d'écran.
- **Polices** : Noto Sans (normal et gras) et JetBrains Mono, versionnées dans `third_party/fonts`
  (licence SIL OFL) et copiées dans `bin/resources/fonts`. FreeType les rend
  (`imgui[freetype]`, hinting léger) aux tailles demandées grâce aux polices dynamiques d'ImGui
  1.92. Les tailles se règlent en points comme dans Godot (14 par défaut) ; ImGui dimensionnant une
  police par sa ligne entière, elles sont multipliées par la hauteur de ligne de la police
  (1,362 pour Noto Sans). Une police absente est remplacée par celle d'ImGui avec un
  avertissement.
- **Icônes** (`src/tools/Icons`) : 107 icônes Lucide 1.47.0 (licence ISC, `third_party/lucide`) et le
  logo Devex (`engine/resources/icons/devex.svg`), copiés dans `bin/resources/icons`. Chaque icône
  est un caractère de la zone à usage privé (U+E000 et suivants) : un chargeur de police ImGui
  (`ImFontLoader`) fusionné dans chaque police dessine le SVG avec plutosvg à la taille du texte.
  Les icônes s'écrivent donc dans n'importe quel texte ImGui (menus, onglets, boutons), restent
  nettes à toute échelle et prennent la couleur du texte ; le logo garde ses couleurs. La liste
  `DEVEX_EDITOR_ICONS` fixe les noms et l'ordre ; un test vérifie que chaque fichier existe.
- **Réglages de l'éditeur** : fenêtre *Editor Settings* (menu *Editor* ou bouton *Settings* du
  gestionnaire) : préréglage, couleurs de base et d'accent, contraste, échelle de l'interface
  (automatique ou de 75 à 250 %), tailles des polices, retour aux valeurs par défaut. Appliqués en
  direct et enregistrés pour l'utilisateur dans `%APPDATA%/Devex/Editor/editor.dvx` (section
  `[theme]`, avec la liste des projets). L'overlay F1 des jeux prend le thème par défaut.

### Profileur

- **Le besoin** : savoir où passe le temps d'une image (phases du moteur, systèmes du jeu, code
  C#, passes du GPU) et ce que pèsent les assets chargés, sans quitter l'éditeur ni installer
  d'outil. Choix : un profileur **maison**, montré dans un panneau *Profiler* ; Tracy et Perfetto
  ont été écartés pour l'instant (dépendance, fenêtre à part), un export au format de Perfetto
  pourra venir.
- **Enregistrement** (`core::profiler`, module Core) : des zones nommées qui s'emboîtent, ouvertes
  et fermées sur n'importe quel thread (`DEVEX_PROFILE_SCOPE("Physics")`, ou `ProfileScope`), et
  rassemblées par image entre `beginFrame` et `endFrame`. Chaque thread empile ses zones ouvertes
  sans verrou ; une zone terminée rejoint l'image sous un verrou court. Une zone appartient à
  l'image pendant laquelle elle se termine. Les 300 dernières images sont gardées (environ cinq
  secondes) ; en pause, les images sont encore mesurées mais plus gardées, ce qui fige
  l'historique. Une image gardée ne change plus : les temps GPU qui arrivent plus tard la
  remplacent par une copie, et le panneau lit sans verrou celle qu'il tient.
- **Seulement quand on regarde** : l'enregistrement est coupé tant que le panneau *Profiler* est
  fermé (dans l'éditeur, ou dans les outils F1 d'un jeu) ; une zone coûte alors un test de
  drapeau. Le panneau a un bouton de fermeture et s'ouvre par *Editor > Panels* (*View* sur
  l'overlay) ; une disposition sauvegardée avant lui le place à côté de *Output*.
- **Noms** : un littéral, ou `profiler::intern` pour un nom construit à l'exécution (systèmes,
  zones du C#), dont le texte vit aussi longtemps que le processus : une zone ne copie qu'un
  pointeur. Les tableaux regroupent les zones par texte.
- **Threads** : celui des images est *Main* ; les workers de `core::JobSystem` sont *Worker 1*,
  *Worker 2*… (`nameThread`) et chaque job est une zone *Job* ; les threads jamais nommés partagent
  la voie *Other threads*.
- **Ce que le moteur mesure** : les phases de la boucle (événements, assets, code du jeu, éditeur,
  outils, limite d'images, animation, interface, transformations, interpolation de la physique,
  audio, gameplay, pas fixe, physique, `onUpdate`, rendu) ; chaque système, sous son nom ; dans le
  rendu, l'extraction de la scène, les overlays de l'éditeur, le dessin de l'interface, l'attente
  du GPU, l'environnement, l'acquisition de l'image, les matériaux, les lumières (clusters et vues
  d'ombre), les os, les données de scène, l'enregistrement et l'envoi des commandes, la
  présentation.
- **GPU** : le render graph appelle un marqueur avant chaque passe et une fois après la dernière
  (`RenderGraph::PassMarker`) ; le renderer y écrit un timestamp (`vkCmdWriteTimestamp2`, un pool
  de 128 requêtes par image en vol) et lit les résultats après avoir attendu la fence de cette
  image, quelques images plus tard : `profiler::reportGpu` les rattache à l'image qui les a
  enregistrés. La fin d'une passe est le début de la suivante, barrières comprises ; les temps sont
  comptés depuis le début de l'image sur le GPU. Une file qui ne sait pas écrire de timestamps
  (`timestampValidBits` nul) n'a que les mesures du CPU.
- **C#** : `using (Profiler.Scope("Find a path")) { ... }` mesure une zone du jeu, qui s'affiche à
  côté de celles du moteur ; `Profiler.IsEnabled` dit si l'on enregistre. Le moteur mesure déjà
  chaque type de composant C#, chaque système C# et la copie des champs à l'entrée et à la sortie
  des phases. Un nom ne passe au moteur qu'une fois, gardé en UTF-8. `NativeApi` gagne
  `profileEnabled`, `profileBegin` et `profileEnd` (version 6 de l'amorce).
- **Le panneau** : en haut, pause et reprise, effacement et résumé de l'image choisie (CPU, dont
  le travail, et GPU). Puis une barre par image, la plus récente à droite : le travail en couleur,
  l'attente pâle au-dessus (limite d'images, attente du GPU, acquisition, présentation), en vert
  jusqu'à 60 Hz, orange jusqu'à 30 Hz, rouge au-delà, à l'échelle de l'image la plus longue
  montrée. Dessous, la chronologie de l'image : une voie par thread, les zones emboîtées sous
  celle qui les contient, les attentes en gris, puis la voie du GPU ; la molette zoome autour du
  pointeur, un glisser déplace, un double clic revient à l'image entière, une infobulle donne le
  temps et la part de l'image. À droite, les onglets *CPU* (arbre par thread : temps total, temps
  propre, appels), *GPU* (passes de la plus longue à la plus courte, celles du même nom
  additionnées, part de l'image) et *Memory*. Pendant l'enregistrement, l'image montrée est la
  plus récente dont le GPU est connu, reprise deux fois par seconde pour que les chiffres se
  lisent ; un clic sur une barre met en pause sur cette image. Côte à côte dans un panneau large,
  l'un sur l'autre dans un panneau haut.
- **Mémoire des assets** : `AssetManager::memoryReport` donne, par type, le nombre d'assets et ce
  qu'ils prennent dans la mémoire du processus et dans celle du GPU, puis les plus lourds :
  maillages (sommets, indices et poids envoyés au GPU, et la copie gardée sur le CPU pour les
  colliders et le skinning), textures (tous leurs niveaux), polices (atlas et glyphes), sons
  (échantillons décodés), animations, modèles, textes des scènes. Ce sont des estimations tirées
  des données envoyées, pas des allocations de VMA ; la mémoire GPU totale du moteur est rappelée
  à côté. Relu deux fois par seconde tant que l'onglet est affiché.
- **Coût observé** : dans l'éditeur du bac à sable en Debug, ouvrir le panneau fait passer une
  image d'environ 8,2 à 9,8 ms, surtout pour dessiner la chronologie et l'arbre ; en Release,
  toute la phase *Editor* reste autour de 0,3 ms panneau ouvert.

### Éditeur

- **Fenêtre** : disposition de Godot. Barre du haut : logo, menus *Scene*, *Edit*, *Project*,
  *Editor*, *Help* (avec icônes), nom du projet au centre, état du code du jeu et boutons Play,
  Pause, Stop et pas à pas à droite. *Scene* en haut à gauche, *FileSystem* en bas à gauche,
  *Inspector* à droite, *Output* et *Statistics* sous le viewport ; barre d'état en bas (imports
  en cours ou nombre d'entités, avertissements et erreurs de la sortie, FPS, version). Le titre
  donne la scène, `*` si elle a des modifications non enregistrées, le projet et l'état de jeu.
  La barre de titre du système passe en sombre et prend la couleur du fond (DWM, Windows 11).
- **Gestionnaire de projets** : sans projet, la fenêtre prend une taille compacte (1160 × 820 à
  100 %) et montre la liste des projets de l'utilisateur, comme celui de Godot : étoile des
  favoris (toujours en tête), logo, nom en gras, dossier, date de dernière ouverture ; filtre,
  tri (dernière édition, nom, chemin) ; *Create* (nom, dossier parent, création du dossier,
  vérifications en direct ; le projet reçoit `assets/scenes/Main.dvxscene` et s'ouvre), *Import*
  (un `.dvxproj`, ouvert aussitôt), *Scan* (ajoute les projets d'un dossier et de ses
  sous-dossiers, sans entrer dans `.git`, `.devex`, `out`...), *Edit* (ou double-clic, Entrée),
  *Run* (lance `devex-player` à côté de l'éditeur, qui continue seul), *Rename*, *Show in
  Folder*, *Remove* (de la liste seulement), *Remove Missing*. Ouvrir un projet agrandit la
  fenêtre en éditeur dans le même processus ; *Project > Project Manager* ou Ctrl+Maj+Q y
  revient. La liste (`[project path favorite last_opened]`) remplace les projets récents, dont
  l'ancien format est relu. Un `.dvxproj` passé en argument s'ouvre directement. Les dialogues
  de fichiers sont ceux du système (SDL3), sans bloquer : la réponse arrive par
  `Platform::pollEvents`. C'est le **premier panneau écrit avec `Devex::Ui`** (voir *Une seule
  interface, deux usages*) : ses boutons ont des infobulles, chaque ligne a un menu contextuel (clic
  droit : Edit, Run, Show in Folder, favori, Remove), le tri est une liste déroulante, et Create,
  Rename et Remove sont des modales avec leur voile ; Échap les ferme, Entrée ouvre le projet
  choisi.
- **Onglets de scènes** (`src/tools/SceneTabs`) : chaque scène ouverte a son onglet au-dessus du
  viewport, avec son fichier, sa scène, son historique d'annulation, sa sélection et sa caméra.
  L'onglet actif vit là où le reste de l'éditeur le lit (la scène de l'application, les champs
  des outils) ; les autres attendent dans leur onglet, et changer d'onglet échange les deux
  sans copier. *New Scene* (ou *+*) ajoute une petite scène éclairée (soleil, ciel, caméra, sol,
  cube) ; *Open Scene…*, un double-clic sur une scène de *FileSystem* ou *Open Prefab* l'ouvre dans
  un onglet, ou montre le sien si elle est déjà ouverte ; une scène glissée dans le viewport y
  place une instance (voir *Préfabs*) ; une scène neuve intacte cède sa place. Les onglets modifiés portent un point ; fermer un onglet (croix,
  Ctrl+W) demande d'enregistrer ses modifications, quitter, changer de projet ou revenir au
  gestionnaire les demande pour toutes les scènes concernées (*Save*/*Save All*, *Don't Save*,
  *Cancel* ; une scène sans fichier montre son dialogue d'enregistrement puis l'action continue).
  Ctrl+Tab passe à l'onglet suivant ; en Play, les onglets sont figés sur la scène jouée. À
  l'ouverture d'un projet, l'éditeur rouvre les scènes laissées ouvertes et l'onglet actif
  (`.devex/editor.dvx`, format 2, une section `[scene]` par onglet avec sa caméra ; l'ancien
  `last_scene` est relu), sinon la première scène du projet, sinon une nouvelle ; une scène déjà
  remplie par l'application est gardée dans un onglet sans fichier. *Save All Scenes*
  (Ctrl+Alt+S) enregistre toutes les scènes qui ont un fichier. Les modifications non
  enregistrées sont repérées par l'identifiant d'état de chaque historique
  (`CommandHistory::stateId`). Le code du jeu rechargé libère et restaure aussi les composants
  des scènes en arrière-plan (`ToolsOverlay::forEachBackgroundScene`).
- **Viewport** : la scène est rendue à la taille du panneau. Caméra de l'éditeur indépendante des
  caméras de la scène : clic droit maintenu pour regarder et voler (ZQSD/WASD, A/E pour
  descendre et monter, Maj accélère, molette règle la vitesse), Alt + clic gauche pour tourner
  autour du pivot, clic milieu pour se déplacer, molette pour avancer, F pour cadrer la
  sélection. En Play, le viewport montre la caméra du jeu, qui reçoit clavier et souris quand
  le panneau a le focus ; grille, icônes et gizmos disparaissent.
- **Sélection** : un clic choisit la lumière ou la caméra dont l'icône est sous la souris (sur
  le CPU), sinon demande au GPU l'objet visible sous le pixel ; les entités sélectionnées et leurs
  descendants sont entourés. Voir *Sélection et édition des entités*.
- **Gizmos** (maison, `src/tools/Gizmo`) : W déplacement (axes, plans, plan de la vue), E
  rotation (anneaux par axe, anneau de la vue), R échelle (axes, uniforme au centre) ; X alterne
  axes du monde et de l'entité (l'échelle est toujours locale). Taille constante à l'écran,
  poignées testées en pixels, poignée survolée ou active en jaune. Ctrl aimante par 0,5 m, 15°
  ou 0,1. Glisser modifie `Transform` en direct ; le relâchement enregistre une seule étape
  annulable, pour toutes les entités déplacées. Parent quelconque : le déplacement passe par
  l'inverse de sa matrice monde.
- **Icônes et repères** : grille au sol autour de la caméra (tous les mètres, tous les dix
  mètres de haut) qui s'estompe au loin, axes X et Z colorés ; icônes des lumières et des
  caméras, portée des lumières ponctuelles, cône des spots et pyramide de la caméra quand elles
  sont sélectionnées.
- **Barre d'outils du viewport** : sélection seule (Q, sans gizmo), déplacement (W), rotation (E),
  échelle (R), axes locaux ou du monde, aimantation permanente (Ctrl l'inverse), grille, icônes
  des lumières et caméras, cadrage ; à droite, vitesse de vol, EV de la caméra de l'éditeur et
  aide des contrôles en infobulle. En Play, elle annonce l'état du jeu et le viewport est encadré
  à la couleur d'accent.
- **Création** : le *+* de *Scene* (sous l'entité choisie, comme chez Godot), *Edit > Create
  Entity…* et *Create Child…*, ou le menu contextuel de l'arbre ouvrent la fenêtre **Create
  Entity**, l'équivalent du *Create New Node* de Godot. Devex ayant des entités et des composants
  plutôt que des types de nœuds, elle propose **chaque composant** (du moteur et du code du jeu),
  qui crée une entité le portant avec ce qu'il lui faut (une place dans le monde, ou le rectangle,
  l'image et le texte d'un élément d'interface ; un soleil incliné, une caméra qui ne prend pas la
  place de celle du jeu), et les **préréglages** qui en réunissent plusieurs (Cube, Static box,
  Rigid box, Trigger zone, 2D camera…). L'entité se pose au pivot de la caméra, ou comme enfant. La
  même fenêtre sert à *Add Component* : elle n'offre alors que ce qui manque à l'une des entités
  choisies, ajoute avec le composant ce dont il a besoin, et propose *New Script*. Faite avec
  `Devex::Ui`, elle s'éloigne de l'arbre de Godot : une **palette** avec la recherche en haut (le
  clavier y va dès l'ouverture ; les noms qui commencent par ce qui est tapé d'abord, puis les mots
  des noms, le composant, la catégorie et la description, tous les mots devant se trouver), les
  catégories en colonne d'icônes à gauche avec *Favorites* et *Recent*, les résultats en lignes
  arrondies (icône teintée, nom, description, catégorie, étoile) et une fiche à droite (grande
  icône, type, catégorie, description, ce que l'entité reçoit). Haut, Bas, Page haut et bas
  choisissent, Entrée ou un double-clic crée, Échap ferme. Favoris et récents (les dix derniers)
  sont gardés **par projet**, comme chez Godot, dans `.devex/creation.dvx`. Le catalogue
  (`src/tools/CreationCatalog`) est du code pur, testé sans fenêtre. Un modèle glissé dans le
  viewport se pose au sol sous la souris ; un matériau glissé sur un objet le remplace. Suppr
  supprime la sélection. Ctrl+A garde son rôle de *tout sélectionner* (il ajoute un nœud chez
  Godot).
- **Raccourcis** : Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Maj+S, Ctrl+Alt+S, Ctrl+W, Ctrl+Tab ; F5 ou Ctrl+P
  pour jouer, F7 pause, F8 arrêt, F9 pas à pas ; Ctrl+B compile le code du jeu ; Ctrl+Maj+Q
  revient au gestionnaire de projets, Ctrl+Q quitte.
- **Pendant le jeu** : l'historique des modifications est mis de côté ; les modifications faites
  à la copie jouée ont leur propre historique, oublié à l'arrêt.

### Sélection et édition des entités

- **Sélection multiple** (`src/tools/Selection`) : la sélection est une liste d'UUID dans l'ordre
  où ils ont été choisis ; la dernière entité est l'**active**. Le gizmo s'y place, l'inspecteur y
  lit ses valeurs, et *Create Child* s'en sert. Chaque onglet de scène garde la sienne ; ce que
  l'annulation retire en sort.
- **Dans l'arbre** : un clic sélectionne, Ctrl+clic ajoute ou retire, Maj+clic prend les lignes
  entre la dernière cliquée et celle-ci, dans l'ordre affiché (Ctrl+Maj ajoute la plage) ; un clic
  sous les lignes ne choisit rien. Les flèches Haut et Bas changent d'entité, Gauche et Droite
  replient et déplient ; l'entité choisie dans la vue vient dans le champ de l'arbre. Glisser une
  ligne d'une sélection emporte la sélection entière, en une étape, sans passer sous elle-même ni
  sortir d'une instance de préfab. Comme chez Godot, **le quart haut d'une ligne place avant elle,
  le quart bas après, le milieu dedans** : un trait d'accent ou la ligne éclairée le montrent
  pendant le glisser ; sous les lignes, l'entité va en dernier parmi les racines. Un modèle ou un
  préfab venu de FileSystem va dans la ligne où il est lâché. L'ordre des frères est celui du
  fichier de scène : `makeReparentCommand` prend le frère devant lequel placer l'entité, et son
  annulation la remet exactement où elle était (`Scene::placeLast` la remet en dernier sans
  changer de parent, ce que `setParent` laisse en place).
- **Dans la vue** : Maj+clic ajoute, Ctrl+clic ajoute ou retire. Un glisser commencé hors d'une
  poignée trace un **rectangle** qui sélectionne ce qu'on voit : les objets dont des pixels sont
  visibles dedans, lus sur le GPU comme le clic, et les icônes des lumières et caméras qu'il
  contient ; pas ce que cachent les murs.
- **Préfabs** : une instance est un seul objet dans la vue. Le premier clic sélectionne son
  instance la plus extérieure, les suivants descendent d'instance en instance jusqu'à l'entité
  sous la souris, que les clics suivants gardent (comme Unity) ; un rectangle sélectionne
  l'instance la plus extérieure. L'arbre sélectionne toujours exactement la ligne cliquée.
- **Gizmo sur plusieurs entités** : il agit sur l'entité active et entraîne les autres racines de
  la sélection (celles qu'aucun ancêtre sélectionné ne porte), chacune autour de sa propre origine
  comme le mode *Pivot* de Unity : même déplacement dans le monde, même rotation dans le monde,
  même rapport d'échelle. Quand l'active est portée par un ancêtre sélectionné, seul l'ancêtre
  bouge. Une étape d'annulation pour le tout.
- **Inspecteur de plusieurs entités** : les composants qu'elles ont toutes. Chaque champ montre la
  valeur de l'active, ou un tiret quand les autres diffèrent (case indéterminée pour une case à
  cocher, tiret par composante pour un vecteur ou des angles) ; une modification va à toutes,
  un vecteur seulement dans les composantes changées, et devient une étape d'annulation. Les
  listes s'éditent entité par entité. *Add Component* ajoute aux entités qui ne l'ont pas ; retirer
  un composant le retire de toutes, sauf si l'une le tient de son préfab. Les couleurs montrent la
  valeur de l'active sans tiret.
- **Copier, couper, coller, dupliquer** (Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+D, et les menus) : la copie
  est un texte `[entities format=1]` suivi des arbres des racines de la sélection, tels que
  `saveEntityTree` les écrit (`scene::saveEntityTrees`), mis dans le **presse-papiers du
  système** : on colle dans une autre scène, un autre projet ou un autre éditeur ouvert, et on
  peut le lire. Coller donne à chaque entité un **UUID neuf** (`scene::copyEntityTrees`) : les
  références entre entités copiées suivent, celles vers d'autres entités restent, et les instances
  de préfab restent liées, avec les entités qu'on leur a ajoutées et les références vers leurs
  entités (UUID dérivés de la nouvelle instance). Les entités collées vont à côté de l'active,
  sous le même parent ; dupliquer place chaque copie juste après son original. Une copie qui
  prendrait le nom d'un frère est renommée : « Crate 3 » devient « Crate 4 », « Crate » devient
  « Crate 2 ». Chaque opération est une étape d'annulation et sélectionne ce qu'elle crée.
- **Renommer** : F2 ou le menu ; le nom s'édite dans l'arbre, Entrée ou un clic ailleurs valide,
  Échap annule.
- **Masquer dans la vue** (l'œil de fin de ligne, H, ou le menu) : comme la visibilité de scène de
  Unity, l'entité et ses descendants disparaissent de la vue de l'éditeur (maillages, icônes,
  formes de collision) et ne se sélectionnent plus au clic ; le jeu, lui, les voit toujours, et
  le Play les montre. La liste est gardée par onglet et dans `.devex/editor.dvx` du projet
  (`hidden` de la section de la scène), écrite dès qu'elle change ; elle se perd quand l'onglet
  de la scène est fermé, comme sa caméra. *Show All in Viewport* les rend toutes.
- **Glisser un matériau** du FileSystem sur un objet de la vue : l'objet sous la souris est
  entouré pendant le glisser (une sélection GPU par frame), et le lâcher remplace le premier champ
  matériau de ses composants (celui de `MeshRenderer`), en une étape ; sur une instance de
  préfab, c'est une modification de l'instance.
- **Raccourcis** : Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+D, Ctrl+A (tout sélectionner), F2, H et Suppr
  agissent quand l'arbre ou la vue a le clavier, jamais pendant la saisie d'un texte ni, pour la
  vue, pendant le jeu ; Ctrl tenu désactive les touches d'outils de la vue (Ctrl+X coupe, il ne
  change pas d'axes).

### Assets

- **Projet** : `ApplicationConfig::project` désigne le `.dvxproj` (ou l'éditeur l'ouvre) ; `Runtime` ouvre alors une
  `AssetDatabase` sur son dossier `assets/`. Le bac à sable est le projet `samples/sandbox`,
  ouvert depuis les sources pour que les modifications d'assets s'y voient en direct.
- **Sources et importeurs** : chaque fichier dont l'extension a un importeur est une source ;
  `texture` (`.png`, `.jpg`, `.tga`, `.bmp`, `.hdr`, décodés par stb_image), `material`
  (`.dvxmat`), `gltf` (`.gltf`, `.glb`), `fbx` (`.fbx`), `obj` (`.obj`), `scene` (`.dvxscene`),
  `curve` (`.dvxcurve`), `frames` (`.dvxframes`), `tileset` (`.dvxtileset`), `animator`
  (`.dvxanimator`) et `navmesh` (`.dvxnavmesh`), entre autres. Les fichiers et dossiers cachés (`.`) sont ignorés.
- **`.dvxmeta`** : créé au premier scan avec un UUID aléatoire et les options par défaut de
  l'importeur. Il porte l'identité de l'asset : il se versionne avec la source. Un `.dvxmeta`
  illisible est signalé et laissé tel quel, jamais remplacé. Une source copiée avec son
  `.dvxmeta` (UUID déjà vu) reçoit de nouveaux identifiants.
- **Sous-assets** : les éléments d'un fichier (maillages, matériaux, textures d'un modèle) sont
  listés dans le `.dvxmeta` par type, **clé** et UUID. La clé est le nom du fichier, un nom de
  repli (`Mesh 2`) ou le nom suivi de `#index` en cas de doublon ; une texture reçoit le suffixe de
  son rôle (` (linear)`, ` (normal)`, ` (metallic-roughness)` pour celles que l'import FBX
  assemble). Une clé connue garde son UUID ; une clé disparue reste listée,
  pour que son UUID revienne avec elle.
- **Cache** : chaque import écrit ses artefacts (`imported/<uuid>.dvxasset`, écriture atomique)
  et un enregistrement texte (`sources/<uuid>.dvxsource`) : importeur et version, empreinte des
  options, taille, date et hachage XXH64 de la source et de ses dépendances, artefacts produits,
  erreur éventuelle. À l'ouverture, ces enregistrements rendent les assets disponibles
  immédiatement ; les artefacts qu'aucun enregistrement ne cite sont supprimés.
- **Détection des changements** : taille et date d'abord, contenu haché seulement si elles
  diffèrent (un fichier touché sans changement n'est pas réimporté). Changent aussi l'import :
  options, importeur, version, dépendances (buffers et images externes d'un `.gltf`, images d'un
  FBX, `.mtl` et images d'un OBJ), artefact manquant. Une source déplacée avec son `.dvxmeta` garde ses assets sans réimport.
- **Imports** : sur le `core::JobSystem` (un thread par cœur moins un), en parallèle entre fichiers
  et à l'intérieur (lignes de blocs d'une texture, textures d'un glTF). `AssetDatabase::update`,
  appelé au début de chaque frame, applique les imports terminés et renvoie des `AssetEvent`
  (`Imported`, `Removed`). Un import échoué garde les assets de l'import réussi précédent et
  n'est retenté qu'après un changement ; `reimport` force un import.
- **Surveillance** : efsw observe `assets/` ; après 250 ms sans événement, un scan complet
  (dates seulement) décide des réimports. Les éditeurs qui enregistrent en plusieurs étapes ne
  provoquent donc qu'un import.
- **Suppression** : les assets d'une source supprimée disparaissent (`Removed`) avec leurs
  artefacts ; son `.dvxmeta` reste, et la source qui revient retrouve ses UUID.
- **Chargement** : l'`AssetManager` de `Runtime` charge un asset la première fois qu'il sert
  (maillage et matériaux d'un `MeshRenderer`, textures d'un matériau, modèle placé). Avec le
  pool de jobs de l'application, les **maillages et textures se chargent en arrière-plan** :
  les demander lance la lecture et le décodage sur un worker et ne renvoie rien ; une fois par
  frame, avant l'extraction de la scène, `finishLoads` confie ce qui est fini au renderer
  (64 Mo au plus par frame : la copie dans la mémoire de staging se fait sur le thread
  principal), qui les copie sur le GPU. En attendant, les maillages ne sont pas dessinés et les
  matériaux prennent les textures par défaut : rien ne gèle, les objets apparaissent en quelques
  frames. Les matériaux, modèles, polices (dont l'atlas suit le même chemin vers le GPU),
  triangles des colliders, sons, animations, thèmes et textes des scènes restent lus quand on
  les demande : légers, ou nécessaires tout de suite. Sans pool de jobs (tests, outils), tout se
  charge quand on le demande, comme avant.
- **Sûreté des workers** : un worker ne touche qu'une fonction de lecture préparée sur le thread
  principal (`AssetSource::artifactReader` : le chemin de l'artefact pour la base d'assets, dont
  la liste change pendant les imports ; l'appel direct pour un paquet, immuable) et une file
  partagée qu'il garde en vie. Chaque demande a un numéro : un réimport en lance une nouvelle et
  rend l'ancienne périmée. Changer de source ou détruire le gestionnaire incrémente une
  génération, attend les lectures en cours et oublie les autres, qui s'arrêtent sans rien lire.
- **Rechargement** : sur `Imported`, un maillage ou une texture chargé est relu en arrière-plan et
  **reste affiché jusqu'à ce que la nouvelle version soit prête**, qui prend alors un nouveau
  handle (l'ancien est libéré après les frames en vol) ; un matériau garde le sien, et les
  matériaux sont résolus à nouveau quand des textures arrivent. Les maillages enregistrés par
  l'application (primitives) ne sont jamais détruits par lui.
- **Préchargement** : `AssetManager::preload` lance le chargement d'un asset sans l'utiliser,
  `isReady` dit s'il est chargé et, pour ce qui se dessine, copié sur le GPU (un matériau l'est
  quand ses textures le sont ; un asset qui a échoué compte comme prêt, pour ne pas attendre
  indéfiniment), `pendingLoads` compte ce qui est en route. L'éditeur l'affiche dans sa barre
  d'état (*Loading N assets*) et le panneau *Statistics* montre les copies vers le GPU ; le
  profileur montre les zones *Load a mesh* et *Load a texture* sur les workers, *Finish loads*
  et *Uploads* sur le thread principal, et la passe *Uploads* du GPU.
- **Scènes chargées en arrière-plan** : `SystemContext::sceneToLoadInBackground` (ou
  `Application::loadSceneInBackground`, `Game.LoadSceneInBackground` en C#) lit et construit la
  scène tout de suite, hors de l'écran, puis précharge les assets que nomment les champs de ses
  composants (`scene::referencedAssets`, par la réflexion, listes comprises). La scène courante
  continue ; `SystemContext::loadingScene` et `loadingProgress` (`Game.IsLoadingScene`,
  `Game.LoadingProgress`) donnent la part prête, de 0 à 1, pour un écran de chargement ; quand
  tout est prêt, elle remplace la scène courante comme `sceneToLoad`, qui reste le chemin
  immédiat et annule un chargement en cours. En C#, `Assets.Preload` et `Assets.IsReady`. La
  version de l'API des jeux passe à 10, celle de l'amorce C# à 7. Tab, dans le bac à sable,
  change de scène ainsi.
- **Textures** : mipmaps complets calculés en espace linéaire pour les couleurs sRGB, normales
  renormalisées à chaque niveau. Les images Radiance `.hdr` deviennent des textures `RGBA16F`
  non compressées, pour les ciels. Compression **BC7** (sRGB pour les couleurs, perceptuelle) par
  l'encodeur bc7e de basisu, **BC5** pour les normal maps ; options `srgb`, `normal_map`,
  `mipmaps`, `compress` et `quality` (`fast`, `normal`, `high`). Un glTF déduit le rôle de chaque
  image de son usage : couleur de base et émission en sRGB, métal-rugosité et occlusion en
  linéaire, normales en BC5.
- **glTF** : l'asset principal est un **modèle** (nœuds de la scène par défaut avec transformations
  locales). Les primitives d'un maillage deviennent ses sous-maillages, chacun avec son matériau,
  et gardent les tangentes du fichier ou reçoivent des tangentes MikkTSpace ;
  les matériaux reprennent tous les paramètres PBR (y compris `KHR_materials_emissive_strength`).
  Les images référencées par un `.gltf` sont importées comme sous-assets du modèle, même si elles
  sont aussi des textures du projet.
- **FBX et OBJ** : lus par **ufbx** (v0.23.1, un seul fichier C vendu dans `third_party/ufbx`,
  compilé en bibliothèque statique avec ses propres avertissements), ils donnent les mêmes assets
  qu'un glTF : un modèle, un maillage par géométrie (un sous-maillage par matériau, faces
  triangulées, sommets identiques fusionnés, tangentes MikkTSpace), matériaux, textures,
  squelettes, skinning (les quatre os les plus lourds) et un clip par pile d'animation. Les
  deux formats partagent l'importeur ; l'OBJ n'a pas d'animation.
  - **Repère** : ufbx convertit les axes et l'unité du fichier au repère du moteur (Y-up, main
    droite, mètres), dans la géométrie et les translations, pas dans une échelle des nœuds ; les
    transformations propres aux maillages (*geometric transforms*) sont appliquées aux sommets et
    les modes d'héritage d'échelle compensés. Un OBJ, qui ne dit rien, est pris en mètres et Y-up.
  - **Échelle** : l'option `scale` du `.dvxmeta` (1 par défaut) multiplie le tout. L'échelle
    uniforme et fixe des nœuds racines est **cuite** dans les maillages et les translations de
    leurs descendants (ce qui commute avec leurs rotations), avec la pose de référence des skins
    corrigée : les objets qu'exporte Blender, à l'échelle 100 pour compenser le centimètre de
    ses fichiers, arrivent à l'échelle 1 comme ceux d'un glTF. Une racine dont l'échelle est
    animée, non uniforme, ou dont un maillage est partagé avec une racine d'une autre échelle, la
    garde.
  - **Matériaux** : ufbx ramène chaque modèle d'ombrage (Lambert, Phong, Standard Surface,
    Physical de 3ds Max, matériaux de Blender relus en PBR…) à des paramètres PBR. Une texture
    remplace la couleur à laquelle elle est reliée ; les couleurs sont prises linéaires, comme
    Blender les écrit. Rugosité et métal, dans des cartes séparées en FBX, sont **assemblés**
    dans une texture métal-rugosité (vert et bleu), une carte de brillance inversée ; une même
    image pour les deux est prise comme déjà assemblée. L'opacité d'un matériau Phong vient de
    sa transparence ; une opacité reliée à l'image de couleur donne un matériau découpé (`Mask`),
    une opacité fixe inférieure à 1 un matériau transparent.
  - **Images** : intégrées au FBX, ou cherchées au chemin relatif écrit dans le fichier puis sous
    leur seul nom à côté du modèle ; les chemins absolus (souvent ceux de la machine de
    l'artiste) ne sont pas suivis. Une image introuvable est signalée et le matériau garde ses
    valeurs.
  - **Animations** : chaque pile est échantillonnée par ufbx en clés linéaires (30 par seconde
    pour les courbes non linéaires, clés réduites quand elles s'alignent) à partir de zéro ; les
    pistes constantes égales à la pose de repos sont omises.
  - Non importés : caméras, lumières, morph targets, matériaux propres à une instance d'un
    maillage partagé, couleurs de sommets, seconds jeux d'UV.
- **Ajout de fichiers** : `AssetDatabase::addFile` copie un fichier extérieur dans un dossier du
  projet, avec les fichiers qu'il lit (`Importer::findDependencies` : buffers et images d'un
  `.gltf`, images d'un FBX, `.mtl` et images d'un OBJ), écrit son `.dvxmeta` et renvoie l'UUID du
  modèle à venir. L'éditeur l'utilise pour les fichiers déposés sur la fenêtre.
- **Inspecteur des modèles** : cliquer un modèle dans FileSystem montre ce que son fichier a donné
  (maillages, matériaux, textures, animations), **Place in Scene**, **Reimport** et ses réglages
  d'import : échelle (FBX et OBJ), compression et qualité des textures ; changer un réglage
  réimporte le fichier. Les modèles déjà placés gardent la position de leurs nœuds (ce sont des
  copies) : seuls leurs maillages suivent.
- **Outils** : panneau *Assets* (sources, type, statut, erreur en infobulle, réimport, sous-assets
  dépliables) ; un asset se glisse sur un champ `AssetId` de l'inspecteur, un modèle dans la
  hiérarchie (placement annulable). L'inspecteur liste les assets du type attendu.

### Préfabs

- **Modèle** : comme Godot, un préfab est une **scène** ordinaire (`.dvxscene`), placée dans d'autres
  scènes où elle reste liée à son fichier. Les préfabs s'imbriquent à tous les niveaux : un préfab
  contient des instances d'autres préfabs, avec leurs propres modifications. Pas de type d'asset à
  part : toute scène peut servir de préfab (le bac à sable les range dans `assets/prefabs`).
- **Fichier** : une instance est une section d'entité qui nomme son préfab, suivie de ses
  modifications :

  ```text
  [entity uuid="2c1f5e0a-7d4b-4c9e-8f3a-6b5d2e1c0f47" name="Lamp zone"]
  parent = "b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44"
  prefab = asset("9a3e4f21-5c6d-4b7a-8e9f-0a1b2c3d4e5f")

  [override type="Transform"]
  position = vec3(-8, 3.2, -6)

  [override target="7e2d9c4b-1a3f-4e5d-9b8c-2f6a0d1e3c57" type="PointLight"]
  color = vec3(0.55, 0.75, 1)

  [override target="7e2d9c4b-1a3f-4e5d-9b8c-2f6a0d1e3c57" name="Bulb"]
  ```

  `target` est l'UUID d'une entité dans le préfab (la racine quand il manque) ; `type` change des
  champs d'un composant, ou **ajoute** le composant avec ces champs si l'entité du préfab ne l'a
  pas ; `name` renomme. Les entités ajoutées sous celles de l'instance sont des sections
  ordinaires dont le parent est un UUID dérivé. Seules les **différences** sont écrites : un
  champ changé dans le préfab atteint toutes les instances qui ne l'ont pas modifié. Retirer un
  composant ou une entité du préfab dans une instance n'est pas possible (il faut *Make Local*).
- **UUID** : les entités d'une instance ont des UUID **dérivés** de celui de l'instance et du leur
  dans le préfab (`derivePrefabUuid`, un hachage 128 bits marqué version 8, jamais produit par
  `Uuid::generate`). Ils sont stables d'un chargement à l'autre : sélection, annulation et
  entités ajoutées y font référence. Un préfab à une seule racine donne sa racine à l'entité de
  l'instance ; avec plusieurs racines, l'entité de l'instance les regroupe (avec un `Transform`).
- **Chargement** : les sections deviennent une liste plate d'entités (`FlatScene`) où chaque
  instance est remplacée par les entités de son préfab, chargées récursivement, auxquelles les
  modifications s'appliquent au niveau du texte ; les entités sont ensuite créées d'un coup.
  Les entités d'une instance reçoivent `PrefabEntity` (la racine de l'instance extérieure qui les
  enregistre, et leur UUID dans le préfab), les racines d'instances (imbriquées comprises)
  `PrefabInstance`. Ces composants ne sont jamais écrits comme les autres.
- **Enregistrement** : l'instance est comparée à sa **base**, le préfab chargé seul avec les mêmes
  UUID (`prefabBase`, mise en cache), champ par champ sur les valeurs écrites par la réflexion,
  pour que les flottants se comparent à l'identique. Les entités ajoutées sont écrites après les
  modifications. Copier une entité intérieure à une instance l'écrit en entités ordinaires.
- **Sources des préfabs** : `setPrefabSourceLoader` donne au module le moyen de lire une scène par
  `AssetId`, pour tout le processus ; le runtime passe par `AssetManager::sceneText` (fichier
  source, sinon artefact importé), qui garde le texte jusqu'à l'événement d'import suivant. Les
  préfabs lus sont gardés, avec leurs dépendances, et relus quand leur texte ou celui d'un préfab
  qu'ils contiennent change.
- **Robustesse** : un préfab absent ou illisible laisse l'instance **non résolue** : une entité
  vide qui garde ses sections `override` telles quelles et les réécrit (avertissement). Un préfab
  qui se contient lui-même, directement ou non, est détecté (pile de chargement) et son instance
  intérieure reste non résolue. Une modification dont la cible a quitté le préfab est abandonnée
  (information) ; une entité ajoutée dont le parent a disparu passe sous l'instance. L'import d'une
  scène vérifie sa syntaxe sans charger ses préfabs (`PrefabLoading::KeepUnresolved`), les imports
  tournant hors du thread principal.
- **Mise à jour en direct** : quand un import change des scènes, l'éditeur enregistre, dans la scène
  éditée et celles des onglets en arrière-plan, les instances qui les utilisent (directement ou
  par un préfab intermédiaire) **avec les anciens textes** encore dans l'`AssetManager`
  (`snapshotPrefabInstances`), oublie ces textes, puis recharge les instances à leur place
  (`rebuildPrefabInstances`) : les modifications sont conservées, la scène n'est pas marquée
  modifiée. Une instance que le nouveau préfab ne permet plus de charger reste telle quelle, avec
  une erreur. Le jeu en cours (Play, `devex-player`) garde ses instances.
- **Code du jeu** : `scene::instantiatePrefab(scene, prefab, parent)` crée une instance avec un
  nouvel UUID ; un champ `AssetId` avec `.assetType = "scene"` choisit le préfab dans
  l'inspecteur.
- **Éditeur** :
  - une scène glissée depuis *FileSystem* dans le viewport (au sol sous la souris) ou sur l'arbre
    (comme enfant) crée une instance, en une étape annulable ; une scène ne peut pas se contenir
    elle-même. Le double-clic ouvre toujours la scène dans un onglet ;
  - l'arbre montre les racines d'instances avec l'icône de paquet et les entités venues de préfabs
    en bleu (en rouge si le préfab manque) ; ces entités ne se suppriment ni ne se déplacent
    (les commandes le refusent aussi), mais acceptent des enfants et des composants ;
  - le menu contextuel donne *Open Prefab*, *Make Local* et *Save as Prefab…* ; l'inspecteur,
    une ligne « Instance of » avec *Open*, *Revert All* et *Make Local* ;
  - les valeurs qui diffèrent du préfab ont une barre à la couleur d'accent et un nom en gras ; un
    clic droit propose *Revert to Prefab Value* (ou *Revert to Prefab Name*). Un composant ajouté
    est marqué ; les composants venus du préfab ne se retirent pas ;
  - *Revert All* retire toutes les modifications sauf la place de la racine (`Transform`, nom) et
    garde les entités ajoutées ; *Make Local* transforme l'instance, préfabs imbriqués compris, en
    entités ordinaires ;
  - *Save as Prefab…* écrit l'entité et ses descendants dans un **nouveau** fichier (dans
    `assets/prefabs` par défaut), racine ramenée à l'origine, puis remplace l'entité par une
    instance au même endroit, qui garde son UUID ; les deux étapes de la scène s'annulent en une
    (`makeReplaceEntityTreeCommand`), le fichier reste. Écraser un préfab existant est refusé.

### Physique

- **Moteur** : Jolt Physics 5.6 (MIT, le moteur physique de Godot 4), en DLL par vcpkg (`joltphysics`),
  derrière le module `Physics` : son API publique (`devex/physics/PhysicsWorld.hpp`) ne montre aucun
  type de Jolt, que les jeux n'ont pas besoin d'inclure. Jolt est initialisé tant qu'un monde existe
  (allocateur, fabrique, types), et `VerifyJoltVersionID` refuse une DLL compilée avec d'autres
  réglages.
- **Composants** (module `Scene`, sauvegardés et édités comme les autres) : `RigidBody` (type
  `static`, `kinematic` ou `dynamic`, masse en kg, frottement, rebond, amortissements, échelle de
  gravité, couche, rotations verrouillées, collision continue, vitesses) ; `BoxCollider`,
  `SphereCollider`, `CapsuleCollider`, `CylinderCollider` (taille et centre dans l'espace de leur
  entité, dont ils suivent l'échelle, drapeau `trigger`) ; `MeshCollider` (un maillage, celui du
  `MeshRenderer` par défaut ; triangles pour le décor, enveloppe convexe pour ce qui bouge) ;
  `CharacterController` (rayon, hauteur, pente et marche maximales, masse, force de poussée,
  couche ; `velocity`, `grounded` et `groundNormal` sont l'état du jeu, non sauvegardé).
- **Corps** : une entité avec `RigidBody` forme un corps avec ses colliders et ceux de ses
  descendants qui n'ont pas de `RigidBody` à eux (forme composée statique de Jolt) ; des colliders
  sans `RigidBody` au-dessus d'eux forment un corps statique. Les colliders `trigger` forment un
  capteur à part qui suit le corps (cinématique s'il bouge) et remarque aussi les corps
  cinématiques et les personnages. Un `RigidBody` dynamique avec un maillage non convexe prend son
  enveloppe convexe (avertissement). Le corps est placé à la position et à la rotation du monde de
  son entité ; les échelles sont intégrées aux formes (enveloppes et triangles mis à l'échelle,
  primitives redimensionnées, la plus grande échelle pour les sphères).
- **Synchronisation** : à chaque pas, le monde décrit les corps attendus par la scène et les
  compare à ceux qu'il a, par une signature des réglages et des formes : un corps apparaît avec ses
  composants, est reconstruit quand ils changent et disparaît avec eux ou avec son entité. Un corps
  dynamique ou statique dont l'entité a été déplacée par le jeu est téléporté ; un corps cinématique
  suit son entité (`MoveKinematic`) ; une vitesse modifiée dans `RigidBody` est appliquée. Après le
  pas, les corps dynamiques écrivent leur `Transform` (relative au parent), leur transformée du
  monde et leurs vitesses. Le coût est linéaire en nombre de corps ; des milliers de corps
  demanderont des marqueurs de modification plutôt que des signatures. La signature compte la
  place des formes au dixième de millimètre (et une rotation `q` comme `-q`), et range les formes
  d'un corps par entité : sans cela, l'arrondi des matrices du monde d'un corps qui tourne
  changeait à chaque pas et le reconstruisait (jusqu'au jalon 15).
- **Personnages** : un `CharacterVirtual` de Jolt par `CharacterController`, une capsule posée sur
  la position de l'entité, avec un corps intérieur pour être touché par les requêtes, les
  déclencheurs et les corps. À chaque pas, le jeu a mis sa vitesse ; debout, la vitesse verticale
  est remplacée par celle du sol (plateformes mobiles), sinon la gravité s'ajoute ; `ExtendedUpdate`
  marche sur les pentes, monte les marches (`step_height`) et colle au sol. La vitesse écrite en
  retour est relative au sol. Les personnages se bloquent entre eux.
- **Couches** : 16 couches nommées dans les réglages du projet, avec une matrice symétrique de qui
  touche qui (`[physics]` et `[physics_layer]` du `.dvxproj`, écrits seulement s'ils diffèrent des
  valeurs par défaut). Pour Jolt, une couche d'objet combine la couche du projet et le fait de bouger :
  deux corps statiques ne sont jamais testés, et la phase large n'a que deux couches (statique,
  mobile). *Project > Project Settings* nomme les couches, règle la gravité et la matrice ; les
  changements valent au prochain lancement du jeu.
- **Boucle** : le runtime crée un monde quand le jeu commence (démarrage hors de l'éditeur, Play dans
  l'éditeur, avec les réglages du projet) et le détruit à la fin (`ApplicationConfig::enablePhysics`).
  Chaque pas fixe : `onFixedUpdate`, systèmes `FixedUpdate`, transformées, pas de physique. Avant le
  rendu, les corps dynamiques, les personnages et leurs descendants sont placés entre leurs deux
  derniers pas selon l'avancement vers le prochain (transformées du monde seulement), pour un
  mouvement fluide à plus de 60 images par seconde. Les maillages des colliders viennent de
  `AssetManager::meshData` (données du CPU, maillages intégrés compris).
- **Jeu** : `SystemContext::physics` (nul sans physique) donne `raycast`, `sphereCast` et
  `overlapSphere` (masque de couches, entité ignorée), `addForce`, `addTorque`, `addImpulse` et
  `addImpulseAt`, et les **contacts** (`Begin`, `End`, entités, déclencheur ou non) des pas de la
  frame, que les systèmes `Update` voient une fois. Un contact est compté par paire de corps, pas
  par paire de sous-formes. La version de l'API des jeux passe à 2.
- **Éditeur** : les formes des colliders et des personnages sont dessinées en fil de fer, vertes
  (bleues pour les déclencheurs) : celles de la sélection et de ses descendants, ou toutes avec le
  bouton de la barre d'outils du viewport ; les maillages ne sont pas dessinés. La simulation ne
  tourne qu'en Play. Le menu de création ajoute boîte statique, boîte et sphère rigides, personnage
  et zone de déclenchement ; l'inspecteur choisit la couche par son nom.
- **Réglages de Jolt** : pool de threads de Jolt (jusqu'à 8), 65 536 corps, 65 536 paires, 16 384
  contraintes de contact, 32 Mo de mémoire temporaire. Les corps au repos s'enfoncent de 2 cm au
  plus (tolérance de pénétration de Jolt). Les vitesses données à Jolt restent juste sous ses
  limites (500 m/s, 15π rad/s), que les vitesses relues, une fois arrondies, pouvaient dépasser.
  En Debug, une vérification de Jolt qui échoue est écrite en erreur fatale avant l'arrêt.

### Audio

- **Backend** : module `Devex::Audio`, basé sur **miniaudio**, avec `stb_vorbis` pour Ogg Vorbis.
  Un `AudioEngine` par application possède la sortie et les groupes de mixage ; un `AudioWorld`
  gère les sons de la scène jouée. Les types de miniaudio restent privés au module. Sans sortie
  sonore, le moteur continue avec un avertissement et un mélangeur sans périphérique, également
  utilisé par les tests pour lire les échantillons sans matériel audio.
- **Clips** : WAV, FLAC, MP3 et Ogg Vorbis sont importés comme assets `AudioClip` (`audio` dans la
  réflexion). L'artefact `.dvxasset` conserve le fichier encodé, sa fréquence, ses canaux, sa durée
  et les crêtes de sa forme d'onde. L'option d'import `loading` choisit `decoded` (PCM décodé une
  fois au chargement et partagé entre les lectures), `streamed` (décodage pendant chaque lecture)
  ou `auto` (décodé jusqu'à 10 secondes, sinon streamé). **Le fichier encodé reste en mémoire dans
  les deux cas** : il ne s'agit pas encore de streaming depuis le disque. Les références de clips
  suivent la même chaîne d'assets que les autres données, jusqu'au paquet exporté.
- **Sources** : `AudioSource` porte le clip, le volume, le pitch, la boucle, le démarrage
  automatique, le groupe et les réglages spatiaux. Une source 2D conserve le même volume partout ;
  une source spatialisée suit la position mondiale de son entité, avec atténuation inverse,
  linéaire, exponentielle ou désactivée, distances minimale et maximale, rolloff et Doppler.
  `playOnStart` démarre la source quand elle apparaît dans la scène jouée ; retirer la source ou
  l'entité arrête le son.
- **Écouteur** : le premier `AudioListener` disposant d'une transformation écoute ; à défaut,
  c'est la caméra marquée `primary`. Position et orientation suivent les transformations
  mondiales (Y-up, -Z avant) ; les vitesses des sources et de l'écouteur sont calculées chaque
  frame pour le Doppler. Un seul écouteur est actif.
- **Jeu** : `SystemContext::audio` et `Application::audio()` donnent l'`AudioWorld` (nul si
  l'audio est désactivé). `play(scene, entity)` recommence la source ; `stop`, `pause`, `resume`
  et `isPlaying` la pilotent ; `playOneShot(clip, position, volume, group, spatial)` joue un son
  ponctuel sans créer d'entité. En C#, `Audio.Play`, `Stop`, `Pause`, `Resume` et `IsPlaying`
  pilotent les mêmes sources ; `Audio.PlayOneShot(clip, position)` spatialise le son, tandis que
  la surcharge sans position le joue en 2D. `AudioSource` et `AudioListener` ont leurs vues C#
  générées comme les autres composants du moteur.
- **Groupes** : le `.dvxproj` enregistre le volume Master et huit groupes, dont Effects, Music et
  Voice par défaut, dans `[audio]` et `[audio_group]`. Les sources et sons ponctuels choisissent
  leur groupe par indice ; l'inspecteur l'affiche par nom (`audioGroup` en réflexion C++,
  `[AudioGroup]` sur un champ `uint` C#). Le C++ obtient l'indice avec
  `context.audio->engine().findGroup("Music")` puis appelle `setGroupVolume` ; le C# utilise
  `Audio.GetGroupVolume("Music")` et `Audio.SetGroupVolume("Music", 0.5f)`, ou `Audio.Master` pour
  le volume général. Ces changements du code ne réécrivent pas le projet.
- **Éditeur** : sélectionner un clip dans FileSystem affiche sa forme d'onde, ses informations,
  les boutons Play/Stop et le choix du mode de chargement (réimporté à chaque changement) ; un
  double-clic lance l'aperçu sonore, disponible hors Play. *Project > Project Settings* règle les
  volumes et noms des groupes. Le viewport dessine les icônes des sources et écouteurs, ainsi que
  les distances minimale et maximale de la source spatialisée sélectionnée. Les sons du jeu
  suivent la pause de Play et sont libérés à l'arrêt ou au changement de scène.
- **Bac à sable** : le lanceur C++ joue `throw.wav` à chaque balle, les cibles C# `hit.wav` à chaque
  impact et la porte C# sa source `door.wav` quand elle s'ouvre ou se ferme. Le cube flottant
  bourdonne avec une source spatialisée en boucle ; une ambiance Ogg en 2D tourne dans Music.
  Les sons sont synthétisés par `scripts/generate_sample_assets.py --audio-only` (encodage Ogg,
  MP3 et FLAC par `ffmpeg`), avec des clips de test dans les quatre formats.

### Animation squelettique

- **Import** : un fichier glTF ou FBX rigué produit, à côté de ses maillages et matériaux, un squelette et
  un asset `AnimationClip` par animation (`animation` dans la réflexion). Le maillage emporte les
  joints et poids de ses sommets (quatre par sommet, normalisés à l'import) et sa **pose de
  référence** (`inverseBind`) ; le modèle emporte la liste des joints de chaque skin et les clips du
  fichier. Les tangentes MikkTSpace, qui peuvent dédoubler des sommets, recopient le skinning.
- **Squelette** : instancier un modèle crée **une entité par os**, avec sa `Transform`, sous la
  racine du modèle ; le maillage skinné reçoit un `SkinnedMeshRenderer` dont la liste `bones` pointe
  ces entités dans l'ordre des joints. Les os sont donc visibles dans l'arbre, déplaçables, et on
  peut attacher un objet à une main en le mettant enfant de l'os.
- **Clips** : un clip décrit ses joints **par nom** et, pour chacun, des pistes de translation, de
  rotation ou d'échelle (interpolation `linear`, `step` ou `cubic spline`, comme glTF). Un clip joue
  donc sur n'importe quel squelette dont les os portent les mêmes noms ; un os absent est ignoré.
  Les morph targets ne sont pas importés.
- **Lecture** : le composant `Animator` porte le clip courant, la vitesse, la boucle, le démarrage
  automatique, la durée de fondu et le root motion. L'`AnimationWorld` avance les animateurs juste
  avant le calcul des transformations du monde, échantillonne le clip, **fond** avec le clip
  précédent pendant `blend_time` et écrit les transformations locales des os. Changer `clip` depuis
  l'inspecteur ou depuis le code lance le nouveau clip avec un fondu.
- **Root motion** : par défaut les clips animent sur place et le code déplace l'entité. Avec
  *Apply root motion*, le déplacement de l'os racine est retiré de la pose et donné à l'entité : à
  sa `Transform`, ou à la vitesse horizontale de son `CharacterController` quand elle en a un. Le
  retour au début d'une boucle n'est pas compté comme un déplacement.
- **Jeu** : `SystemContext::animation` et `Application::animation()` donnent l'`AnimationWorld` :
  `play(scene, entity, clip, fondu)`, `stop`, `pause`, `resume`, `isPlaying`, `time` et `setTime`.
  En C#, la classe `Animation` expose les mêmes appels, et `[AssetType("animation")]` limite un
  champ `AssetId` aux clips.
- **Skinning** : le rendu déforme les sommets **dans le vertex shader**. Chaque instance skinnée
  publie ses matrices d'os (transformation mondiale de l'os fois sa pose de référence) dans un
  buffer de la frame, dont l'adresse et l'offset voyagent dans les push constants avec les
  attributs de skinning du maillage. Les passes couleur, ombres, sélection et picking partagent la
  même fonction `vertexTransform`, si bien qu'un personnage projette son ombre et se sélectionne
  dans sa pose. Le calcul est donc refait par passe : pas de pré-skinning en compute pour l'instant,
  et les colliders ne suivent pas la pose.
- **Éditeur** : le panneau **Animation** (ancrable, *Editor > Panels > Animation*) montre
  l'`Animator` de l'entité sélectionnée ou de son ancêtre : choix du clip, Play/Pause/Stop, boucle,
  vitesse, et une piste par os avec ses images clés. Hors mode Play, le panneau pose lui-même le
  squelette de la scène éditée — les os restent donc dans la dernière pose prévisualisée ; pendant
  Play, il suit le jeu et permet de mettre en pause et de se déplacer dans le clip. Il n'y a pas
  encore de marqueurs d'événements, ni d'édition des courbes.
- **Cache d'import** : le cache mémorise désormais la version des formats cuits
  (`artifactLayouts`). Changer la disposition d'un `.dvxasset` réimporte les assets concernés à
  l'ouverture du projet au lieu de les faire échouer au chargement.
- **Bac à sable** : `scripts/generate_sample_assets.py` fabrique `robot.glb`, un robot rigué à onze
  os avec les clips *Idle*, *Walk* et *Wave*. Dans l'arène, le composant C# `RobotGuide` le fait
  patrouiller, attendre à chaque extrémité et saluer le joueur qui s'approche, en changeant de clip
  avec un fondu de 0,25 s.

### Machines à états d'animation

- **Asset** : un fichier `.dvxanimator` (importeur `animator`, type `Animator`), comme
  l'*Animator Controller* d'Unity ou l'*AnimationTree* de Godot, partagé par tous les personnages
  qui le jouent. Il liste des **paramètres** (`float`, `int`, `bool`, `trigger`, avec leur valeur de
  départ), des **états**, des **transitions** et l'**état d'entrée**, ainsi que la place des nœuds
  dans le graphe de l'éditeur. Les états et les paramètres se nomment de façon unique ; les
  transitions désignent les états par leur nom (lisible et stable dans les diffs). Les conditions
  s'écrivent en appels : `greater("Speed", 0.1)`, `less`, `equals`, `not_equals` (entiers),
  `is("Grounded", true)`, `trigger("Jump")`. Une transition sans `from` part de n'importe quel état
  (« Any State »). `asset::validate` refuse un animator incohérent (entrée inconnue, noms en double,
  transition vers un état inconnu, test inadapté au type du paramètre…).
- **États** : un clip seul, un **arbre de mélange 1D** (des clips placés sur une ligne qu'un
  paramètre parcourt : marche vers course) ou **2D** (des clips placés sur un plan de deux
  paramètres, pondérés par interpolation en bandes de gradient, le *freeform cartesian* d'Unity :
  chaque clip a tout le poids à sa place, les poids varient en douceur entre elles). Les clips d'un
  arbre avancent à la même fraction de leur cycle, et l'état dure la moyenne de leurs durées selon
  leurs poids. Un état porte aussi une vitesse, un paramètre qui la multiplie, la boucle, et une
  **animation de sprite** : en entrant dans l'état, le `SpriteAnimator` de l'entité joue cette
  animation nommée de ses `.dvxframes` (l'état dure alors ses images divisées par leur cadence).
- **Transitions** : vérifiées dans l'ordre du fichier, la première dont toutes les conditions sont
  vraies et dont le **temps de sortie** est atteint (fraction du cycle de l'état depuis son début :
  1 à la fin) est prise ; sans condition, une transition part à la fin de l'état. Les déclencheurs
  qu'elle vérifie sont remis à zéro. L'état quitté continue d'avancer pendant le **fondu** (durée en
  secondes) et les deux poses se mêlent ; aucune transition n'est prise pendant un fondu.
- **Lecture** : le composant `Animator` gagne un champ `controller` ; avec lui, l'`AnimationWorld`
  joue la machine à états (le clip du composant est ignoré, `play` la reprend depuis l'entrée,
  `stop` y revient), sinon il joue son clip comme avant. Le root motion suit la pose finale. Une
  nouvelle version de l'asset (réimport, édition pendant le jeu) est reprise telle quelle : la
  machine garde son état s'il existe encore, les nouveaux paramètres prennent leur valeur de
  départ.
- **Jeu** : `AnimationWorld::setFloat`, `setInteger`, `setBool`, `setTrigger`, `resetTrigger`,
  `parameter`, `state`, `stateTime` et `status` (l'état, le fondu en cours, les paramètres), en C++
  ; en C#, `Animation.SetFloat`, `SetInteger`, `SetBool`, `SetTrigger`, `ResetTrigger`, `GetFloat`,
  `GetInteger`, `GetBool`, `GetState`, `IsInState` et `GetStateTime`. Un paramètre peut être posé
  avant que l'animator ne charge ; un nom inconnu du contrôleur est signalé une fois. L'API des
  jeux passe à 17 (le composant `Animator` change de disposition) et l'amorce C# à 14.
- **Éditeur** : le panneau **Animator** (ancrable, à côté d'*Output* la première fois ; *View >
  Animator*, double-clic sur un `.dvxanimator`, ou le bouton de son inspecteur) montre le graphe du
  contrôleur de l'`Animator` de l'entité sélectionnée, ou de l'animator choisi dans le FileSystem :
  nœuds *Entry* (flèche orange vers l'état d'entrée), *Any State* et états (nom et ce qu'ils
  jouent), transitions en flèches, côte à côte dans les deux sens. On déplace les nœuds, la vue
  (bouton du milieu ou droit, molette pour le zoom, bouton pour tout cadrer) ; clic droit : nouvel
  état, arbre 1D ou 2D, *Make Transition* (puis clic sur la cible), état d'entrée, suppression ;
  un clip glissé du FileSystem crée un état, ou devient le clip de l'état sous la souris. À gauche,
  les paramètres (ajout par type, renommage propagé aux conditions et aux arbres, valeur de
  départ). L'état ou la transition cliqué s'édite dans l'**inspecteur** (nom, entrée, mouvement,
  clips et seuils ou positions de l'arbre, animation de sprite, vitesse, boucle, transitions
  sortantes ; durée, temps de sortie, conditions, ordre), jusqu'à ce qu'une autre entité ou un autre
  asset soit choisi. Chaque changement terminé est enregistré dans le fichier (réimporté aussitôt)
  et forme une étape d'**annulation propre au panneau** (Ctrl+Z, Ctrl+Y, boutons), puisque les
  assets n'entrent pas dans l'historique de la scène ; un contrôleur incohérent n'est pas enregistré
  et la raison s'affiche. Pendant le jeu, l'état actif s'allume avec l'avancement de son cycle, le
  fondu en cours s'éclaire, et les paramètres montrent les valeurs du jeu, modifiables à la main
  (un bouton lève les déclencheurs). *New Animator* dans un dossier du FileSystem en crée un.
- **Bac à sable** : le robot de l'arène joue `assets/animators/robot.dvxanimator` : un arbre 1D
  mêle *Idle* et *Walk* selon `Speed`, que `RobotGuide` fait monter et descendre (accélération),
  et `Greeting` fond vers *Wave* quand le joueur approche. Le chevalier du jeu de plateformes joue
  `hero.dvxanimator` : ses états *Idle*, *Run*, *Jump* et *Fall* nomment ses animations de sprite,
  et `Hero2D` ne donne plus que `Speed`, `Grounded` et `VerticalSpeed`.

### Tweens et coroutines

- **Tweens** (`animation::TweenWorld`, `SystemContext::tweens`, `Tween` en C#) : un tween amène un
  **champ d'un composant** d'une valeur à une autre en un temps donné. Le champ tient un nombre,
  un vecteur, une couleur ou une rotation et se nomme « Composant.champ » : `Transform.position`,
  `UiRect.opacity`, `UiImage.color`, `PointLight.intensity` ou le champ d'un composant du jeu, C++
  ou C#. Tout ce que la réflexion décrit s'anime donc sans code propre à chaque propriété. Le
  tween part de la valeur du champ à la fin de son délai, ou d'une valeur donnée, et peut être
  relatif (ajouté au départ). Les rotations se donnent en degrés autour de x, y et z et passent
  par un slerp. Il boucle ou non (recommencer, aller-retour), un nombre de fois ou sans fin ; il se
  met en pause, reprend, s'arrête sur place ou saute à sa fin.
- **Résolus par nom à chaque frame** : un tween garde l'UUID de son entité et le nom de son champ,
  pas de pointeur, parce que le module du jeu et le C# se rechargent et que les composants
  bougent en mémoire. Il s'arrête avec son entité ou son composant. Les tweens écrivent une fois
  par frame, après les clips des `Animator` (un tween déplace une entité animée en entier) et
  avant la mise en page des interfaces et le calcul des transforms ; ils attendent quand
  l'éditeur met le jeu en pause. Leurs identifiants ne se répètent pas dans un processus : une
  poignée gardée d'une scène précédente ne désigne plus rien.
- **Séquences** (`playSequence`, `TweenSequence` en C# avec `Append`, `Join`, `AppendInterval`) :
  des étapes jouées l'une après l'autre, chacune faite de tweens joués ensemble puis d'une
  attente. Le temps d'une frame va aux tweens d'une étape ou à une attente, jamais aux deux.
- **Composant `Tweener`** : le même tween sans code, réglé dans l'inspecteur (champ, départ,
  arrivée, relatif, durée, délai, courbe, boucle, répétitions). Il démarre seul avec le jeu
  (*Play On Start*) ou quand le code le demande (`playTweener`, `Tween.PlayTweener`), et s'arrête
  quand le composant est retiré ; remis, il repart.
- **Courbes** : les 22 courbes classiques de Penner (`math::Ease`, `Ease` en C# : quadratique,
  cubique, sinus, exponentielle, back, élastique, rebond ; entrée, sortie, les deux) et des
  **courbes dessinées**. Une courbe dessinée est un asset `.dvxcurve` fait de clés (temps, valeur,
  pentes d'entrée et de sortie, spline d'Hermite) ; un tween ou un `Tweener` qui la nomme la suit
  à la place de sa courbe. Elle peut dépasser 1 (rebond, dépassement) ou redescendre.
- **Éditeur de courbes** : l'inspecteur d'une courbe la dessine sur une grille, avec ses clés et
  les poignées des pentes de la clé choisie à déplacer (Maj tourne un seul côté, Ctrl arrondit
  les valeurs au dixième), un double clic pour ajouter une clé, le menu d'une clé (lisse, plate,
  linéaire, supprimer), des préréglages (linéaire, entrée, sortie, les deux, dépassement, rebond)
  et les valeurs de la clé choisie. Chaque modification terminée réécrit le fichier, qui se
  réimporte ; un fichier changé ailleurs est relu. *New Curve*, dans le menu d'un dossier du
  FileSystem, en crée une et la montre dès son import.
- **Coroutines C++** (`runtime::Coroutine`, `CoroutineScheduler`, `SystemContext::coroutines`) : des
  coroutines C++20 qui attendent `co.wait(secondes)`, `co.nextFrame()`, `co.until(condition)` ou
  `co.tween(poignée)`. Chaque `co_await` rend le `SystemContext` de la frame où la coroutine
  reprend, puisque celui d'une frame ne vit que pendant elle. Elles reprennent pendant Update,
  après les systèmes, comme dans Unity. Elles s'arrêtent avec l'entité qui les possède, au
  remplacement de la scène, à l'arrêt du jeu et avant le rechargement du module : leurs cadres
  tiennent du code et des objets du module, et sont détruits avant lui (les destructeurs de leurs
  variables passent). Le planificateur garde la fonction qui a lancé une coroutine, pour qu'une
  lambda qui est elle-même une coroutine garde ses captures. Une exception termine la coroutine
  et va au journal.
- **Coroutines C#** : une méthode `async Coroutine` attend `Wait.Seconds`, `Wait.NextFrame`,
  `Wait.Until`, `Wait.While`, un tween ou une autre coroutine. `Coroutine` est un type « à la
  Task » avec son propre constructeur de méthode, qui confie ses attentes au planificateur du
  runtime plutôt qu'à des rappels. Lancée par un composant (dans ses méthodes, ses rappels de
  collision ou l'une de ses coroutines), une coroutine s'arrête avec lui ; sinon avec la scène. Le
  runtime installe pendant les phases un contexte de synchronisation : l'`await` d'une `Task`
  (`Task.Delay`, `Task.Yield`, une lecture de fichier) reprend pendant Update, sur le thread du
  jeu, et l'exception d'un `async void` va au journal. Une coroutine qui échoue passe son
  exception à celle qui l'attend ; si personne ne l'attend, elle est signalée à la frame suivante.
  Une coroutine arrêtée ne passe pas par ses blocs `finally`, et toutes s'arrêtent au
  rechargement du code, qui ne pourrait pas se décharger tant qu'elles le tiennent. Dans une
  méthode `async`, un tween joué sans être attendu demande `_ =` (avertissement CS4014).
- **Versions** : l'API des jeux passe à 13 (`SystemContext` gagne `tweens` et `coroutines`),
  l'amorce C# à 10.
- **Bac à sable** : la caisse du plateau tournant flotte par un `Tweener` qui suit la courbe
  dessinée `curves/Hover.dvxcurve` ; N passe du jour à la nuit en trois secondes par une
  coroutine C++ qui change la lumière par rapports égaux (100 000 lux à 0,3 passent par 170 au
  milieu) ; le bouton Sauvegarder dit « Sauvegardé » puis revient en fondu, par une coroutine C#
  qui attend ses tweens ; le menu principal apparaît en fondu.

### Particules

- **Simulation sur le CPU** (`particles::ParticleWorld`, `SystemContext::particles`, `Particles` en
  C#) : chaque émetteur est simulé par un job du pool du moteur (`JobSystem::parallelFor`), les
  émetteurs en parallèle, comme Shuriken dans Unity. Des dizaines de milliers de particules
  tiennent dans le budget ; le code du jeu lit et pilote les émetteurs sans aller-retour avec le
  GPU ; les collisions passent par la physique ; et l'aperçu de l'éditeur montre ce que le jeu
  montrera. Une simulation sur le GPU (des millions de particules) viendra à côté, pas à la place.
- **Réglages dans le composant** : `ParticleEmitter` porte tout l'effet, rangé dans l'inspecteur en
  sections repliables : *Emitter* (durée d'un cycle, boucle, préchauffage, maximum, espace du
  monde ou local, graine), *Emission* (par seconde, par mètre parcouru, salve à chaque cycle),
  *Shape* (point, sphère, demi-sphère, cône, boîte, cercle ; volume ou surface ; direction
  aléatoire), *Particles* (plages de durée de vie, vitesse, taille, rotation et rotation par
  seconde ; couleurs de naissance et de mort ; intensité), *Motion* (gravité, accélération,
  freinage, part du mouvement de l'émetteur, bruit tourbillonnant), *Over Life* (courbes de
  taille, d'opacité et de vitesse, les assets `.dvxcurve` du jalon 33), *Collision*, *Sub
  Emitter*, *Rendering* et *Trails*. Un effet se réutilise par un préfab, comme dans Unity. Ces
  sections viennent d'un indice de réflexion, `FieldHints::group`, que n'importe quel composant
  peut employer.
- **Identité et durée de vie** : l'état d'un émetteur (ses particules, ses traînées, son cycle)
  vit dans le `ParticleWorld`, pas dans la scène : il n'est ni sauvegardé ni copié, et une scène
  commence sans particules. Les émetteurs sont retrouvés chaque frame par leur entité et démarrent
  seuls (*Play On Start*) ; le code les joue, les arrête (en laissant finir ou en effaçant), les
  met en pause et émet d'un coup (`play`, `stop`, `pause`, `emit` ; `Particles.Play`, `Stop`,
  `Emit`… en C#). Une nouvelle scène, le début et la fin du jeu vident le monde.
- **Mouvement** : intégration semi-implicite, gravité du projet, accélération constante, freinage,
  bruit de gradient en trois dimensions qui évolue dans le temps. Les particules émises au rythme
  de l'émetteur se répartissent le long de son chemin pendant la frame, déjà en route, pour qu'un
  émetteur rapide laisse une traînée continue ; une salve ou une émission du code part de là où
  l'émetteur se tient. En espace local, les particules suivent l'émetteur ; la gravité est alors
  ramenée dans son repère.
- **Collisions** : pendant le jeu, un segment de la position précédente à la suivante interroge la
  physique (`PhysicsWorld::raycastSolid`, qui traverse les déclencheurs), depuis les workers : la
  physique ne fait pas de pas pendant ce temps. La particule rebondit (rebond et frottement), perd
  une part de sa vie, et peut déclencher un **sous-émetteur** : un autre émetteur, qui n'émet que
  là où une particule touche ou meurt (étincelles d'un impact, éclaboussures de pluie). Ces
  émissions sont appliquées après les jobs, sur le thread principal.
- **Traînées** : chaque particule garde les derniers points de son chemin (`trails`), et un
  composant `TrailRenderer` laisse un ruban derrière son entité (projectiles, balles, épées) :
  un point tous les quelques centimètres, qui vit un temps donné, avec largeur et couleur de la
  tête à la queue.
- **Rendu** : le `RenderWorld` reçoit des particules (position, taille, couleur, rotation, image
  de la planche, étirement) et des rubans (points et segments), par lots triés **avec les
  surfaces transparentes**, du plus loin au plus proche : une particule derrière une vitre est
  vue à travers. Les surfaces transparentes quittent donc la passe *Scene* pour une passe
  *Transparent* où la profondeur est attachée en lecture seule et lue en même temps
  (`DEPTH_READ_ONLY_OPTIMAL`, liaison 12 du set de la frame), ce qui donne des **particules
  douces** qui s'estompent au contact des surfaces. Les particules d'un lot en alpha sont triées
  sur le CPU ; les lots additifs ne le sont pas. Un seul pipeline mélange des couleurs
  prémultipliées : une particule additive sort un alpha nul, qui s'ajoute.
- **Apparence** : face à la caméra, étirée le long de son mouvement (étincelles, pluie), couchée
  ou dressée ; texture (sans texture, un disque doux), planches d'images jouées sur la vie ou
  tirées au hasard ; couleurs relatives à l'exposition comme l'émission des matériaux, au-dessus
  de 1 elles alimentent le bloom ; ou **éclairées** comme une surface mate tournée vers la caméra
  (soleil et ses ombres, ciel, lumières locales), pour la fumée et la poussière.
- **Éditeur** : les émetteurs de la scène éditée jouent en aperçu dans la vue, sans collisions ;
  l'inspecteur d'un émetteur ajoute *Restart*, *Stop* et le nombre de particules vivantes.
  Mettre le jeu en pause fige ses particules.
- **Versions** : l'API des jeux passe à 14 (`SystemContext::particles`), l'amorce C# à 11.
- **Bac à sable** : sur la balise de gauche de la scène `sandbox`, un feu additif qui rétrécit et
  une fumée éclairée qui grandit et s'estompe (courbes `Shrink`, `Grow`, `FadeInOut`) ; sur celle
  de droite, une fontaine d'étincelles étirées avec leurs traînées. Dans l'arène, les balles
  lancées laissent une traînée, et une cible touchée rejoue, là où la balle l'a frappée, la salve
  d'étincelles d'un émetteur qui rebondissent sur le sol (`code/Target.cs`).

### Sprites et jeux 2D

- **Dans le rendu 3D** : un sprite est un rectangle de texture posé dans le plan XY de son entité,
  face à +Z, dessiné dans la passe *Transparent* avec les surfaces transparentes et les
  particules, dans les unités du monde (mètres). 2D et 3D se mélangent comme dans l'URP 2D
  d'Unity : un sprite passe derrière un mur, une particule devant un sprite, et le post-traitement
  s'applique à tout. Les sprites ne s'écrivent pas dans la profondeur : ils testent celle des
  surfaces opaques, et se couvrent entre eux dans leur ordre de dessin.
- **Découpés à l'import de la texture** (comme Unity) : les réglages d'import d'une texture
  (`sprite_mode`) en font un sprite (`single`) ou la découpent en grille (`grid`, `columns` et
  `rows`, cellules de gauche à droite puis de haut en bas, cellules vides écartées). Chaque sprite
  est un **sous-asset** de la texture (`AssetType::Sprite`, clé = numéro de la cellule, qui garde
  son UUID d'un import à l'autre) : son rectangle en pixels, la taille de la texture, les **pixels
  par unité**, le **pivot** (où se tient l'entité, de (0, 0) en bas à gauche à (1, 1) en haut à
  droite) et les **bords** gardés par les modes découpé et répété. Sous les pixels transparents
  d'une texture de sprites, l'import étend la couleur des pixels voisins, pour qu'un filtrage lisse
  ne fasse pas de liseré sombre. Un sprite glissé du FileSystem dans la vue crée une entité qui le
  montre, sous la souris.
- **Filtrage** : `filter = "nearest"` est un réglage d'import de toute texture (format de texture
  cuit en version 3) : le pixel art reste net. Chaque texture du tableau bindless a son sampler à
  côté d'elle (`textureSamplers[]`, liaison 8 du set global), lisse ou au plus proche : tous les
  shaders qui lisent une texture (matériaux, interface, particules, sprites) respectent le réglage
  sans rien savoir de plus.
- **Composant `SpriteRenderer`** (le nom d'Unity, comme `MeshRenderer`) : le sprite, une teinte et
  une intensité (au-dessus de 1, le bloom), retournements horizontal et vertical autour du pivot,
  mode de dessin (**simple** à la taille de ses pixels, **découpé en neuf** dont le milieu s'étire,
  **répété** dont le milieu se répète, à la taille du composant), **couche de tri** et **ordre**
  dans la couche, mélange alpha ou additif, et **éclairé** en option : par défaut, les couleurs
  sont montrées telles quelles, quelle que soit l'exposition, comme les particules ; éclairé, le
  sprite est une surface mate tournée vers la caméra (soleil, ciel, lumières), sans éclairage 2D
  dédié pour l'instant. Le découpage en neuf et la répétition se font par pixel dans le shader
  (`shaders/sprite.slang`), sur un seul quad, avec les pentes d'une projection continue pour que
  les coutures ne changent pas de niveau de mip.
- **Tri** (comme Unity) : des **couches de tri nommées** dans les réglages du projet
  (`[sorting_layer]`, onglet *Sorting* : ajouter, renommer, monter, descendre, retirer ; *Default*
  toujours présente), puis l'**ordre** dans la couche, puis la distance (le carré de la distance
  à une caméra en perspective, la profondeur le long d'une caméra orthographique), la plus loin
  d'abord. Les surfaces transparentes et les particules sont dans *Default*, à l'ordre 0 : un
  sprite de *Foreground* passe devant elles, où qu'il soit. Un sprite nomme sa couche : renommer
  une couche laisse ses sprites dans *Default* jusqu'à ce qu'ils la nomment de nouveau. Les
  sprites vus sont triés sur le CPU et envoyés dans cet ordre ; les sprites qui se suivent entre
  deux autres dessins partent en un seul appel instancié.
- **Animation image par image** (comme le `SpriteFrames` de Godot) : un asset `.dvxframes` porte
  les **animations nommées** d'un personnage (*idle*, *run*, *jump*…), chacune avec ses sprites,
  sa cadence et sa boucle. Le composant `SpriteAnimator` en joue une par son nom (vide : la
  première), à une vitesse (négative : à l'envers) ; nommer une autre animation la reprend à sa
  première image, une animation sans boucle s'arrête sur sa dernière et éteint `playing`. Son
  image (`frame`) choisit ce que montre le `SpriteRenderer`, dans l'éditeur aussi : elle pose le
  sprite. Les animateurs avancent pendant le jeu seulement (`animation::updateSpriteAnimators`,
  après les clips), et se mettent en pause avec lui. En C#, les champs sont ceux du composant, et
  `animator.Play("run")` joue une animation sans la reprendre si elle joue déjà : on l'appelle à
  chaque frame depuis une machine à états.
- **Caméra orthographique** : `Camera::projection` (*perspective* ou *orthographic*),
  `orthographic_size` (la moitié de la hauteur montrée, en mètres) et `far_plane` ; la profondeur
  reste inversée, de 1 au plan proche à 0 au plan lointain. Les cascades d'ombre et les clusters
  de lumières se calculent sur un **volume de vue** (demi-taille à une distance = demi-taille
  d'origine + pente × distance), qui décrit les deux projections. Le ciel d'une vue
  orthographique se voit comme d'une perspective de 60° : une toile de fond qui tourne avec la
  caméra sans se déplacer avec elle. *Create > 2D camera* place une caméra orthographique aux
  couleurs gardées telles quelles (tonemapping *None*, sans anticrénelage ni occlusion).
- **Type de scène** : comme la racine d'une scène Godot est un `Node2D` ou un `Node3D`, une scène
  est **2D** ou **3D** (`scene::SceneKind`), écrit dans son en-tête : `[scene format=1 kind="2d"]`.
  Une scène écrite avant le type est classée à la lecture (`scene::inferSceneKind`) : 2D si sa
  caméra principale (ou sa première) est orthographique, ou, sans caméra, si elle dessine sprites,
  tuiles ou interfaces et aucun maillage ; 3D sinon. *Scene > New 2D Scene* (une caméra 2D) et
  *New 3D Scene* (soleil, ciel, caméra, sol, cube) ; Ctrl+N et le bouton + d'onglet créent une
  scène du type de l'écran montré. *Scene > Scene Kind* change le type, avec son annulation.
- **Écrans 2D et 3D** (comme Godot) : une scène se voit dans l'écran de son type, qui s'ouvre avec
  elle. L'écran de l'autre type reste cliquable mais ne montre **rien** de la scène : un ciel
  neutre gris à exposition fixe, la grille et une ligne qui dit où la scène s'édite ; ni
  sélection, ni gizmo, ni dépôt. Seule exception : l'écran 2D d'une scène 3D, où s'éditent ses
  **interfaces**. Chaque écran montre tout ce que sa scène contient : une scène 2D montre ses
  maillages dans l'écran 2D, vus de face, et une scène 3D ses sprites dans l'écran 3D. La 2.5D
  (sprites parmi des modèles, caméra orthographique ou non) est donc une scène 3D, comme dans
  Godot (`Sprite3D`, caméra 3D). Ainsi rien de la scène ne se voit mal, ni ne disparaît.
- **Écran 2D de l'éditeur** : le viewport vu de face, sans perspective : la caméra de l'éditeur
  regarde le plan XY en orthographique ; clic droit ou milieu pour glisser, molette pour zoomer
  vers la souris, F pour cadrer ; grille du plan XY qui s'adapte au zoom, axes X et Y colorés ; le
  gizmo garde les poignées du plan (X, Y, le plan XY, le centre, la rotation autour de Z). La
  caméra d'une scène orthographique montre sa boîte. Les sprites se choisissent au clic et au
  rectangle (sur le GPU, à leurs pixels visibles, dans leur ordre de dessin) et s'entourent quand
  ils sont sélectionnés. Le jalon 35 avait mis un bouton 2D dans la barre du viewport, à la
  manière d'Unity, l'écran 2D ne sachant alors dessiner que des rectangles d'interface : ce
  raccourci allait contre la disposition de Godot que suit l'éditeur, et il a été retiré.
- **Inspecteurs** : une texture montre son aperçu, avec la grille de découpe et les pivots, ses
  réglages d'import (couleur, normal map, mipmaps, compression, qualité, filtrage) et ses sprites
  (mode, pixels par unité, colonnes et lignes, pivot prédéfini ou libre, bords), et *New Sprite
  Frames* crée à côté d'elle des animations de tous ses sprites. Des sprite frames montrent leurs
  animations (ajouter, retirer, renommer, cadence, boucle), un aperçu qui joue, et leurs images :
  un sprite ou une texture glissés du FileSystem s'ajoutent, un clic droit déplace, duplique ou
  retire. Les aperçus passent par `Renderer::imguiTexture`, un set ImGui par texture créé à la
  demande et libéré avec elle.
- **Versions** : l'API des jeux passe à 15 (la caméra a changé de disposition) ; l'amorce C# ne
  change pas, les vues des composants étant générées.
- **Bac à sable** : la scène `platformer` (Tab depuis l'arène) est un petit jeu de plateformes en
  pixel art généré (`assets/textures/2d`, `assets/sprites`) : un chevalier court et saute (`Move`,
  `Jump`), ramasse des pièces qui tournent et flottent (étincelles, son, compteur), devant un
  coucher de soleil répété qui défile plus lentement que le sol, un mur de briques éclairé par une
  torche vacillante (particules, lumière et `Tweener`), et des herbes dans la couche
  *Foreground*. Le niveau est une carte de tuiles depuis le jalon 36 (voir *Tuiles*), et le jeu
  bouge par la physique 2D depuis le jalon 37 (voir *Physique 2D*). Le jeu est en C#
  (`code/Platformer.cs`).

### Tuiles

- **Composant `Tilemap`** (comme Unity et Godot, les cellules vivent dans le composant) : un
  tileset, la taille d'une cellule en mètres, une teinte, une couche de tri et un ordre, éclairé
  ou non. La cellule (x, y) couvre [x, x + 1) × [y, y + 1) cellules depuis l'origine de l'entité,
  y vers le haut, dans son plan XY ; chaque tuile remplit sa cellule, sans pivot. Une cellule
  garde le numéro de sa tuile sur 14 bits (0 pour aucune) et deux bits qui la **retournent**
  (horizontalement et verticalement).
- **Par blocs** : les cellules sont rangées par blocs de 16 × 16 ; la scène les enregistre dans
  le champ `blocks`, une chaîne par bloc (`"x,y:"` puis ses 256 cellules en base64, deux octets
  chacune, ligne par ligne depuis le bas), triées par ligne puis par colonne, sans les blocs
  vides. Une carte se copie avec son entité, se modifie dans un préfab et s'annule comme tout
  champ ; les diffs changent d'une ligne par bloc touché. `scene::TileGrid` lit les blocs,
  change beaucoup de cellules et les réécrit ; `tileAt` et `setTile` changent une cellule sans
  décoder toute la carte ; `cellAt` et `cellCenter` passent du monde aux cellules. Le champ est
  caché de l'inspecteur par un nouvel indice de réflexion, `FieldHints::hidden`.
- **Tilesets** (comme le `TileSet` de Godot) : un asset `.dvxtileset` liste ses tuiles, chacune
  avec un numéro stable (les cellules le gardent quand d'autres tuiles sont ajoutées ou
  retirées), un sprite, une **collision** (*none*, *full* : solide de tous côtés, *top* : tient ce
  qui arrive d'en haut et laisse passer par-dessous), des **images d'animation** avec leur cadence
  (eau, lave, torches), et des **données** libres pour le jeu (`"water"`, `"damage=5"`). Les
  collisions servent au code du jeu et, depuis le jalon 37, à la physique 2D
  (`TilemapCollider2D`).
- **Rendu** : une carte est un lot parmi les sprites. L'extraction résout chaque tuile une fois
  par carte (le sprite de son image à l'instant, la texture, les retournements en échangeant les
  coordonnées de texture) ; le renderer trie les cartes avec les sprites par couche, ordre et
  distance (depuis le milieu de leurs tuiles), écarte les tuiles hors de la vue et envoie celles
  qui restent à la suite dans le buffer des sprites : les sprites et les cartes qui se suivent
  partent en un seul appel instancié. Une carte se choisit au clic sur ses tuiles, et s'entoure
  quand elle est sélectionnée. Les tuiles animées suivent une horloge qui avance dans l'éditeur
  et s'arrête quand le jeu est en pause.
- **Peinture** : sous la `Tilemap` de l'entité inspectée, les outils **Paint**, **Erase**,
  **Rectangle**, **Fill** (les cellules pareilles qui se touchent ; les vides seulement dans le
  rectangle des cellules peintes) et **Pick**, les retournements, et la palette des tuiles du
  tileset. Dans la vue (2D ou 3D, le rayon rencontre le plan de la carte), le clic gauche peint en
  glissant, sans trou entre deux positions ; Maj efface, Ctrl prend la tuile ; clic droit ou
  milieu déplacent la vue ; Échap arrête. La vue montre les cellules autour de la souris, le
  cadre de ce qui va changer et la tuile à peindre, pâle ; la sélection et le gizmo attendent.
  Chaque trait est **une étape d'annulation** du champ `blocks`.
- **Inspecteur d'un tileset** : sa palette, où l'on glisse des sprites ou une texture entière ;
  pour la tuile choisie, son sprite, sa collision, ses données, ses images d'animation (glissées
  aussi) et leur cadence ; *Remove Tile*. *New Tileset* dans un dossier du FileSystem, ou depuis
  une texture découpée (une tuile par sprite).
- **Code** : C++ par `scene::TileGrid`, `tileAt`, `setTile`, `cellAt`, `cellCenter` et
  `AssetManager::tileset` ; C# par `Tilemaps.GetTile`, `SetTile` (retournée ou non), `CellAt`,
  `CellCenter`, `GetCollision` et `GetData`. L'amorce C# passe à 12.
- **Bac à sable** : le niveau du jeu de plateformes est une `Tilemap` (`assets/tiles/platformer`,
  planche `assets/textures/2d/tiles.png`) : herbe et terre (bords retournés), corniches de pierre
  qu'on traverse par-dessous, caisses, piliers de pierre, fleurs, panneau, et un bassin d'eau
  animée. Le chevalier revient au départ quand il tombe à l'eau, en lisant les données du
  tileset (`code/Platformer.cs`) ; il bute contre les tuiles et se pose sur les corniches par la
  physique 2D depuis le jalon 37.

### Physique 2D

- **Moteur** : Box2D 3.1 (MIT), par vcpkg (`box2d`), derrière le module `Physics2D` : son API
  publique (`devex/physics2d/Physics2DWorld.hpp`) ne montre aucun type de Box2D. Un monde 2D existe
  à côté du monde 3D pendant le jeu ; les deux partagent la gravité (X et Y) et les 16 couches du
  projet, mais ne se touchent pas. Une entité simulée en 2D bouge en X et Y et tourne autour de Z ;
  sa profondeur, son échelle et ses rotations autour des autres axes restent.
- **Composants** (module `Scene`) : `RigidBody2D` (type `static`, `kinematic` ou `dynamic`, masse
  répartie sur l'aire des colliders, frottement, rebond, amortissements, échelle de gravité,
  couche, rotation bloquée, collision continue, vitesses) ; `BoxCollider2D`, `CircleCollider2D`,
  `CapsuleCollider2D` (debout sur Y) et `PolygonCollider2D` (3 à 8 points, enveloppe convexe),
  dimensionnés dans l'espace de leur entité dont ils suivent l'échelle, `trigger`, et `one_way`
  pour la boîte et le polygone ; `TilemapCollider2D` ; `CharacterController2D`. Mêmes règles qu'en
  3D : un `RigidBody2D` forme un corps avec les colliders 2D de son entité et des descendants sans
  `RigidBody2D` à eux ; des colliders sans `RigidBody2D` au-dessus d'eux forment un corps statique.
- **Synchronisation** : comme en 3D, une signature des réglages et des formes (placées dans le
  repère du corps) crée, reconstruit et retire les corps ; un corps déplacé par le jeu est
  téléporté, un cinématique suit son entité (`b2Body_SetTargetTransform`, sa vitesse porte ce qui
  le touche), une vitesse modifiée est appliquée ; après le pas (4 sous-pas), les corps dynamiques
  écrivent leur `Transform`, leurs vitesses et les transformées de leurs descendants. Le monde 2D
  fait son pas juste après le 3D, et s'interpole de même.
- **Filtrage** : bits de catégorie de Box2D : les 16 couches pour les formes pleines et les
  personnages, un bit pour les corniches, un pour les déclencheurs, que les formes pleines listent
  dans leur masque. Les couches et le drapeau de chaque forme vivent aussi dans ses données
  utilisateur, où les requêtes du jeu les lisent.
- **Collisions à sens unique** (*one way*) : une corniche tient ce qui arrive d'en haut et laisse
  passer par-dessous et par les côtés. Les corps dynamiques passent par le rappel *pre-solve* de
  Box2D : le contact vaut si la normale sort de la corniche vers le haut et si le corps n'est pas
  déjà enfoncé de plus de 5 cm. Les personnages ne touchent jamais les corniches par leur corps :
  leur déplacement les traite (plus bas).
- **Collider de tuiles** (`TilemapCollider2D`, comme le *TilemapCollider2D* d'Unity avec son
  *CompositeCollider*) : les tuiles *full* de chaque ligne se fusionnent en bandes, puis les bandes
  de même largeur empilées en rectangles ; les tuiles *top* d'une ligne deviennent une corniche d'un
  dixième de cellule en haut des cellules. Les rectangles sont gardés tant que les blocs de la carte
  et le tileset restent les mêmes : peindre pendant le jeu reconstruit le corps. `tileRectangles`
  est public et testé seul ; l'éditeur dessine les mêmes rectangles.
- **Personnage 2D** (`CharacterController2D`, « move and slide » comme Godot, sur le *mover* de
  Box2D 3.1) : une capsule posée sur la position de l'entité (rayon, hauteur), pente et marche
  maximales, échelle de gravité, force de poussée, couche ; `velocity`, `grounded` et
  `ground_normal` sont l'état du jeu (plus bas). À chaque pas, avant le pas de Box2D : sans sol,
  la gravité s'ajoute ; debout, elle ne s'accumule pas et la vitesse du sol porte le personnage.
  Jusqu'à 5 itérations recueillent les plans en contact (`b2World_CollideMover`), les résolvent
  (`b2SolvePlanes`) et avancent jusqu'au premier obstacle (`b2World_CastMover`, et un lancer de
  capsule pour les corniches) ; la vitesse est coupée par ce qui arrête. En marchant, ce qui est
  trop raide (pentes, coins des marches) devient un mur vertical, que le personnage ne gravit pas
  en glissant ; une **marche** (montée de `step_height`, avancée, descente) le fait passer sur
  plus haut que lui, s'il retombe sur un sol plus haut que son départ ; en descente de pente ou de
  marche, il reste **collé au sol** ; posé à moins de 5 cm d'un sol, il s'y **pose**. Le **sol** est
  une surface assez plate sous la capsule, ou le coin d'un dessus plat (un court rayon à côté du
  coin le trouve) : un personnage tient au bord d'une plateforme sur son arrondi. Une corniche
  n'arrête le personnage que si ses pieds étaient au-dessus au début du pas et s'il ne monte pas
  vers elle ; une corniche déjà traversée le laisse passer. Il **pousse** les corps dynamiques
  rencontrés de côté jusqu'à sa vitesse, avec au plus sa force de poussée. Un corps cinématique
  suit la capsule, pour les déclencheurs, les requêtes et les corps qui tombent dessus ; comme en
  Box2D les corps cinématiques ne touchent ni les statiques ni les cinématiques, un personnage n'a
  de contacts qu'avec les corps dynamiques et les déclencheurs.
- **Points des plans** : Box2D 3.1.1 donne le point de `b2PlaneResult` dans le repère du corps
  touché ; le monde le passe en coordonnées du monde (la règle des corniches, et le coin du sol, le
  demandent).
- **Contacts** : `Begin`/`End` par paire d'entités, déclencheur ou non, depuis les événements de
  contact et de capteur de Box2D (activés sur toutes les formes). Le monde compte les paires de
  formes qui se touchent : un contact commence avec la première et finit avec la dernière ; les
  paires d'une forme détruite finissent avec elle, quel que soit le moment où Box2D le rapporte.
  Même disposition que les contacts 3D.
- **Jeu** : `SystemContext::physics2d` (nul sans physique) donne `raycast` (à travers les
  déclencheurs), `overlapCircle` (déclencheurs compris), `addForce`, `addTorque`, `addImpulse` et
  les contacts ; l'API des jeux passe à 16. En C#, `Physics2D.Raycast` (`RayHit2D`),
  `OverlapCircle`, `AddForce`, `AddTorque` et `AddImpulse` ; les contacts 2D suivent les 3D dans
  `Physics.Contacts` et arrivent aux mêmes `OnCollisionEnter`/`OnTriggerEnter`. Les vues C# des
  composants 2D sont générées ; `CharacterController2D.Velocity` et `Grounded` se lisent et
  s'écrivent comme les autres champs. L'amorce C# passe à 13.
- **État de jeu** : un nouvel indice de réflexion, `FieldHints::runtime`, marque les champs que le
  moteur tient pendant le jeu (vitesse et sol du personnage 2D) : vus par le code C++ et C#, ni
  sauvegardés, ni copiés, ni montrés.
- **Éditeur** : les formes 2D sont dessinées dans le plan XY avec les autres collisions (celles de
  la sélection, ou toutes) : boîtes, cercles, capsules, polygones, rectangles des tuiles, capsules
  des personnages, et une flèche vers le haut sur ce qui est à sens unique. Le menu de création
  ajoute boîte statique 2D, boîte et cercle rigides 2D, personnage 2D et zone de déclenchement 2D.
- **Bac à sable** : le jeu de plateformes passe à la physique 2D : le niveau a un
  `TilemapCollider2D`, le chevalier un `CharacterController2D` (gravité × 3, saut de 14 m/s pour
  franchir les corniches à 3 m, que le saut d'avant n'atteignait pas), les pièces sont des
  déclencheurs ramassés dans `OnTriggerEnter`, trois caisses dynamiques se poussent, et une
  plateforme cinématique (`Shuttle2D`) fait traverser l'eau. Le code de collision écrit à la main
  a disparu de `code/Platformer.cs`.

### Navigation

- **Bibliothèque** : Recast & Detour 1.6 (zlib), par vcpkg (`recastnavigation`), derrière le module
  `Navigation` : son API publique (`devex/navigation/NavMeshBuilder.hpp`, `NavigationWorld.hpp`)
  ne montre aucun type de Recast ou de Detour. Recast cuit le maillage, Detour y cherche les
  chemins, DetourTileCache le reconstruit autour des obstacles et DetourCrowd fait marcher les
  agents en foule, comme le *NavMesh* d'Unity ou le *NavigationServer* de Godot.
- **Composants** (module `Scene`) : `NavMeshSurface` porte l'asset cuit (`nav_mesh`) et les
  réglages de la cuisson : rayon, hauteur, marche et pente maximales de l'agent (0,4 m, 1,8 m,
  0,4 m, 45°), taille des cellules (0,2 m) et hauteur des cellules (0,1 m), côté des tuiles en
  cellules (48). `NavMeshAgent` : vitesse, accélération, vitesse de rotation, rayon, hauteur,
  distance d'arrêt, qualité d'évitement (`none` à `high`) et `velocity`, l'état du jeu
  (`FieldHints::runtime`). `NavMeshObstacle` : boîte ou cylindre, taille et centre dans l'espace
  de son entité, dont il suit l'échelle.
- **Ce qui est cuit** : les colliders qui ne bougent pas, sans `RigidBody` ou sous un `RigidBody`
  statique, ni déclencheurs ni personnages ; boîtes et maillages exacts (`MeshCollider`, par son
  maillage), sphères, capsules et cylindres à 12 côtés, triangles tournés vers l'extérieur. Ce que
  le joueur heurte est ce que les agents contournent.
- **Cuisson** : par tuiles de Recast (hauteur de champ, surfaces praticables, filtres des
  obstacles bas, des corniches et des passages trop bas, érosion du rayon de l'agent), jusqu'aux
  **couches** de chaque tuile, compressées par zstd, que l'asset garde avec les réglages et les
  bornes. Au chargement, DetourTileCache fait des couches le maillage de polygones de chaque tuile
  (bits de tuiles et de polygones calculés au plus juste) ; un obstacle ne reconstruit que les
  tuiles qu'il touche.
- **Asset** : `.dvxnavmesh`, un fichier binaire écrit par l'éditeur à côté de la scène (importeur
  `navmesh`, type `NavMesh`) : un maillage qu'on régénère, pas un fichier qu'on écrit à la main.
  L'importeur le lit, le vérifie et le garde tel quel ; l'export le copie comme les autres.
- **Monde** (`NavigationWorld`, un par scène jouée, créé avec la physique) : il marche le maillage
  de la première `NavMeshSurface` qui en a un (un avertissement s'il y en a d'autres) ; le maillage
  suit son asset (recuit : les agents repartent sur le nouveau). Chaque frame, après les systèmes
  Update et la mise à jour des transformées : les obstacles déplacés de plus de 10 cm ou tournés de
  plus de 0,05 rad sont redécoupés (boîtes tournées autour de Y seulement) ; les agents
  apparaissent, changent de réglages et partent avec leurs composants ; un agent que le jeu a
  déplacé repart de là (téléportation) ; la foule avance, puis chaque agent écrit sa position
  (par son `Transform`, sous son parent), sa vitesse, et se tourne vers où il va, debout autour de
  Y, à sa vitesse de rotation. Un agent arrive quand il est à sa distance d'arrêt, à plat.
  256 agents et 256 obstacles au plus ; quatre qualités d'évitement de DetourCrowd, `none` sans
  évitement ni séparation.
- **Jeu** : `SystemContext::navigation` (nul sans navigation) donne `setDestination` (au point du
  maillage le plus proche, avant même que l'agent marche), `stop`, `hasDestination`,
  `remainingDistance`, `path` (les coins devant l'agent), `findPath`, `samplePosition` et `raycast`
  le long du maillage ; l'API des jeux passe à 18. En C#, la classe `Navigation` :
  `SetDestination`, `Stop`, `HasDestination`, `RemainingDistance`, `FindPath` (tableau de `Vec3`),
  `SamplePosition` et `Raycast` (`out` position et normale) ; les vues des trois composants sont
  générées. L'amorce C# passe à 15.
- **Éditeur** : sous la `NavMeshSurface` de l'inspecteur, ce que le maillage contient (polygones,
  tuiles, triangles de départ), un avertissement quand les réglages ont changé depuis la cuisson,
  **Bake** et **Clear**. Bake cuit la scène ouverte (enregistrée, dans `assets/`), écrit
  `<scène>.dvxnavmesh` à côté d'elle (`<scène> <entité>` s'il y a plusieurs surfaces), attend son
  import puis pose l'asset dans le champ par une commande annulable, et dit le temps, les tuiles,
  les couches et la taille. La vue dessine le maillage (bords en cyan, arêtes intérieures
  estompées) de la surface sélectionnée, ou de toutes avec les collisions ; les agents en
  cylindres, les obstacles en contours, et pendant le jeu les chemins des agents. Le menu de
  création ajoute une surface et un agent.
- **Bac à sable** : dans l'arène, le robot patrouille par son `NavMeshAgent` (le code ne donne plus
  que ses destinations ; sa machine à états mélange la marche par la vitesse réelle de l'agent),
  deux drones suivent le joueur (`code/Follower.cs`) en se contournant, la porte fermée et les
  caisses (le préfab) découpent le maillage pendant qu'elles bougent.

### Culling et ombres locales

- **Boîtes englobantes** : l'import cuit dans le maillage la boîte qui tient tous ses sommets
  (version 4 de sa disposition, donc les projets se réimportent d'eux-mêmes). Un maillage construit
  par le code, ou importé avant, est mesuré quand il arrive sur le GPU. Chaque instance transforme
  cette boîte par sa matrice, en gardant la boîte des huit coins déplacés.
- **Tronc de vue** : `render::Frustum` lit les six plans d'une matrice de vue et projection, et
  répond si une boîte peut être vue. Un plan que la projection laisse indéfini, comme le plan
  lointain d'une projection infinie, revient vide et laisse tout passer ; une boîte jamais mesurée
  passe aussi, pour qu'un maillage ne disparaisse jamais par accident. C'est du calcul pur, donc
  c'est testé sans GPU.
- **Ce qui est testé** : la prépasse, l'ombrage, les surfaces transparentes et chaque vue d'ombre
  n'envoient que ce que leur vue garde. Une cascade ne teste que ses quatre côtés : ce qui est
  au-dessus d'elle, entre le soleil et le sol, projette toujours dedans. La sélection et le picking
  regardent la scène entière, puisqu'ils répondent sur un pixel plutôt que sur une image. Le
  panneau *Statistics* montre le nombre d'instances écartées.
- **Maillages animés** : la boîte de la pose de repos est agrandie de moitié, faute de connaître la
  pose du moment sans parcourir les os. Un bras levé très haut peut encore sortir de sa boîte.
- **Ombres locales** : `PointLight::castShadows` et `SpotLight::castShadows` les demandent. Chaque
  frame, les lumières qui en veulent et que la caméra peut voir sont classées par puissance divisée
  par le carré de leur distance ; les plus importantes reçoivent une tuile de 1024², les autres de
  512², dans un atlas `D32` de 4096². Un spot prend une tuile, une lumière ponctuelle six, une par
  face du cube autour d'elle. Une lumière qui ne tient plus garde sa lumière et perd son ombre.
- **Vues** : chaque tuile porte sa matrice et sa place dans l'atlas (`ShadowView`), que le shader
  lit par l'indice que la lumière garde (`firstShadowView`, -1 quand elle n'en a pas). Une lumière
  ponctuelle choisit la face de son cube d'après la direction vers la surface. Le filtrage est le
  même que celui des cascades, trois par trois, avec les taps bornés à l'intérieur de la tuile pour
  qu'une ombre ne bave pas sur sa voisine.
- **Bac à sable** : la première lampe de la scène `sandbox` projette tout autour d'elle et le
  projecteur dans son cône ; la nuit (touche N), les caisses et les cubes portent leur ombre.

### Transparence et post-traitements

- **Passes** : une frame dessine maintenant la **prépasse** (profondeur, mouvement et normales),
  l'**occlusion ambiante**, les **ombres**, la **scène** opaque puis le ciel, les surfaces
  **transparentes**, la mesure de luminance, l'**anticrénelage temporel**, la chaîne du **bloom**,
  le **tonemapping** avec l'étalonnage, les lignes et gizmos des outils, l'interface, enfin les
  panneaux. Le MSAA a disparu : le TAA le remplace, coûte moins cher et lisse aussi l'intérieur des
  matériaux.
- **Prépasse** : chaque instance opaque est dessinée une fois pour écrire la profondeur, le
  déplacement de ses pixels depuis la frame précédente et la normale de sa surface (pliée sur un
  octaèdre en deux nombres). L'ombrage qui suit teste la profondeur sans l'écrire : il ne calcule
  la lumière que pour la surface visible. Le coût est un dessin de plus par instance, rendu par le
  surdessin évité et par ce que la prépasse rend possible.
- **Mouvement** : le renderer garde la transformation de chaque instance et les matrices de ses os
  de la frame précédente, repérées par l'entité et le sous-maillage, et la vue et la projection
  sans jitter. Un objet qui apparaît ne bouge pas ; un personnage animé suit ses os, donc ses
  membres aussi.
- **Transparence** : un matériau en `blend` quitte la passe opaque et rejoint une passe qui les
  dessine du plus loin au plus proche, en testant la profondeur sans l'écrire, pour que deux
  surfaces transparentes se mélangent. Le tri se fait par le centre de l'instance : deux surfaces
  qui s'entrecroisent restent fausses, comme partout où l'on trie plutôt que de résoudre. Une
  surface transparente ne projette pas d'ombre, sans quoi une vitre assombrirait le sol comme un
  mur.
- **Anticrénelage temporel** : la projection est déplacée d'une fraction de pixel à chaque frame
  (suite de Halton, huit points), et la passe de résolution mélange la frame à ce que les
  précédentes ont résolu, retrouvé en suivant le mouvement de chaque pixel. L'historique est borné
  par les couleurs autour du pixel, ce qui empêche un objet qui bouge de traîner son passé ; les
  couleurs sont pondérées par leur luminance, ce qui empêche une étincelle d'entraîner tout le
  voisinage. Deux images d'historique alternent d'une frame à l'autre.
- **Occlusion ambiante** : huit points jetés dans l'hémisphère au-dessus de chaque pixel, comparés
  à la profondeur, donnent ce que la surface voit du ciel. Les points changent à chaque frame et le
  TAA fait la moyenne, ce qui suffit sans flou séparé. Le résultat ne multiplie que la lumière
  ambiante, jamais la lumière directe : c'est pour cela qu'il est calculé après la prépasse et
  avant l'ombrage.
- **Bloom** : l'image résolue est seuillée puis halvée cinq fois, chaque niveau filtré en treize
  points, puis rajoutée du plus petit au plus grand avec un filtre en tente. La chaîne entière est
  nommée par un seul descripteur, donc ses images restent dans la disposition générale : c'est la
  seule façon d'en lire une pendant qu'on en écrit une autre.
- **Étalonnage** : le tonemapping applique ensuite, dans l'ordre, l'aberration chromatique (au
  moment de lire l'image), la table de couleurs, la vignette et le grain. La table est une texture
  en bande de carrés, un par pas de bleu, lue entre les deux plus proches ; elle doit être importée
  **sans encodage sRGB**, sinon ses valeurs sont décodées avant d'être lues.
- **Réglages** : tout vit sur le composant `Camera`, à côté de l'exposition et du tonemapping :
  `antialiasing`, `ambient_occlusion` et son rayon, `bloom` et son seuil, `vignette`, `grain`,
  `chromatic_aberration` et `color_table`. Chaque effet s'éteint en mettant son réglage à zéro, et
  le renderer saute alors ses passes.
- **Bac à sable** : trois panneaux de verre teinté (`assets/materials/glass.dvxmat`) se croisent
  devant les sphères, et les satellites du plateau tournant brillent assez pour laisser un halo.

### Une seule interface, deux usages

- **La question** : l'éditeur est en Dear ImGui, les jeux ont `Devex::Ui`. Écrire deux systèmes
  d'interface serait le double du travail et de la maintenance ; c'est ce qu'Unity a longtemps fait
  avant de converger. Godot, lui, dessine son éditeur avec les mêmes nœuds que les jeux, et s'en
  porte bien. **La cible est donc une seule interface, celle des jeux, qui grandira jusqu'à porter
  l'éditeur.**
- **Pourquoi pas d'un coup** : l'éditeur demande des champs de saisie complets, des arbres de
  milliers de lignes, des tableaux, des séparateurs déplaçables, du docking, du glisser-déposer,
  des menus contextuels, des modales et des sélecteurs de couleur. Tout porter d'un coup rendrait
  l'éditeur moins bon pendant des mois ; `Devex::Ui` grandit donc d'abord, puis prend les panneaux
  un à un.
- **Le mythe du mode immédiat** : ImGui reconstruit son interface à chaque frame, mais le coût suit
  ce qui est visible, et l'éditeur tourne à plusieurs centaines d'images par seconde. `Devex::Ui`
  reconstruit d'ailleurs sa liste de dessin à chaque frame elle aussi ; la différence est que son
  **état** vit dans des entités plutôt que dans le code qui dessine. Ce qui manque vraiment à ImGui
  est ailleurs : l'apparence est contrainte, il n'y a pas d'animation, et la mise en page ne suit
  pas la fenêtre.
- **Le chemin** : faire grandir `Devex::Ui` avec ce dont les jeux ont besoin de toute façon, et qui
  se trouve être ce qui manque à l'éditeur — saisie de texte, défilement et découpe, thèmes
  réutilisables, liaison de données, puis listes virtualisées et tableaux. Ensuite seulement,
  porter l'éditeur panneau par panneau : les deux peuvent cohabiter dans la même frame, puisque
  l'un est dessiné par le renderer et l'autre par ImGui.
- **Le début** (jalon 40) : un panneau de l'éditeur est **fait d'entités**, comme Godot fait son
  éditeur de nœuds : une scène à lui, un canevas en `constant_pixels`, un `UiWorld` qui le place et
  lui répond, et le code du panneau qui crée ses entités une fois puis lit ses actions
  (`wasClicked`, `wasChanged`, `contextTarget`...) comme un script de jeu. `tools::detail::UiPanel`
  le loge dans une fenêtre ImGui : il prend la place qui reste, lui donne la souris et les touches
  que la fenêtre reçoit (avec la saisie de texte et la position de la méthode de saisie par
  `ImGuiPlatformImeData`), et montre son image (`ImGui::Image` d'une surface d'interface) ; le
  dock reste celui d'ImGui en attendant. Ses unités sont des points de texte, à l'échelle où ImGui
  dessine les siens (ImGui mesure une police à sa ligne entière, Devex UI à son em : sans cette
  correction, les lettres étaient 36 % plus grandes que leurs voisines), si bien qu'il suit
  l'échelle de l'interface. `EditorUiKit` partage entre les panneaux les polices de
  l'éditeur (Noto Sans normale et grasse, cuites en atlas de distances à 40 pixels), ses icônes
  (les SVG dessinés en textures blanches que les images teintent) et son **thème**, une liste de
  styles nommés (`panel`, `button`, `primary`, `row_selected`, `field`, `dropdown`...) refaits
  depuis les couleurs de l'éditeur quand elles changent. Les polices, le thème et les icônes ont
  des identifiants d'asset réservés que seul le kit résout. Premier panneau porté : le
  gestionnaire de projets.
- **La suite** (jalon 41) : *FileSystem* et *Output*. Les briques des panneaux (boutons, champs de
  recherche, menus qui prennent la hauteur de leurs entrées visibles, dialogues, infobulles aux
  couleurs de l'éditeur) sont partagées par `PanelBuilder`, et la police à chasse fixe
  (JetBrains Mono) rejoint le kit. Les deux listes sont **virtuelles** : seules les lignes à l'écran
  ont des entités, remplies d'après le défilement avant la mise en page ; l'arbre de FileSystem est
  aplati en lignes à chaque image selon les dossiers ouverts, et les dossiers du code sont relus
  une fois par seconde. Un panneau qui répond lui-même aux touches (l'arbre) coupe la navigation
  au clavier de Devex UI (`setKeyboardNavigation`). Le **glisser-déposer traverse la frontière**
  avec ImGui : ce que porte un panneau devient aussi un glisser ImGui
  (`ImGuiDragDropFlags_SourceExtern`, sans aperçu, l'infobulle d'ImGui prenant le relais hors du
  panneau), que la vue et l'inspecteur prennent comme les leurs ; un glisser ImGui qui passe sur
  le panneau y est annoncé (`carryFromOutside`), et ses cibles le prennent. Limite : les menus et
  les infobulles d'un panneau sont dessinés dans son image, ils ne peuvent pas en sortir (Godot en
  fait des fenêtres) ; les infobulles passent à la ligne pour y tenir.
- **Puis l'arbre de scène** (jalon 42), dans le même esprit que la fenêtre de création : lignes
  arrondies, icône teintée, guides fins, noms des entités de préfabs dans leur couleur, boutons de
  fin de ligne de la couleur de leur ligne (le pointeur les montre). Un panneau prend les glissers
  de tous les autres, portés par ImGui, sauf le sien : un fichier de FileSystem se lâche dans
  l'arbre. Tant qu'un champ de `Devex::Ui` a le clavier, ImGui le sait comme pour un de ses
  champs (`WantTextInputNextFrame`) : les raccourcis de l'éditeur laissent les lettres au champ.
  Les entrées de menu montrent leur raccourci contre leur bord droit.
- **Puis l'inspecteur** (jalon 43), celui des entités ; les inspecteurs des assets, des fichiers
  de code et des éléments du panneau Animator restent en ImGui dans la même fenêtre (dans un
  enfant qui défile, la fenêtre ne défilant plus elle-même), et le peintre de tuiles aussi, sous
  l'entité, comme Godot peint dans un panneau à part. **Comme Godot, en plus soigné** : une carte
  arrondie par composant, repliée depuis son en-tête (icône teintée, nom en gras, menu ⋮ et clic
  droit : *Remove Component*), des groupes repliables, une ligne par champ dont le contrôle suit
  la valeur. Les nombres se **glissent de côté** (Maj pour dix fois plus fin, le curseur devient
  une double flèche) et se **tapent** après un clic, sommes comprises ; les vecteurs sont des
  cases x, y, z, w aux lettres colorées ; les rotations s'éditent en angles d'Euler gardés tels
  quels pendant l'édition, pour qu'ils ne sautent pas ; les couleurs ouvrent un **sélecteur
  maison** (carré saturation-luminosité, barre des teintes, barre d'opacité sur damier,
  hexadécimal, et les nombres de la valeur, qui peuvent dépasser 1 pour une lumière) ; les
  énumérations, couches, groupes de sons, couches de tri, assets et entités sont des listes
  déroulantes ; les champs d'assets et d'entités prennent ce que FileSystem et l'arbre y lâchent,
  et ne s'éclairent que pour ce qu'ils acceptent (un glisser d'asset entre dans le panneau
  typé `asset:texture`, `asset:mesh`...). Un champ qu'un style écrit est grisé et dit pourquoi ;
  une valeur qui diffère du préfab est marquée d'un trait et d'un libellé en gras, et son menu
  (clic droit) la ramène. Le statut du style d'un élément, les commandes d'un émetteur et la
  cuisson du maillage de navigation ont suivi.
- **Comment le panneau suit la scène** : les entités du panneau sont refaites quand la
  **signature** de ce qu'il montre change (scène, entités choisies, composants qu'elles
  partagent, taille des listes, préfab, génération du registre des composants, taille du texte),
  et seulement alors ; à chaque image, les contrôles reprennent les valeurs de la scène (sauf ceux
  en cours d'édition), puis, après la mise à jour de l'interface, les changements vont à la scène
  en direct. **Une édition est un pas d'annulation** : il commence au premier changement (les
  valeurs de toutes les entités sont gardées) et se clôt quand plus rien ne tient le contrôle
  (`UiWorld::held()`, `editedField()`), si bien qu'un glisser, une frappe ou un réglage du
  sélecteur font chacun un pas ; un clic (case, liste, dépôt) en fait un tout de suite. Plusieurs
  entités montrent ce qu'elles partagent, un tiret où elles diffèrent (le format d'un nombre sans
  `{}`, le texte indicatif d'un champ, celui d'une liste sans choix) ; un nombre d'un vecteur ne
  change que cet axe chez chacune. Les longues listes (assets, entités de la scène) ne sont faites
  qu'au moment où elles peuvent s'ouvrir : sous le pointeur, avec le focus ou déjà ouvertes
  (`UiWorld::listedDropdown()`). Limites : pas de recherche dans ces listes (dix lignes et la
  molette), des libellés coupés plutôt qu'abrégés (l'infobulle donne `Composant.champ`), et le
  sélecteur de couleur reste dans l'image du panneau.
- **Puis le reste de la fenêtre** (jalon 44) : les assets, les fichiers de code et les éléments du
  panneau Animator ont chacun leur **page**, faite des mêmes pièces que les entités (en-tête avec
  l'icône et le fichier, cartes repliables, lignes d'un libellé et de ses contrôles, notes, rangées
  de boutons, grilles de sprites qui passent à la ligne avec la largeur du panneau). Une page
  (`InspectorPage`) dit sa signature, se construit, reprend ses valeurs avant la mise à jour de
  l'interface et lit ensuite ce qui a été cliqué, tapé, glissé et lâché ; elle vit dans le fichier
  de son domaine (`TextureInspector.cpp`, `CurveInspector.cpp`, `AnimatorPanel.cpp`...). Il n'y a
  plus d'ImGui dans l'Inspecteur : texture (aperçu sur damier avec la grille de découpe et les
  pivots), modèle, son (forme d'onde et tête de lecture), courbe (graphe dont on glisse les clés
  et les pentes, double clic pour ajouter, clic droit pour lisser, aplatir, redresser ou
  supprimer, préréglages), sprite frames (animations, aperçu qui joue, images réordonnées, doublées
  et supprimées depuis leur menu, sprites lâchés après une image ou au bout), tileset (palette
  animée, sprite, collision, données, images d'une tuile), animator (résumé), états et transitions
  de l'Animator (clips, paramètres, conditions, ordre), fichier de code, tout autre asset (son nom,
  son fichier, *Reimport*), et le peintre de tuiles sous le Tilemap d'une entité.
- **Les réglages d'import attendent** *Reimport*, comme dans le dock Import de Godot : une option
  changée est marquée et gardée de côté, *Revert* l'oublie, *Reimport* les écrit toutes dans le
  `.dvxmeta` et importe le fichier une seule fois (`AssetDatabase::setImportOptions`). Une texture
  n'est plus recompressée à chaque clic.
- **Puis les réglages et les dialogues** (jalon 45). Les pièces des pages quittent l'inspecteur
  pour une base commune, `FormUi` (cartes, lignes, bascules, listes, nombres, champs, pastilles de
  couleur et leur sélecteur, notes, rangées de boutons, grilles de sprites), dont héritent
  l'inspecteur et toutes les fenêtres ; les cartes des pages d'assets se replient désormais,
  comme celles des composants. **Editor Settings et Project Settings comme dans Godot** : les
  sections à gauche, un **filtre** au-dessus qui trouve un réglage par son nom dans toutes les
  sections (une carte dont le nom, ou celui de sa section, contient le filtre se montre entière ;
  les autres ne gardent que leurs lignes qui le contiennent, et les sections sans rien pâlissent),
  les cartes de la section choisie à droite. Project Settings a sept sections : *Application*
  (nom, scène de démarrage choisie dans la liste des scènes, icône lâchée depuis FileSystem),
  *Window*, *Physics*, *Collision Layers* (les noms, puis la **matrice en triangle aux noms
  penchés** par `UiRect.rotation` ; le texte ignorait jusqu'ici la rotation et l'échelle de son
  élément, il les suit désormais comme les images, dans les jeux aussi), *Sorting Layers* (montées, descendues,
  retirées), *Audio* et *Input Map*. Chaque action d'entrée a sa carte ; une liaison se choisit
  dans la **fenêtre d'événement**, comme celle de Godot : elle écoute la prochaine touche ou le
  prochain bouton de manette pressé, et liste toutes les touches, boutons, axes et sticks par
  appareil, avec un filtre (taper dans le filtre n'est pas une liaison ; Échap renonce).
  - **Où les modifications vont** : le thème s'applique pendant qu'on le change (la fenêtre
    garde la taille de son texte tant qu'un de ses contrôles est tenu, puis se refait), et
    s'enregistre dans les réglages de l'utilisateur une fois l'édition finie. Le projet est
    édité sur une copie, écrite dans le `.dvxproj` quand plus rien n'est glissé ni tapé ; un
    champ texte ne compte qu'une fois quitté (Entrée ou clic ailleurs, Échap l'abandonne). Les
    noms qui n'ont pas de sens sont corrigés à l'écriture (nom du jeu vide, couches de tri
    en double).
  - **Des fenêtres flottantes**, pas des modales : Editor Settings, Project Settings, Export et
    C# Debugging restent des fenêtres qu'on déplace et redimensionne, pour voir le thème changer
    ou régler un volume pendant que le jeu tourne ; leur contenu est une image de `Devex::Ui`
    qui les remplit. **Les dialogues** (New Script, modifications non enregistrées, About) sont
    une carte au milieu de la fenêtre sur un voile, comme la fenêtre de création ; New Script
    vérifie le nom pendant qu'on le tape (identifiant, composant existant, fichier déjà dans
    `code/`) et montre le fichier qu'il écrira. *Save as Prefab* reste le dialogue de fichier du
    système.
  - Chaque fenêtre a son image (surfaces 7 à 11) ; le pool de descripteurs du backend Vulkan
    d'ImGui passe de 16 à 64 jeux, une image par fenêtre et par image en vol. Limites : pas de
    bouton de retour à la valeur par défaut par réglage (seulement *Reset to Defaults* pour le
    thème), et la fenêtre d'événement ne prend les boutons de la souris que dans la liste.
- **Puis les panneaux de mesure** (jalon 46). **Statistics devient des moniteurs, comme ceux de
  Godot** : l'appareil en haut (GPU, pilote, présentation, swapchain), puis une carte repliable par
  mesure avec sa courbe sur les 240 dernières images, sa valeur actuelle et son maximum — durée
  d'image (avec les repères 60 et 30 FPS), draw calls, instances écartées, mémoire GPU, envois,
  entités — et les ressources en dessous ; l'infobulle d'une courbe donne la valeur de l'image sous
  le pointeur. Les mesures sont relevées à chaque image, panneau montré ou non (`Monitors`).
  **Le Profiler** garde sa forme : une barre par image (le travail en couleur, l'attente pâle
  au-dessus, verte, jaune ou rouge selon la durée ; un clic regarde l'image et met en pause), la
  timeline des zones de l'image regardée (une ligne par thread, zones imbriquées, zoom à la
  molette autour du pointeur, glisser pour se déplacer, double clic pour tout revoir, infobulle de
  la zone sous le pointeur), et les onglets CPU (arbre repliable des temps, total, propre et
  appels), GPU (passes du graphe de rendu) et Memory (assets chargés). Côte à côte dans un panneau
  large, l'un au-dessus de l'autre dans un haut, avec une barre à glisser entre les deux.
  - **Des entités réutilisées pour la timeline** : chaque zone visible prend un rectangle dans une
    réserve et y est placée à chaque image ; seules les zones en vue en ont un. Les infobulles
    viennent de Devex UI ; le zoom et le glisser sont lus à la main dans l'entrée du panneau. Le
    temps d'attente de chaque image enregistrée est trouvé une fois, et seule la zone sous le
    pointeur écrit son infobulle : le panneau ne coûte presque rien en Release.
  - Le Profiler ouvert depuis le menu passe devant les autres onglets de son dock.
- **Puis le cadre de l'éditeur** (jalon 48) : la barre de menus, la barre d'état, les onglets des
  scènes et la barre d'outils de la vue. Ce sont des bandes fines, chacune un panneau Devex UI dans
  sa fenêtre ImGui (surfaces 14, 16 et 17) ; le dock entre elles reste celui d'ImGui.
  - **Une couche au-dessus de la fenêtre pour les menus et les infobulles.** Une bande de trente
    points n'a pas la place de dessiner un menu ni une infobulle dans sa propre image. Choix : une
    **couche** (surface 15) qui couvre toute la fenêtre, transparente, dessinée seulement tant
    qu'un menu est ouvert ou qu'une infobulle se montre ; elle ne prend la souris que pour un menu.
    Écarté : agrandir l'image de chaque bande (elle couvrirait les panneaux et leur prendrait la
    souris) et garder les menus d'ImGui (deux apparences pour les mêmes menus). Côté moteur,
    `UiWorld::setTooltipsDrawn(false)` et `UiWorld::shownTooltip` laissent un outil montrer
    l'infobulle ailleurs que dans l'image ; `UiPanel::setTooltipsOutside` s'en sert.
  - **Des menus décrits en données** (`MenuEntry` : icône, libellé, raccourci, coche, séparateur,
    action, entrées du sous-menu). Un seul `UiPopup` de type menu les montre, avec le sous-menu
    **dans le même popup**, à côté de l'entrée survolée : un appui dans le sous-menu n'est pas un
    appui hors du menu. Comme dans toute barre de menus, un titre ouvre son menu au clic, et le
    pointeur sur un autre titre y passe tant qu'un menu est ouvert ; une entrée qui n'ouvre qu'un
    sous-menu ne ferme rien ; Échap ou un appui ailleurs ferme. Les bandes et les menus ne font
    qu'**emprunter le clavier** : il revient à la fenêtre où l'on travaillait dès que le menu se
    ferme ou que le bouton est lâché, avant que l'entrée choisie n'agisse (elle peut ouvrir sa
    propre fenêtre).
  - **Les onglets des scènes, comme dans Godot** : glisser un onglet sur un autre prend sa place
    (`UiDragSource` et `UiDropTarget`, `SceneTabs::move`), la croix ou le bouton du milieu ferme, un
    point marque ce qui n'est pas enregistré, l'infobulle donne le chemin, **+** ajoute une scène
    et reste en vue au bout des onglets ; quand ils débordent, la molette les fait défiler et
    l'onglet qui vient à l'écran est ramené en vue. L'onglet affiché prend la couleur de la barre
    d'outils sous lui. Pendant le jeu : ni croix ni **+**, et un mot sur le jeu (*Playing*,
    *Paused*) à la place des outils.
  - **Des boutons clairs tant qu'on ne les survole pas** : les styles teintent la couleur d'un
    bouton, et un bouton transparent n'a rien à éclaircir. Les boutons des barres changent donc de
    style — `bar_button`, `bar_hover` sous le pointeur, `bar_selected` tant que ce qu'ils
    représentent est actif — ce qui marche aussi sur le thème noir, où multiplier ne donne rien.
  - **La barre des outils par-dessus un jeu (F1) est la même** (`MenuBarUi`), avec ses deux menus,
    *Edit* et *View*. `ToolsOverlay::openMenu` ouvre un menu comme un clic sur son titre, pour les
    tests.
  - Coût mesuré sur le banc caché, en Debug : 7,1 → 7,9 ms par image pour les trois bandes (0,2 ms
    les menus, 0,1 ms la barre d'état, 0,2 ms l'en-tête de la vue) ; la couche ne coûte rien tant
    qu'elle ne montre rien. Limites : un seul niveau de sous-menu, toujours ouvert à droite ; pas
    d'accès aux menus par Alt ; l'appui qui ferme un menu n'agit pas sur ce qui est dessous ; pas
    de menu contextuel sur les onglets (ajouté depuis, voir le jalon 51) ; les onglets des panneaux
    ancrés restent ceux d'ImGui.
- **Puis le ménage avant le dock** (jalon 51). Ce qui reste d'ImGui dans l'éditeur, une fois tous
  les panneaux portés, est le dock lui-même et les fenêtres qui accueillent les images des
  panneaux. Avant de s'y attaquer :
  - **Les dessins par-dessus la vue passent en Devex UI.** La vue et le 2D posent des marques
    (`ViewportMarks` : le rectangle d'une sélection à la souris, le cadre du jeu, le contour, les
    poignées et les ancres de l'élément d'interface choisi, le mot sur ce que montre l'écran, le
    cadre du jeu qui tourne) ; un panneau transparent (surface 21), d'une unité par pixel de la vue,
    les dessine avec des `UiLine`, des images et un texte, une fois que tout ce qui répond à la
    souris sur la vue a répondu. Il ne prend rien : la vue sous lui garde le pointeur.
  - **Les widgets ImGui sans appelant disparaissent** : `Widgets.hpp/.cpp` (boutons d'outil,
    listes, champs de recherche, grilles de propriétés, vecteurs glissés), le sélecteur d'asset en
    ImGui et `uiColorU32`.
  - **Mesures** (bancs cachés de `devex_tools_tests`, cinq passages, la médiane) : la disposition
    par défaut prend 7,9 ms par image en Debug, comme au jalon 48 ; l'écran Script sur un fichier
    de cinq mille lignes 13,0 ms, dont 3,5 ms à placer les lettres des lignes en vue ; le panneau
    Animator 6,7 ms. En Release l'image attend la synchronisation de l'écran (4,2 ms à 240 Hz), et
    l'éditeur prend 0,43 ms de processeur par image, 0,58 ms avec l'écran Script et ses cinq mille
    lignes, 0,44 ms avec l'Animator : ce que coûte une zone de texte ne grandit pas avec son texte.
  - **Puis les lignes d'une zone de texte gardent leurs lettres** d'une image à l'autre, comme les
    textes du jalon 47 : `TextCache::areaLine` garde chaque ligne en vue et son numéro, placés
    depuis l'origine et déplacés au dessin, et ne les place à nouveau que si leur texte change ;
    une ligne qui sort de la vue est oubliée. L'écran Script sur cinq mille lignes passe de 13,0 à
    10,5 ms par image en Debug (le dessin des panneaux de 4,8 à 2,6 ms), et de 0,32 à 0,23 ms de
    dessin en Release.
  - **Puis l'écran Script garde les couleurs de ses lignes** : les mots de chaque ligne en vue sont
    trouvés une fois et gardés tant que la ligne, son langage et le commentaire ouvert avant elle ne
    changent pas ; les couleurs des sortes de mots sont calculées une fois par image. L'écran Script
    passe de 10,5 à 9,5 ms par image en Debug, et le panneau de texte de 0,17 à 0,11 ms en Release
    (l'éditeur de 0,60 à 0,52 ms).
  - **Puis ce qu'on glisse hors d'un panneau se dit en Devex UI** : son nom suit le pointeur dans la
    couche au-dessus de l'éditeur, qui remplace l'infobulle d'ImGui, et c'est le même nom que dans le
    panneau (`ball.dvxscene`, plus `ball (scene)` au dehors). La vue qui accepte un asset ne
    s'encadre plus du rectangle d'ImGui (`ImGuiDragDropFlags_AcceptNoDrawDefaultRect`) : comme dans
    Godot, elle reste nette, et un matériau montre toujours la surface qui le prendra.
    `dragAsset`, une source de glisser ImGui sans appelant, disparaît. Le glisser reste un glisser
    ImGui sous le capot, pour que la vue le prenne, jusqu'au dock.
  - **Puis le C# colore les zones de texte** comme l'écran Script : `Ui.SetTextColors` (les mots),
    `Ui.SetTextHighlights` (ce qu'une recherche trouve), `Ui.SetTextMarks` (une erreur) et
    `Ui.VisibleTextLines` (ce qui vaut d'être coloré). Les positions comptent les caractères de la
    string C#, comme `IndexOf` ; le moteur les traduit en octets UTF-8 d'un seul passage sur le
    texte de la zone (une lettre hors du premier plan fait deux caractères et quatre octets), et
    range les couleurs dans l'ordre du texte, que le dessin demande. `UiWorld::setTextSpans` et ses
    voisins prennent la scène, pour garder ce qu'on donne à une zone que le monde n'a pas encore
    vue : depuis le `Start` d'un script. Écartés : les octets en C# (faux dès le premier accent) et
    la sélection et l'annulation, qui peuvent attendre qu'un jeu en ait besoin. Le bootstrap passe
    à la version 20.
  - **Puis le menu des onglets de scène**, au clic droit, comme celui de Godot : *Close Tab*,
    *Close Other Tabs*, *Close Tabs to the Right*, *Close All Tabs* et *Show in FileSystem*. Il
    s'ouvre sous le pointeur dans la couche au-dessus de l'éditeur, et tient les onglets par leurs
    identifiants, qui ne changent pas quand on les range. `PendingAction` ferme désormais une liste
    d'onglets : ceux qui ont des changements non enregistrés sont demandés une seule fois, dans la
    fenêtre qui les liste comme en quittant, et une scène vide prend la place de la dernière.
    *Show in FileSystem* amène le panneau devant, ouvre les dossiers autour du fichier, le choisit
    sans toucher à l'inspecteur, le met au milieu de la liste et efface un filtre qui le cacherait
    (`revealInFileSystem`). Écartés pour l'instant : les entrées d'enregistrement, que le menu
    Scene offre déjà, *Undo Close Tab* et *Play This Scene*.
  - **Puis Ctrl+K commente les lignes**, comme dans Godot : Ctrl+/ ne répondait pas sur un clavier
    AZERTY, où / se tape avec Maj. Les lettres suivent la disposition du clavier, Ctrl+K marche donc
    partout ; Ctrl+/ reste pour les claviers où / est une touche, et le / du pavé numérique aussi.
    Le menu Edit et la barre d'état de l'écran Script annoncent Ctrl+K. Au passage, le curseur et
    la sélection restent sur leurs lettres quand une ligne gagne ou perd `// ` : ils gardaient leur
    place en octets, ce qui envoyait le curseur d'une fin de ligne sur la suivante.
- **Puis l'éditeur de texte** (jalon 50), le dernier panneau en ImGui, qui reposait sur le champ
  multi-ligne d'ImGui (curseur, sélection, annulation, défilement). Le champ de Devex UI, `UiInput`,
  replace tout son texte à chaque image : bien pour une ligne, pas pour un fichier de code.
  - **`UiTextArea`, un composant du moteur**, comme Godot sépare `LineEdit` et `TextEdit` : un texte
    de beaucoup de lignes (celui du `UiText` à côté, comme pour un champ), qui défile dans les deux
    sens au lieu de revenir à la ligne. **Seules les lignes en vue sont placées** : le monde garde
    où commence chaque ligne et la largeur de la plus longue, ce qui fait coûter à un fichier de
    milliers de lignes ce que son écran montre. Écartés : étendre `UiInput` (le champ d'un
    formulaire et l'éditeur de code n'ont pas les mêmes règles) et une zone écrite dans les outils
    seulement (rien pour les jeux, et la logique d'édition hors du moteur).
  - Ce qu'il fait : curseur et sélection à la souris (double clic pour le mot, triple pour la ligne,
    glisser, Maj+clic), flèches, Ctrl+flèches par mots, Origine sur la première lettre puis le
    début de la ligne, Ctrl+Origine/Fin, pages ; Entrée garde l'indentation (`auto_indent`) et
    l'augmente après les caractères de `indent_after` ; Tab écrit des espaces jusqu'au prochain
    arrêt, déplace les lignes sélectionnées, Maj+Tab les ramène, et Retour arrière dans
    l'indentation recule d'un arrêt ; une tabulation du texte est aussi large que `tab_size`
    espaces ; copier, couper, coller (sans les retours chariot d'un autre système) ; lecture seule
    (`read_only`) ; numéros de ligne ; ligne du curseur éclairée ; barres de défilement à glisser ;
    molette.
  - **L'annulation vit dans le monde**, par zone : les lettres tapées à la suite font un pas
    jusqu'à la fin d'un mot, et **ce qu'un outil ou un script écrit dans le texte devient un pas
    comme un autre** — le monde garde le texte tel qu'il l'a vu et en déduit ce qui a changé (du
    premier octet différent au dernier). Rechercher-remplacer, commenter, compléter s'annulent
    donc d'un coup, sans rien demander aux outils. `forgetTextHistory` repart de zéro quand le texte
    devient un autre (un fichier relu).
  - **Les couleurs viennent de l'outil**, pour les lignes en vue seulement : des morceaux
    (`TextSpan`) que le monde garde hors de la scène, des morceaux dessinés derrière les lettres
    (ce que la recherche a trouvé) et des lignes marquées (erreurs d'une compilation). Le monde dit
    quelles lignes sont en vue, où est le curseur (pour la liste des complétions et l'IME), quelle
    ligne est sous le pointeur, et sélectionne à la demande. `UiInput` reçoit en plus les pages,
    Tab, l'annulation et le modificateur des mots, depuis l'éditeur comme depuis un jeu.
  - **L'écran Script, comme celui de Godot** : plus d'onglets ; à gauche la liste des fichiers
    ouverts (point des modifications, croix et bouton du milieu pour fermer, filtre) et, dessous,
    ce que le fichier montré déclare ; au-dessus du texte, des menus **File** (nouveau script,
    ouvrir, enregistrer, tout enregistrer, relire, éditeur externe, fermer), **Edit** (annuler,
    refaire, commenter) et **Search** (chercher, remplacer, suivant, précédent, aller à la ligne),
    qui passent par la couche au-dessus de la fenêtre ; les barres de recherche, de remplacement et
    d'aller à la ligne s'ouvrent au-dessus du texte ; la ligne du curseur, le langage et
    l'encodage dessous. **Chaque fichier ouvert a sa zone**, qui garde son curseur, son défilement
    et son annulation tant qu'il reste ouvert (avant, changer d'onglet remettait tout à zéro). La
    liste des complétions prend les flèches, Entrée et Tab au texte tant qu'elle est ouverte
    (`UiPanel::setInputFilter`), et reste fermée après un choix ou Échap jusqu'à ce qu'on tape
    autre chose.
  - Limites : pas de retour à la ligne automatique dans une zone ; pas de sélection en colonnes ni
    de curseurs multiples ; les couleurs par morceau ne sont pas encore offertes au C# (le
    composant, lui, l'est ; elles le sont depuis, voir le jalon 51) ; Ctrl+/ ne répond pas sur un
    clavier AZERTY (le menu Edit le fait ; Ctrl+K depuis, voir le jalon 51) ;
    la liste de tous les scripts du projet a disparu de l'écran : ils s'ouvrent depuis FileSystem.
- **Puis Animation et Animator** (jalon 49), les deux derniers panneaux ancrés encore en ImGui
  (surfaces 18 et 19). Ils demandaient au moteur deux choses qu'il n'avait pas, et que les jeux
  gagnent aussi : des traits et des marques.
  - **`UiLine`, un composant du moteur pour les flèches du graphe.** Une ligne brisée entre des
    points, donnés en unités depuis le coin haut gauche de son élément, avec son épaisseur, sa
    couleur, une pointe de flèche au bout ou au milieu (`arrow`, `arrow_size`) et la fermeture sur
    le premier point (`closed`) ; un quad par segment, et le coin que deux segments laissent ouvert
    à l'extérieur du virage est comblé. Écarté : des rectangles tournés pris dans une réserve, qui
    ne donnaient rien aux jeux et raccordaient mal. Les jeux s'en servent pour un arbre de
    compétences, des liens entre des éléments, un tracé sur une carte ; en C# comme tout composant.
  - **`UiPlot` avec des marques pour les clés.** Un clip a des milliers de clés : une entité par
    losange était trop. `UiPlotKind::Marks` dessine un losange par valeur, qui est alors une
    **place le long du tracé** entre `min_value` (bord gauche) et `max_value` (bord droit) ; ce qui
    est au-delà n'est pas dessiné, `line_width` est la demi-taille d'une marque, `highlighted` en
    dessine une dans la couleur d'éclairage, et `plotValueAt` rend la marque la plus proche sous
    le pointeur. Une piste de la timeline est donc une seule entité, quel que soit son nombre de
    clés, et zoomer ne fait que changer ses deux bornes.
  - **La timeline d'Animation, comme celle de Godot** : la règle reste en haut, les noms des os à
    gauche, les pistes défilent à la molette (avant, celles qui dépassaient étaient coupées) ;
    Ctrl+molette zoome autour du pointeur, Maj+molette ou le bouton du milieu glisse le long du
    clip, un double clic revoit tout le clip ; appuyer puis glisser sur la règle ou les pistes
    déplace la tête de lecture. Le clip se choisit dans une liste, qui ne lit tous les clips du
    projet que lorsqu'elle peut s'ouvrir.
  - **Le graphe de l'Animator garde ses gestes**, lus à la main dans l'entrée du panneau comme
    avant dans celle d'ImGui : clic pour choisir, glisser un état, bouton droit ou du milieu pour
    déplacer la vue, molette pour zoomer autour du pointeur, clic droit pour le menu de ce qui est
    dessous, *Make Transition* puis clic sur l'état d'arrivée, clips lâchés depuis FileSystem.
    Les nœuds sont des entités prises dans une réserve et placées à chaque image (un cadre de la
    couleur de la bordure, l'intérieur, deux textes, la progression de l'état en jeu) ; les liens
    sont des `UiLine`, et l'infobulle d'un lien passe par la couche au-dessus de la fenêtre. Les
    paramètres sont à gauche d'une barre qui se glisse (`UiSplitter`) : le nom se tape, la valeur
    se glisse ou se tape, et pendant le jeu ce sont les valeurs du jeu que l'on voit et que l'on
    change. Un changement est enregistré une fois lâché, en un pas de l'historique du panneau.
  - Les panneaux ouverts depuis le menu passent devant les autres onglets de leur dock. Limites :
    pas de barre de défilement horizontale sur la timeline zoomée ; les clés se lisent mais ne
    s'éditent pas (les clips viennent des modèles importés) ; les liens sont droits ; les traits
    ne sont pas lissés au-delà de ce que fait le rendu de l'interface.

### Interfaces

- **Modèle** : une interface est faite d'**entités et de composants**, comme le reste d'une scène,
  plutôt que d'un arbre séparé : la hiérarchie, les préfabs, l'inspecteur, l'annulation et le C#
  s'y appliquent sans rien de particulier. `Canvas` marque la racine d'une interface ; `UiRect`
  donne à chaque élément sa place ; `UiImage`, `UiText` et `UiButton` disent ce qu'il montre et ce
  qu'il répond ; `UiLayout` place les enfants à la place de leurs ancrages.
- **Canevas** : plein écran seulement, dessiné par-dessus le jeu dans l'ordre de son `sortOrder`.
  En `scale_with_screen`, l'interface est posée à sa résolution de référence puis mise à l'échelle
  pour la fenêtre, en mélangeant les rapports de largeur et de hauteur en logarithmes selon
  `match_width_or_height` ; en `constant_pixels`, une unité vaut un pixel. Un canevas non
  interactif laisse passer la souris.
- **Ancrages et marges** : `anchor_min` et `anchor_max` sont les fractions du parent dont
  dépendent les coins, `offset_min` et `offset_max` les écartent en unités. Des ancrages égaux sur
  un axe donnent une taille fixe, des ancrages séparés étirent l'élément avec son parent. Le pivot
  sert à la rotation et à l'échelle ; X va à droite et Y vers le bas, comme l'écran.
- **Conteneurs** : `UiLayout` range les enfants en ligne, en colonne ou en grille, avec un
  espacement, un remplissage et un alignement. Sur l'axe du conteneur, un enfant garde la taille de
  ses marges s'il ne s'étire pas, sinon il partage ce qui reste ; en travers il suit ses propres
  ancrages. Les enfants cachés ne prennent pas de place, ce qui referme le trou d'une entrée
  masquée. La grille donne à chaque case la même taille.
- **Polices** : un `.ttf` ou `.otf` est importé en **atlas de distances signées** (stb_truetype,
  `size` et `spread` en options d'import), ce qui garde les lettres nettes à toute taille sans
  cuire une police par taille. L'atlas est une texture à un canal (`R8Unorm`) ; le shader compare
  la distance lue au demi-seuil et adoucit le bord de la moitié d'un pixel, calculée à partir de
  l'étalement et de la taille dessinée. Sont cuits : le latin de base et Latin-1, le latin étendu A
  (le `œ` du français et les lettres d'Europe centrale), la ponctuation courante (tirets,
  guillemets courbes, points de suspension, la puce `•` d'un mot de passe) et `€`. Les autres
  écritures demanderont une police cuite pour elles. Les **paires de crénage** de la police (table
  `kern` lue par stb_truetype) sont cuites avec l'atlas, seulement celles qui bougent, triées pour
  être trouvées par dichotomie.
- **Texte** : `UiText` porte son texte, sa police, sa taille en unités, son alignement, le retour à
  la ligne (coupé aux espaces, sinon au caractère), l'interligne et un contour dessiné en
  repassant les lettres autour d'elles. La mise en page est pure (`ui::layoutText`) : elle rend des
  quadrilatères, les images en ligne et les **arrêts du curseur** (un avant chaque caractère et un
  en fin de ligne), et se teste sans GPU. Les champs, la sélection et le clic entre deux lettres
  s'appuient tous sur ces arrêts.
- **Texte riche** : avec `rich`, les marques `[b]`, `[i]`, `[color=#rrggbb]`, `[size=32]` et
  `[icon=0]` changent l'apparence d'un passage, et `[/b]`, `[/i]`, `[/color]`, `[/size]` la
  referment ; `[[` écrit un crochet, et une marque inconnue reste écrite telle quelle. Le gras est
  simulé en dessinant la lettre deux fois, l'italique en la penchant : une police ne cuit qu'une
  graisse. Les icônes sont les textures listées dans `icons`, à la taille du texte qui les entoure.
- **Découpe et défilement** : `UiRect::clip_children` découpe tout ce qui est dessous au rectangle
  de l'élément. Chaque lot de dessin porte son rectangle de découpe, appliqué en scissor : un
  changement de découpe coupe le lot. `UiScroll` déplace ses enfants de son `offset`, les découpe
  à lui-même, et la molette fait défiler la liste la plus profonde sous le pointeur, sans aller
  plus loin que ce que le contenu dépasse.
- **Neuf parts** : les bords d'une `UiImage` texturée sont des fractions **de la texture** : une
  bordure de 0,3 sur une image de 64 pixels dessine des coins de 19 unités quelle que soit la
  taille du rectangle, sans dépasser sa moitié. L'`AssetManager` retient la taille de chaque
  texture chargée pour cela.
- **Champs** : `UiInput`, posé à côté d'un `UiText` qui garde le texte tapé, rend un élément
  éditable. Un clic place le curseur entre deux lettres et commence une sélection que le glisser
  étend ; les flèches, Début et Fin déplacent le curseur (avec Maj, la sélection), Haut et Bas
  changent de ligne dans un champ multiligne ; Retour arrière et Suppr effacent un caractère
  entier, pas un octet. Ctrl+A, Ctrl+C, Ctrl+X et Ctrl+V passent par le presse-papiers de SDL ;
  un champ `password` montre un point par caractère et ne donne jamais son texte au
  presse-papiers. Entrée termine l'édition (`Ui.WasSubmitted("name")`) ou, en multiligne, va à
  la ligne ; Échap rend le clavier sans toucher au texte et sans fermer le menu autour.
  `max_length` limite en caractères, les caractères de contrôle n'entrent jamais, un texte de
  remplacement grisé s'affiche tant que le champ est vide, le curseur clignote, et `padding`
  écarte les lettres des bords. Le curseur est gardé en octets du texte réel ; pour un mot de
  passe, il passe au texte de points en comptant les caractères.
- **Saisie du système** : tant qu'un champ est édité, le runtime active la saisie de texte de SDL
  (`Platform::setTextInput`), ce qui fait composer les touches mortes et ouvre la méthode de
  saisie ; ce qui est tapé arrive dans `Input::typedText`, distinct des touches. Les touches
  tenues se répètent (`Input::wasKeyRepeated`) pour continuer d'effacer ou de déplacer. Les
  raccourcis de texte suivent la **lettre imprimée** sur la touche (`Input::wasLetterPressed`)
  plutôt que sa place : sur un clavier français, Ctrl+A est à la place du Q américain, et c'est
  bien lui qui sélectionne tout.
- **Curseurs et cases** : `UiSlider` fait glisser une valeur entre `min_value` et `max_value`,
  arrondie à `step` ; le rectangle est la piste, la partie remplie et la poignée sont dessinées
  par-dessus l'image. Les flèches le déplacent quand il a le focus, sans déplacer le focus.
  `UiToggle` s'inverse au clic ou au bouton de validation et dessine sa marque dans sa boîte.
  Tous deux prennent le focus comme un bouton et signalent leur changement par leur action
  (`Ui.WasChanged("volume")`) ; la valeur se lit dans le composant.
- **Liaison de données** : `UiBinding` lit un champ d'un composant — de son entité ou de celle
  qu'il nomme par `source` — par la réflexion, et écrit `format` dans le `UiText` voisin en
  remplaçant chaque `{}` par la valeur, une fois par frame avant la mise en page. Un nom comme
  `position.x` ou `color.a` choisit une composante ; `decimals` fixe les chiffres après la
  virgule. Un score, une barre de vie ou la valeur d'un curseur suivent ainsi le jeu sans script.
- **Thèmes** : un fichier `.dvxtheme` est un asset (`theme`) de **styles nommés** ; chaque section
  `[style name="panel" component="UiImage"]` donne des valeurs de champs d'un composant, et un
  style peut toucher plusieurs composants. Le canevas nomme son thème (`Canvas::theme`) et chaque
  élément le style qu'il suit (`UiRect::style`) ; `ui::ThemeApplier` écrit ces valeurs dans les
  composants de l'élément à chaque frame, avant la mise en page, par la réflexion, sans ajouter de
  composant qu'il n'a pas. Les valeurs qu'un style nomme appartiennent donc au thème : un script qui
  les change est repris à la frame suivante. Chaque texte de valeur n'est lu qu'une fois (le cache
  est indexé par le texte, qui reste juste quand un thème est relu), et un thème modifié s'applique
  à la frame qui suit sa réimportation.
- **Des styles compilés** : chaque style d'un thème est fait une fois en la liste des champs qu'il
  écrit (type de composant, champ, valeur lue, et pour les valeurs simples — nombres, vecteurs,
  booléens, énumérations — leurs octets, recopiés tels quels) ; le thème d'un canevas est demandé
  une fois par passe, et partagé par les éléments d'un même parent. Chercher chaque frame le style
  par son nom parmi une cinquantaine, puis le composant et le champ par leurs noms, coûtait à
  l'éditeur, fait de milliers d'éléments stylés, 14 ms par image en Debug : 1 ms désormais. Un
  thème qu'aucun élément ne suit est oublié, et tout est refait quand le code du jeu est rechargé.
- **Des lettres gardées** (jalon 47) : placer les lettres d'un texte (lire ses caractères, couper
  ses lignes, l'aligner) est la partie lente de son dessin, et la plupart des textes disent la même
  chose au même endroit pendant longtemps. `ui::TextCache` garde donc, **par élément**, les lettres
  telles qu'elles ont été placées, dans une boîte dont le coin est à l'origine : le dessin les
  déplace là où se tient l'élément, si bien qu'un texte qui défile ou qu'on déplace n'est pas
  replacé. Elles ne le sont que si le texte, son style, sa police ou la taille de sa boîte
  changent — une comparaison par image, sans hachage — et un texte qui n'est plus dessiné est
  oublié au balayage qui suit. Un champ garde à part ce qu'il contient et ce qu'il montre quand il
  est vide. `UiWorld` a son cache, que chaque dessin prend sauf si l'appelant donne le sien
  (`DrawContext::textCache`) ; les jeux en profitent sans rien faire. Les listes déroulantes
  ouvertes et les infobulles, passagères, sont placées à chaque image. Côté éditeur, les largeurs
  mesurées par les panneaux (`EditorUiKit::textWidth`) sont gardées par police et par taille.
  Mesuré sur Sandbox en Debug : la liste de dessin passe de 3,2 à 1,2 ms, et l'image de l'éditeur,
  à 26,5 ms avant les styles compilés, à 7,1 ms.
- **Profilage** : `UiWorld` mesure ses styles, sa mise en page et sa liste de dessin, et l'éditeur
  chacun de ses panneaux (arbre de scène, FileSystem, inspecteur, fenêtres...) : le panneau
  Profiler dit où va le temps. Un banc de mesure caché (`devex_tools_tests "Editor frame
  benchmark"`) ouvre une copie de Sandbox dans une fenêtre cachée et imprime ces zones.
- **Thèmes dans l'éditeur** : hors Play, l'éditeur applique lui aussi les thèmes à la scène éditée,
  pour que l'écran 2D et l'inspecteur montrent l'interface telle que le jeu la dessinera ; ces
  écritures ne passent pas par l'historique et ne marquent pas la scène comme modifiée. Dans
  l'inspecteur, les champs qu'un style écrit sont grisés et ne se modifient pas (une infobulle dit
  pourquoi), et une ligne sous le champ `style` dit combien de champs le style écrit, ou pourquoi
  il n'en écrit aucun (pas de thème sur le canevas, pas de style de ce nom), avec **Open** qui
  ouvre le thème dans l'écran Script. `ui::styleOf` trouve le style d'un élément : le thème du
  canevas le plus proche au-dessus de lui, puis le style de ce nom.
- **Dessin** : `ui::buildDrawList` transforme les éléments placés en sommets, indices et lots que
  le renderer dessine en une passe après le tonemapping, dans l'image du jeu (donc aussi dans le
  viewport de l'éditeur). Les lots se rejoignent tant que la texture et le genre ne changent pas ;
  les coins arrondis et les lettres ont leur propre shader. Les couleurs sont **linéaires**, comme
  partout dans le moteur, et l'écran 2D les convertit pour les montrer telles qu'elles seront.
- **Entrées** : `ui::UiWorld` place les canevas chaque frame, cherche l'élément sous le pointeur du
  plus haut canevas au plus bas et de l'élément dessiné en dernier au premier, et n'appelle un clic
  que si l'appui et le relâchement tombent sur le même bouton. Le clavier (flèches, Entrée, Échap)
  et la manette (croix, stick gauche, boutons sud et est) déplacent le focus vers le bouton le plus
  proche dans la direction demandée, en comptant double ce qui est de travers. Un bouton porte une
  **action** nommée que le jeu lit (`Ui.WasClicked("play")`), et teinte l'image de son entité selon
  qu'il est survolé, enfoncé ou inutilisable.
- **Manettes** : `Devex::Platform` ouvre les manettes SDL (quatre au plus), suit leurs boutons et
  leurs axes, applique une zone morte aux sticks et relâche ce qui restait pressé quand une manette
  est débranchée. `Input::isGamepadButtonDown`, `wasGamepadButtonPressed` et `gamepadAxis` les
  donnent au jeu.
- **Éditeur** : les interfaces s'éditent dans l'écran **2D**, qui montre les canevas visibles de la
  scène **rendus pour de vrai** (polices, images, coins arrondis, thèmes) par le renderer, dans le
  **cadre de ce que montre le jeu** : pour une scène 2D, la boîte de sa caméra principale quand
  elle est orthographique (le HUD apparaît au-dessus du niveau, là où le joueur le verra) ; pour
  une scène 3D, qui n'y montre que ses interfaces, le cadre d'une caméra 2D neuve à l'origine,
  entouré comme Godot entoure son viewport. L'écran **3D** d'une scène 3D les dessine par-dessus
  la vue, comme le jeu le fera, sans les modifier ; un bouton de sa barre les masque quand un menu
  plein écran cache la scène (la vue 3D de Godot ne les montre pas, ce que des propositions
  demandent). Les canevas sont placés comme le jeu les
  placerait sur l'image du viewport, puis réduits dans le cadre (`ui::placeDrawList` : échelle et
  décalage des sommets, des rectangles arrondis, des découpes et de la netteté des lettres) ;
  `ToolsOverlay::interfaceFrame` dit où, et le runtime les dessine hors du jeu. Un clic choisit
  d'abord l'élément dessiné (image ou texte) le plus haut, avant les entités du monde ; les
  conteneurs qui ne dessinent rien se choisissent dans l'arbre, pour qu'un panneau étiré sur tout
  l'écran laisse le monde en dessous à portée. Cliquer de nouveau descend à l'élément du dessous,
  le glisser le déplace, ses huit poignées le redimensionnent, ses ancrages sont marqués ; chaque
  geste devient une étape d'annulation sur `offset_min` et `offset_max`, et F cadre l'élément.
- **Popups et menus** : `UiPopup` marque un élément montré par-dessus le reste de son canevas
  tant qu'il est ouvert (son `UiRect` n'est visible que pendant ce temps). Un popup ouvert est
  placé après tout son canevas, donc dessiné au-dessus, servi le premier par le pointeur, et ni
  découpé ni défilé par ce qui l'entoure ; ouvert en un point (`openPopup(scene, popup, at)`), il y
  pose son coin haut gauche en restant dans le canevas. Un **menu** (`kind = menu`) se ferme quand
  un de ses boutons est choisi (avec les menus d'où il a été ouvert), quand le pointeur appuie
  ailleurs — cet appui ne fait rien d'autre — ou sur Échap. Une **modale** reste jusqu'à ce que le
  jeu la ferme et garde le reste de son canevas du pointeur et du clavier sous un voile
  (`veil_color`).
- **Menus contextuels** : `UiContextMenu` nomme un popup menu que le clic droit (`UiInput`
  `secondaryPressed`) ouvre sous le pointeur, sur l'élément ou sur un de ses descendants ;
  `contextTarget()` dit ensuite sur quoi le menu agit, comme la ligne d'une liste.
- **Infobulles** : `UiTooltip` montre une ligne d'aide près du pointeur quand il s'est posé
  `delay` secondes (0,5 par défaut) sur l'élément, même inutilisable ; un élément qui ne fait que
  dessiner, comme un texte posé sur une liste, laisse passer le pointeur vers ce qui est dessous.
  L'apparence (`TooltipStyle` : fond, texte, police, taille, marge, arrondi) est celle de
  l'`UiWorld`, dessinée en pixels par-dessus tous les canevas. Sans police à elle ni police par
  défaut (un jeu n'en a pas), une infobulle prend celle des textes de l'interface qu'elle aide :
  le texte de l'élément, sinon le plus proche autour de lui.
- **Listes déroulantes** : `UiDropdown` est un bouton dont le `UiText` montre l'option choisie
  (`selected`, écrit par l'interface), en retrait du bord gauche, avec une flèche à droite. Cliqué,
  il ouvre la liste de ses options sous lui (au-dessus s'il n'y a pas la place), aussi large que
  lui, dix options au plus que la molette fait défiler ; les flèches, Entrée et Échap y marchent
  aussi. Le choix est signalé par son action (`Ui.WasChanged("difficulty")`).
- **Découpe** : un élément qui découpe ses enfants et qui est sorti de la vue ne leur laisse rien,
  plutôt qu'une découpe vide, qui voudrait dire que rien ne les découpe : les lignes d'une liste
  défilée hors de sa vue n'y débordent plus.
- **Défilement, séparateurs, dépliants** : `UiScroll` dessine une **barre de défilement** tant que
  le contenu dépasse (`scrollbar`, `scrollbar_size`, `scrollbar_color`), dont le pouce se tire et
  dont la piste avance d'une page. `UiSplitter` partage son rectangle entre ses deux premiers
  enfants, côte à côte ou l'un sur l'autre, avec une barre que le pointeur tire (`position`,
  `min_size`). `UiFoldout` montre ou cache l'élément qu'il nomme (`content`), avec une flèche qui dit
  lequel : sections d'un inspecteur, ou branches d'un arbre quand les dépliants s'emboîtent. Ce
  qui est caché ne compte pas dans le contenu d'une zone qui défile, et une zone dont le contenu a
  rétréci revient d'elle-même à sa fin plutôt que de montrer du vide.
- **Listes virtuelles et tableaux** : `UiVirtualList`, dans un `UiScroll`, prend la hauteur de
  `item_count` éléments de `item_size` unités mais n'a que les lignes visibles pour enfants :
  l'interface les place aux éléments qu'elles montrent et écrit dans `first` le premier, qu'un
  script lit pour les remplir. `UiTable` aligne en colonnes les cellules (enfants) de chaque
  `UiTableRow` en dessous de lui ; la ligne `header` redimensionne une colonne quand on tire le bord
  d'une cellule, et trie par une colonne quand on la clique (`sort_column`, `sort_ascending`, une
  flèche la marque) : le script trie ses lignes quand l'action du tableau change.
- **Glisser-déposer** : `UiDragSource` (un type, des données, une étiquette) laisse le pointeur
  emporter un élément, ou ce qui est dessous, une fois qu'il s'est éloigné de 6 pixels de
  l'appui : un clic qui tremble reste un clic, et un bouton emporté n'est pas cliqué. Pendant le
  glisser, l'étiquette (le premier texte de l'élément par défaut) suit le pointeur dans le style
  des infobulles, et la `UiDropTarget` sous lui qui accepte ce type s'éclaire
  (`highlight_color`) ; lâché dessus, `wasDropped(action)` et `dropped()` (source, cible, type,
  données, et l'endroit de la cible où il a été lâché, de (0, 0) en haut à gauche à (1, 1)) le
  disent au jeu, qui décide quoi en faire : ranger l'objet dans la case, l'échanger, le placer
  avant ou après une ligne.
  Échap repose ce qui est porté ; une cible ne prend jamais sa propre source. Un outil annonce un
  glisser venu d'ailleurs par `carryFromOutside`, chaque image où il dure. Comme chez Godot, rien
  ne bouge tout seul : Godot passe par `_get_drag_data`, `_can_drop_data` et `_drop_data` sur ses
  nœuds, Devex par des composants et des actions, comme le reste de son interface.
- **Champs numériques** : `UiNumberField` (valeur, bornes, pas, vitesse de glisser, décimales,
  format) donne à un `UiText` avec un `UiInput` le comportement du `EditorSpinSlider` de Godot, que
  les jeux ont aussi : appuyé puis déplacé de 3 pixels, il suit le pointeur de côté (Maj : dix fois
  plus fin), la valeur gardée à part pour que l'arrondi au pas ne la bloque pas ; cliqué sans
  bouger, il se tape, le nombre seul sélectionné, et se lit quand le champ est quitté (Entrée ou
  un clic ailleurs ; Échap le laisse). Ce qui est tapé peut être une somme (`2*3+1`, parenthèses,
  virgule ou point) et reprendre le texte du format (`90°`) ; un texte inchangé ne touche pas une
  valeur qui a plus de décimales qu'il n'en montre. Le texte suit la valeur par son format
  (`{} m`), sans les zéros de fin ; un format sans `{}` s'écrit tel quel. `formatNumber` et
  `evaluateNumber` sont publics.
- **Sélecteur de couleur** : `UiColorPicker` choisit une couleur linéaire, comme celles des images,
  dans un carré de saturation et de luminosité, une barre des teintes à droite et une barre
  d'opacité dessous, qu'on tire chacun jusqu'au relâchement. Le carré répartit la couleur telle
  que l'œil la voit (sRGB), dessiné en 8 × 8 cellules dont les coins sont justes, pour que le
  mélange linéaire du GPU entre eux reste proche ; un gris garde sa teinte, un noir sa saturation,
  et une couleur plus forte que le blanc son intensité. `devex/ui/Color.hpp` convertit entre
  linéaire et sRGB, TSV et hexadécimal.
- **Divers** : `UiWorld::held()` dit quel contrôle le pointeur tient (curseur, nombre, sélecteur) ;
  une `UiDropdown` sans option choisie montre son `placeholder`.
- **Sprites dans les images** : la texture d'une `UiImage` peut être un asset Sprite, comme
  l'`AtlasTexture` de Godot : l'image montre la partie de la texture que le sprite couvre, et ses
  bords en neuf parts sont des fractions du sprite. Le contexte de dessin résout les sprites avant
  les textures (`DrawContext::sprites`) ; le jeu ne lit comme sprite que ce que sa base d'assets dit
  en être un, pour ne jamais charger une texture comme le mauvais type. `preserve_aspect` garde la
  forme de l'image au milieu de son rectangle, pour les icônes et les aperçus.
- **Graphes** : `UiPlot` dessine une suite de valeurs entre deux bornes, en ligne (un quad fin par
  segment), en barres ou en barres en miroir (la forme d'onde d'un son), avec un repère vertical
  (la tête de lecture) ; les valeurs au-delà des bornes restent au bord. Les jeux s'en servent pour
  une jauge au fil du temps ; l'éditeur pour les sons, les courbes et les mesures. Chaque valeur
  peut avoir sa couleur (`colors`), une seconde série se dessine derrière la première dans la
  couleur de chaque valeur multipliée par `back_color` (l'attente derrière le travail), des lignes
  de repère traversent le graphe à des valeurs données (`guides`), et une valeur peut être éclairée
  de bas en haut (`highlighted`). `UiWorld::plotValueAt` (`Ui.PlotValueAt` en C#) dit quelle valeur
  est sous le pointeur : la barre, ou le point de la ligne le plus proche. En marques (`marks`),
  chaque valeur est une place le long du tracé où se dessine un losange : les clés d'une piste.
- **Zones de texte** : `UiTextArea` rend le texte de son entité éditable sur beaucoup de lignes,
  sans retour à la ligne, en défilant dans les deux sens : notes, console, code d'un outil. Seules
  les lignes en vue sont placées et dessinées ; le monde garde les débuts de lignes, le curseur,
  la sélection et l'annulation de chaque zone. Un outil lui donne les couleurs de morceaux du
  texte, ce qui se dessine derrière les lettres et des lignes marquées
  (`UiWorld::setTextSpans`, `setTextHighlights`, `setTextMarks`).
- **Traits** : `UiLine` dessine une ligne brisée entre des points, en unités depuis le coin haut
  gauche de son élément qu'elle peut quitter, avec une pointe de flèche au bout ou au milieu et la
  fermeture sur le premier point : les liens d'un graphe ou d'un arbre de compétences, un tracé sur
  une carte, le contour d'une zone.
- **Double clic** : deux clics sur le même bouton à moins de 0,4 seconde ; le second compte aussi
  comme un clic (`wasDoubleClicked`). `startEditing` donne le clavier à un champ, son texte
  sélectionné, comme un formulaire à son premier champ.
- **Jeu** : `SystemContext::ui` et `Application::ui()` donnent l'`UiWorld` ; en C#, la classe `Ui`
  offre `WasClicked(action)`, `WasClicked(entity)`, `WasDoubleClicked(action)`,
  `WasDoubleClicked(entity)`, `WasChanged(action)`, `WasSubmitted(action)`, `WasCancelled()`,
  `OpenPopup(entity)`, `OpenPopup(entity, at)`, `ClosePopup`, `IsPopupOpen`, `ContextTarget`, `PlotValueAt`,
  `WasDropped(action)`, `WasDropped(entity)`, `Dropped` (un `UiDrop` : source, cible, type,
  données, endroit), `Carried`, `Hovered`, `Focused`, `EditedField` et `PointerOverInterface`, que le jeu lit
  avant d'agir sur un clic qui lui serait destiné. Les touches restent visibles des systèmes du
  jeu pendant qu'un champ est édité, comme dans Unity et Godot : un système qui répond à une
  touche seule vérifie `ui->isEditing()` (ou `Ui.EditedField`) pour ne pas réagir aux lettres
  tapées.
- **Bac à sable** : la scène `sandbox` ouvre sur un menu principal (Jouer, Réglages, Quitter), avec
  un menu de pause appelé par Échap, avec un sac de six cases dont on glisse les objets d'une case
  à l'autre (un objet lâché sur une case occupée échange sa place), et un HUD qui montre le score
  et le temps. L'écran des
  réglages montre un champ de nom, un mot de passe, un curseur de volume dont l'étiquette est liée
  à sa valeur, une case « plein écran », une liste déroulante de la difficulté (gardée dans les
  réglages du joueur), des infobulles sur ces trois contrôles, et un panneau d'aide en neuf parts
  (`textures/panel.png`, 64 pixels) qui contient une liste de texte riche défilant à la molette.
  Tout suit le thème `ui/sandbox.dvxtheme`, et `code/Menu.cs` lit les actions. Un clic sur
  l'interface ne capture plus la souris, et les touches du bac à sable (N, C, Tab, Échap) se
  taisent pendant la saisie.

### Gameplay

- **Modèle** : les données du jeu sont des **composants** réfléchis (sauvegardés, éditables dans
  l'inspecteur), sa logique des **systèmes** : des fonctions `void (SystemContext&)` qui
  parcourent la scène. Tout l'état vit dans la scène, si bien que la copie jouée et le
  rechargement ne perdent rien ; un système ne garde pas d'état dans des variables.
- **Module de jeu** : une bibliothèque partagée, écrite en C++ contre `devex/runtime/Game.hpp`, dont
  le point d'entrée `DEVEX_GAME_MODULE(game)` enregistre composants et systèmes. Il exporte aussi
  la version de l'API (`gameApiVersion`) : un module compilé pour une autre version est refusé.
- **Systèmes** : trois phases, `Start` (au lancement du jeu : Play dans l'éditeur, démarrage du
  lecteur), `FixedUpdate` (au pas fixe) et `Update` (à chaque frame). Dans une phase, les systèmes
  s'exécutent par `order` croissant puis dans l'ordre d'enregistrement, après les fonctions
  virtuelles de l'`Application`. `SystemContext` donne la scène, les entrées, la fenêtre, les
  assets, la physique, l'audio, la durée du pas ou de la frame ; `quitRequested` termine le jeu (arrête Play
  dans l'éditeur). Chaque pas `FixedUpdate` est suivi d'un pas de physique.
- **Projet** : le dossier `code/` d'un projet est un projet CMake (`find_package(Devex CONFIG)`,
  `devex_add_game_module(SOURCES …)`) ; *Code > Create game code* en crée un avec un composant
  et un système d'exemple. `Devex_DIR` désigne `cmake/` du dossier de build du moteur, où
  `DevexConfig.cmake` décrit `Devex::Engine` (bibliothèque, en-têtes, GLM, définitions) et
  impose la configuration du moteur. Le module est produit dans
  `.devex/code/<configuration>/builds/<UUID>/bin/Game.dll` ; `active-build.txt` désigne la
  dernière compilation réussie. Les anciens caches directement dans `<configuration>/bin`
  restent détectés et sont reconstruits à l'ouverture.
- **Compilation par l'éditeur** : l'éditeur surveille `code/` (toutes les 500 ms) et, 300 ms
  après la dernière modification, compile en arrière-plan : un script lance `vcvars64` trouvé
  par vswhere (sauf si l'environnement a déjà le compilateur), configure le dossier de build si
  besoin, puis `cmake --build`. Erreurs et avertissements du compilateur vont dans la console ;
  *Code > Build game code* (Ctrl+B) relance une compilation, et la barre de menus indique
  l'état (compilation, prêt, échec avec la première erreur en infobulle).
- **Mise à jour du moteur** : le cache mémorise l'API, le paquet CMake du moteur et sa bibliothèque.
  Un moteur différent ou reconstruit déclenche une recompilation même si les sources du jeu n'ont
  pas changé. Le nouveau cache évite les fichiers de symboles anciens ou verrouillés ; après une
  erreur `LNK1201`, une seule nouvelle tentative est faite dans un autre cache. Le pointeur vers
  le module et son empreinte ne sont publiés, atomiquement, qu'après réussite. L'accueil affiche
  *Code update required* et propose *Update & Edit* ; Run y attend la mise à jour, et Play reste
  indisponible tant que le code n'est pas prêt. Les sources et les données des scènes sont conservées.
- **Chargement et rechargement** : le runtime charge le module d'un projet à son ouverture, avant
  la première scène, depuis une **copie** (`.devex/code/modules`) pour que l'original puisse être
  recompilé. Quand une nouvelle build apparaît (compilée par l'éditeur ou par un IDE), il
  **libère** chaque scène (édition et copie jouée) : les composants des types du module y sont
  conservés en texte et les pools créés par son code détruits (ceux des types du moteur sont
  recréés aussitôt) ; puis il décharge le module, charge la nouvelle build et **restaure** les
  composants, champs ajoutés ou retirés compris. Une partie en cours continue. Le cache des
  préfabs est vidé juste avant le déchargement : les instances telles que leur préfab les fait,
  qu'il garde en scènes pour l'inspecteur et l'écriture des scènes, contiennent elles aussi des
  pools du module, et les détruire après appelait du code déchargé (l'éditeur plantait en
  fermant un projet après avoir montré une instance d'un préfab à composant du jeu).
- **Lecteur** : `devex-player Projet.dvxproj` ouvre la scène de démarrage du projet (*Set as
  startup scene* dans le panneau Assets, sinon la première scène) avec son module de jeu, sans
  éditeur ; il compile d'abord le code s'il est périmé (voir *Journal et plantages*).
- **Limites** : un plantage du code du jeu arrête l'éditeur ; les variables globales d'un module
  sont perdues au rechargement ; la compilation du code ne fonctionne que sous Windows pour
  l'instant.

### Gameplay en C#

- **Deux langages, un seul moteur** : un projet peut être écrit en C++, en C#, ou dans les deux à
  la fois. Le C++ n'a rien perdu : les composants et systèmes d'un module de jeu fonctionnent comme
  avant, et le C# s'ajoute à côté. Dans une frame, les phases s'exécutent d'abord pour le module
  C++, ensuite pour le code C#.
- **Modèle** : une classe qui dérive de `Devex.Component` est un composant du moteur. Ses champs
  publics sont sauvegardés dans les scènes et édités dans l'inspecteur ; `Start`, `Update(delta)`
  et `FixedUpdate(delta)` sont ses comportements. Un **système** est une méthode statique marquée
  `[GameSystem(SystemPhase.Update)]`, qui prend la scène (ou rien) et voit toutes les entités ;
  les systèmes d'une phase tournent après les composants de cette phase, par `Order` croissant.
- **Le moteur possède les données** : chaque composant C# devient un *type décrit à l'exécution*
  (`scene::DynamicComponentLayout`) — nom, champs, décalages calculés comme ceux d'un composant
  C++. Les valeurs vivent donc dans la scène, pas dans l'objet C#, si bien que les scènes, les
  préfabs, l'annulation, l'inspecteur et la copie jouée les traitent exactement comme les
  composants C++. Au **début de chaque phase**, le runtime copie les champs de tous les composants
  dans leurs objets C# (délégués compilés une fois par champ, arbres d'expression) ; le code du jeu
  change ensuite librement ses objets et ceux des autres composants (`entity.Get<Door>().Open =
  true`) ; à la **fin de la phase**, tout est recopié dans la scène. Un composant neuf part des
  valeurs que la classe C# donne à ses champs (`public float Speed = 1.0f;`) : le moteur les
  demande au runtime quand il ajoute le composant, pas quand il le copie.
- **Objets C#** : un par composant, créé quand la phase le rencontre et gardé tant qu'il existe ;
  ses champs non enregistrés (privés, `[Hidden]`) durent donc d'une frame à l'autre. Une nouvelle
  partie (Play, changement de scène) les recrée tous. `Start` est appelé une fois, avant tout le
  reste pour ce composant.
- **Types de champs** : `bool`, `int`, `uint`, `float`, `string`, `Vec2`, `Vec3`, `Vec4`, `Quat`,
  `Uuid`, `AssetId`, `Entity` (enregistré comme référence d'entité) et les énumérations à valeurs
  `int`, ainsi que des `List<T>` de ces types sauf `bool`. Un champ public d'un autre type est
  signalé et n'est pas enregistré. Les attributs `[Angle]` (degrés dans l'inspecteur,
  radians dans le code), `[Color]`, `[PhysicsLayer]`, `[AssetType("mesh")]` et `[Hidden]` donnent
  les mêmes indications que la réflexion C++. `WalkSpeed` est enregistré `walk_speed`, comme un
  champ C++.
- **API** : `Entity` (nom, vie, `Transform` par référence, hiérarchie, destruction, `Get`, `Has`,
  `Add`, `Remove`), `Scene` (dont `Scene.Current`), `Input` (clavier par position physique,
  souris, capture), `Time` (`Delta`, `Elapsed`, `Frame`), `Screen`, `Log`, `Game` (`Quit`,
  `LoadScene`), `Assets.Find("res://...")`, `Prefabs.Instantiate`, `Physics` (`Raycast`,
  `SphereCast`, `OverlapSphere`, forces, couples et impulsions, `Contacts`), `Audio` (sources,
  lectures ponctuelles 2D ou spatialisées, volumes des groupes), `Animation` (lecture des clips,
  fondu, pause, position dans le clip).
- **Composants C++ vus du C#** : ceux du moteur comme ceux du module C++ du jeu sont atteints par
  des **vues** générées de leur réflexion : `ref var light = ref ...` n'est pas nécessaire, la vue
  est une `ref struct` sur la mémoire du composant dont les propriétés lisent et écrivent les
  champs sur place (`entity.Get<PointLight>().Intensity = 900;`), chaînes, entités et listes
  compris (`NativeList<T>`, `NativeStringList`, `NativeEntityList`). Une vue ne se garde pas au-delà
  de l'appel : c'est une `ref struct`, le compilateur l'interdit dans un champ. `Get`, `Has`, `Add`
  et `Remove` servent aux deux sortes de composants, choisies par les contraintes génériques.
  Les vues du moteur sont générées **au build du moteur** par `devex-bindgen` et compilées dans
  `Devex.Managed` ; celles du jeu **par l'éditeur** à chaque chargement du module C++, dans
  `.devex/code/csharp/generated`, que le projet C# compile, et le C# est alors recompilé. Chaque vue
  porte l'empreinte de la disposition de son type (nom, taille, champs, décalages), comparée à
  celle du moteur qui tourne au premier accès : une vue périmée lève une exception au lieu
  d'écrire au mauvais endroit.
- **Collisions** : juste avant `Update`, le runtime appelle `OnCollisionEnter`,
  `OnCollisionExit`, `OnTriggerEnter` et `OnTriggerExit(Entity other)` sur les composants C# des
  deux entités d'un contact (l'entité du corps : celle du `RigidBody` ou du collider), pour les
  contacts des pas de physique de la frame. `Physics.Contacts` les donne tous aux systèmes.
- **Hébergement de .NET** : le moteur charge `hostfxr` à l'exécution et démarre `Devex.Managed.dll`
  (à côté de l'exécutable, dans `bin/managed`), sans dépendance de compilation vers .NET. Les
  tables de fonctions sont échangées une fois (`[UnmanagedCallersOnly]` côté C#, pointeurs de
  fonctions côté moteur) : aucun marshalling, les chaînes passent en UTF-8. Sans .NET installé, le
  moteur démarre normalement et signale simplement que le C# est indisponible.
- **Compilation** : le dossier `code/` d'un projet accueille les `.cs` ; l'éditeur écrit lui-même
  le `Game.csproj` (assembly `Game.Scripts`, référence à `Devex.Managed`) et lance `dotnet build`
  en arrière-plan 300 ms après la dernière modification, comme pour le C++. Les erreurs vont dans
  la console et l'état apparaît dans la barre de menus.
- **Rechargement à chaud** : l'assembly du jeu est chargée dans un `AssemblyLoadContext`
  *collectible*, depuis une **copie** (dossier temporaire par processus et par chargement, avec ses
  symboles), pour que le SDK puisse réécrire le fichier et que les débogueurs trouvent le `.pdb`.
  À chaque nouvelle build, les composants sont conservés en texte dans les scènes, les types
  désenregistrés, le contexte déchargé, puis tout est restauré — même mécanique que le C++, partie
  en cours comprise. Les **champs non enregistrés** des objets C# sont conservés par nom quand
  leur type ne vient pas de l'assembly du jeu (nombres, `Vec3`, `List<Entity>`...), et `Start`
  n'est pas rappelé : une partie continue sans à-coup. Les champs statiques sont perdus.
- **Erreurs** : une exception d'un composant ou d'un système est attrapée et écrite une fois avec
  sa pile, fichier et ligne compris ; ses répétitions sont comptées (un message toutes les 100).
  Les autres composants continuent.
- **Débogage** : *Project > C# Debugging...* donne le numéro du processus et la marche à suivre
  pour s'attacher depuis Visual Studio, Rider ou VS Code (code .NET) ; les points d'arrêt suivent
  le code à chaque rechargement. Avec *Wait for a debugger when Play starts*, Play attend qu'un
  débogueur soit attaché avant de lancer le jeu (Stop annule).
- **Fichiers de code dans l'éditeur** : le panneau FileSystem montre le dossier `code/` (les
  dossiers de build sont masqués) ; cliquer un fichier ouvre le panneau **Text Editor**, à
  onglets, ancrable ou flottant dans la fenêtre principale. Police mono, sélection, copier-coller,
  annulation/rétablissement du champ actif, position ligne/colonne, **Ctrl+S**, Save All,
  Reload et ouverture dans l'IDE externe. Les scripts enregistrés sont recompilés automatiquement.
  **Editor > Panels > Text Editor** rouvre le panneau ; le masquer conserve les fichiers ouverts.
  **Ctrl+O** ouvre un fichier texte et **Ctrl+W** ferme l'onglet lorsque ce panneau a le focus.
- **Écrans principaux** : le centre de la fenêtre montre un **écran** à la fois, choisi au milieu
  de la barre de menus comme dans Godot : **2D** et **3D** (le viewport, vu de face ou en
  perspective) et **Script** (l'éditeur de texte) partagent le nœud central du dock, sans barre
  d'onglets — l'écran choisi est le seul ouvert, si bien qu'il remplit le centre. **Ctrl+F1**,
  **Ctrl+F2** et **Ctrl+F3** les appellent. 2D et 3D ont chacun leur vue de la scène (centre et
  zoom en 2D, pivot, orientation et distance en 3D), gardées par onglet dans
  `.devex/editor.dvx`. L'éditeur suit ce qu'on ouvre : un fichier montre Script ; une scène, un
  changement d'onglet, F5 ou un changement de type montrent l'écran du type de la scène. Les
  panneaux autour (hiérarchie, inspecteur, FileSystem, sortie) ne bougent pas d'un écran à
  l'autre.
- **Coloration syntaxique** : `InputTextMultiline` ne colore pas son texte. Le champ est donc rendu
  avec une couleur de texte transparente, et le panneau **dessine lui-même** les jetons colorés et
  le curseur par-dessus, en mesurant les positions avec la police du champ ; la sélection reste
  celle d'ImGui. Le champ a la taille de tout le fichier et c'est le panneau qui défile, si bien
  que les marges et les marqueurs se placent sans suivre un défilement interne. Seules les lignes
  visibles sont analysées, à partir d'un index des débuts de ligne reconstruit à chaque édition.
  L'analyseur (`CodeHighlight`) reconnaît C++, C#, CMake, JSON et le format texte `.dvx*`, avec
  mots-clés, types, chaînes, nombres, directives et commentaires, y compris les blocs `/* */` qui
  traversent les lignes. Les couleurs viennent du thème et suivent le mode clair ou sombre.
- **Écran Script** : à gauche du texte, la liste des fichiers de code du projet (filtrable, un clic
  les ouvre) et, en dessous, ce que le fichier ouvert déclare — types, fonctions, ou sections pour
  un fichier `.dvx*` — qui amène à la ligne d'un clic. La lecture des symboles se fait par la forme
  des lignes (mots-clés, parenthèses, fins de ligne) : elle suffit pour naviguer et se trompe sur
  les mises en forme inhabituelles.
- **Confort d'édition** : marge des numéros de ligne, surlignage de la ligne courante, **Ctrl+F**
  (recherche, compte des occurrences, sensibilité à la casse, F3 pour la suivante), **Ctrl+H**
  (remplacement, un par un ou tous), **Ctrl+G** (aller à la ligne), **Ctrl+/** (puis **Ctrl+K**, commenter ou
  décommenter les lignes de la sélection), indentation conservée à la nouvelle ligne (et augmentée
  après `{`, `(` ou `:`), **Tab** qui insère quatre espaces, et retrait des espaces en fin de ligne
  à l'enregistrement. Comme ImGui possède les caractères pendant l'édition, ces changements passent
  par une file d'éditions appliquées dans le rappel du champ.
- **Autocomplétion** : à partir de deux caractères, une liste propose les mots-clés du langage, les
  **noms que le moteur connaît** (composants et champs de la réflexion, types de l'API C# ou C++) et
  les identifiants déjà écrits dans le fichier, chacun avec son origine. Les flèches choisissent,
  **Tab** ou **Entrée** insèrent, **Échap** ferme, et un clic prend l'entrée. Le popup s'approprie
  ces touches le temps qu'il est ouvert, pour ne pas déplacer le curseur en même temps. Il n'y a
  pas d'analyse sémantique : c'est une aide à la frappe, pas un service de langage.
- **Erreurs de compilation** : les sorties des compilateurs C++ et C# sont analysées
  (`file(ligne,colonne): error CODE: message`, ainsi que la forme `fichier:ligne:colonne:` de
  clang) et remontées à l'éditeur, qui marque la ligne dans la marge, souligne le code fautif et
  montre le message au survol. Les compilateurs sont lancés avec `VSLANG=1033` et
  `DOTNET_CLI_UI_LANGUAGE=en` : sans cela, une installation localisée écrit ses diagnostics dans la
  langue du système et ni la console ni la marge ne les reconnaissent.
- **Racine du projet** : `res://` représente le dossier contenant le `.dvxproj`, sans dossier
  physique `res`. FileSystem regroupe le fichier de projet, `assets/` et `code/` sous cette racine.
  Un nouveau projet crée `assets/` et `code/` ; ce dernier reste vide jusqu'à la création du code.
  Le dossier seul ne déclenche aucune compilation. Le C++ utilise `code/CMakeLists.txt` et le C#
  les `.cs` de `code/` et de ses sous-dossiers ; les fichiers en dehors de `code/` ne participent
  pas automatiquement au gameplay. Les anciens projets sans ce dossier restent utilisables.
- **Édition des fichiers Devex** : le menu contextuel des assets propose **Edit as Text** et
  **Edit Import Metadata** (`.dvxmeta`) ; le fichier `.dvxproj` apparaît aussi dans FileSystem.
  Double-cliquer une scène conserve son ouverture dans le viewport ; un `.dvxmat` s'ouvre en
  texte. Les fichiers UTF-8 jusqu'à 2 Mio sont acceptés ; BOM et fins de ligne LF/CRLF sont
  conservés. La sauvegarde remplace le fichier atomiquement et refuse d'écraser une version
  modifiée sur disque. Fermer/recharger un fichier modifié, changer de projet ou quitter demande
  Save / Don't Save / Cancel. Une scène enregistrée en texte est validée puis actualisée dans
  son onglet ; ses modifications graphiques non enregistrées et le mode Play bloquent cette
  sauvegarde. Les réglages du projet sont validés puis relus sans réécrire le texte saisi.
  Cette première version ne propose ni coloration syntaxique, ni autocomplétion.
- **Créer un script** : *Add Component > New Script…* demande un nom et le langage (C# par défaut),
  écrit le fichier dans `code/`, l'ouvre dans Text Editor et, dès que la compilation aboutit, ajoute le
  composant à l'entité sélectionnée (une commande annulable). Un composant C++ créé ainsi doit
  encore être enregistré à la main dans le module (`game.component<T>();`).
- **Jeux exportés** : un projet qui contient du C# emporte son runtime .NET. L'export publie
  `managed/` en autonome (`dotnet publish --self-contained`, environ 80 Mo) avec
  `Devex.Managed.dll` et `Game.Scripts.dll` ; le moteur préfère alors le `hostfxr` livré à côté du
  jeu et démarre par ligne de commande (`hostfxr_initialize_for_dotnet_command_line`). Rien n'est à
  installer sur la machine du joueur ; un jeu en C++ seul n'emporte rien de tout cela. Le C# de
  l'export est compilé à part (`.devex/export/csharp`) contre le `Devex.Managed` de la build du
  moteur choisie, avec des vues des composants C++ générées par le `devex-bindgen` de cette build
  à partir du module compilé pour elle : les dispositions diffèrent entre Debug et Release
  (`std::string`, `std::vector` avec MSVC).
- **Un seul .NET par processus** : le runtime .NET ne se décharge pas ; l'hôte démarre une fois et
  chaque `ManagedGame` s'y branche avec ses tables de fonctions, versionnées pour qu'un
  `Devex.Managed.dll` d'une autre build soit refusé.
- **Limites** : un composant qui lève une exception finit sa phase dans un état partiel ; pas de
  listes de structures ni de dictionnaires dans les champs ; les vues C++ ne connaissent que les
  champs réfléchis ; le changement de phase copie tous les champs de tous les composants C# (à
  surveiller pour des milliers de composants) ; la compilation demande le **SDK .NET 10** et ne
  tourne, comme le C++, que sous Windows. `Devex.Environment` (le composant `Environment`) masque
  `System.Environment` dans le code qui importe les deux espaces de noms.

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
- **Dépendances des en-têtes avec un MSVC localisé** : Ninja lit les en-têtes inclus dans les
  notes `/showIncludes`, reconnues par un préfixe. Sans le pack de langue anglais, ce préfixe est
  traduit (« Remarque : inclusion du fichier : » avec des espaces insécables) dans la page de
  code de la console, que `slangc`, CMake ou vcpkg passent en UTF-8 en cours de build ; et CMake
  n'écrit pas ses règles Ninja avec un préfixe qui n'est pas de l'UTF-8 valide. Sans correction,
  modifier un en-tête ne recompilait rien. `cmake/DevexShowIncludes.cmake` détecte ce cas et fait
  passer le compilateur par un petit lanceur (`cmake/tools/ShowIncludesLauncher.cpp`, compilé à
  la configuration) qui réécrit ces notes avec le préfixe anglais.

### Intégration continue

- **GitHub Actions** (`.github/workflows/ci.yml`) : à chaque push sur `main` ou `ci`, à chaque pull
  request, et à la demande. Deux jobs en parallèle sur `windows-2025`, `x64-debug` et
  `x64-release`, avec les presets du dépôt, Visual Studio 2026 (MSVC 19.51), le SDK .NET 10 pour
  le C#, `slangc` du SDK Vulkan 1.4.350 et Ninja, que l'image ne fournit pas (téléchargé des
  versions publiées). Un push plus récent sur la même branche annule l'exécution en cours.
- **Exigences** : les avertissements sont des erreurs (`DEVEX_WARNINGS_AS_ERRORS=ON`), et tous les
  tests passent, sauf ceux marqués `[gpu]` : les machines de GitHub n'ont pas de GPU. Les tags
  Catch2 deviennent des labels CTest (`ADD_TAGS_AS_LABELS`), et la CI lance
  `ctest --label-exclude gpu` ; les tests `[gpu]` (rendu, éditeur) restent à lancer sur les
  machines des développeurs.
- **vcpkg** : un clone **complet** de vcpkg, dont l'historique contient les ports fixés par la
  `builtin-baseline` (un clone partiel lui faisait chercher ces versions pendant l'installation,
  ce qui échouait sur le réseau de la CI). Les paquets construits sont gardés dans le cache de
  GitHub, par configuration et par empreinte de `vcpkg.json`, et sauvés dès la configuration :
  la première exécution construit les dépendances (une dizaine de minutes), les suivantes les
  reprennent ; une exécution complète dure environ un quart d'heure.
- **Échecs lisibles par tous** : les journaux d'une exécution ne se lisent qu'avec des droits sur
  le dépôt. Chaque étape garde donc ce qu'elle écrit dans un fichier, et une exécution qui échoue
  publie en **annotations d'erreur** la fin des journaux de l'étape en faute : la configuration
  avec vcpkg et le port qu'il n'a pas pu construire, les erreurs du build, ou celles des tests.
  Le résumé de l'exécution les montre, et l'API publique de GitHub les rend. Les journaux
  complets restent en artefacts.
- **Ce qu'elle a déjà trouvé** : la liste des projets cherchait un projet par son chemin tel
  qu'écrit alors qu'elle le range sous sa forme canonique ; un dossier au nom court de Windows,
  comme le dossier temporaire des machines de GitHub (`RUNNER~1`), le manquait.

### Dépendances prévues (vcpkg)

| Bibliothèque          | Usage                | Jalon    |
| --------------------- | -------------------- | -------- |
| SDL3 (feature `vulkan`) | fenêtre, entrées   | 1 ✅     |
| GLM (header-only)     | maths                | 1 ✅     |
| volk (+ vulkan-headers) | Vulkan             | 2 ✅     |
| VMA                   | mémoire GPU          | 3 ✅     |
| fastgltf              | import glTF          | 3 ✅     |
| Dear ImGui (docking, SDL3) | outils, éditeur | 5 ✅     |
| basisu (encodeurs BC7, BC5) | compression des textures | 6 ✅ |
| stb (stb_image)       | décodage des images  | 6 ✅     |
| miniaudio             | sortie, mixage et spatialisation audio | 16 ✅ |
| stb (stb_vorbis)      | décodage Ogg Vorbis  | 16 ✅    |
| efsw                  | surveillance des fichiers | 6 ✅ |
| mikktspace            | tangentes            | 7 ✅     |
| joltphysics           | physique             | 11 ✅    |
| box2d                 | physique 2D          | 37 ✅    |
| recastnavigation      | navigation           | 39 ✅    |
| zstd                  | paquets de jeux      | 13 ✅    |
| FreeType (`imgui[freetype]`) | rendu des polices de l'éditeur | 10 ✅ |
| plutosvg              | icônes SVG de l'éditeur | 10 ✅  |
| Catch2                | tests (feature `tests`) | 0 ✅  |

Hors vcpkg :

- **.NET 10** : le SDK compile le code C# des projets et l'assembly `Devex.Managed` du moteur
  (`dotnet` cherché à la configuration, facultatif : sans lui, le moteur se construit et tourne
  sans C#) ; le runtime est chargé à l'exécution par `hostfxr`, jamais lié au moteur ;
- **Slang** est fourni par le SDK Vulkan (`C:\VulkanSDK\1.4.350.0`) ;
- **ufbx** v0.23.1 n'existe pas dans vcpkg : ses deux fichiers (`ufbx.c`, `ufbx.h`) et sa licence
  (MIT ou domaine public) sont versionnés dans `third_party/ufbx` ; le projet CMake active le C pour
  lui. Les fichiers de test FBX et OBJ (`tests/data/fbx`) sont faits par Blender, avec le script
  qui les accompagne ;
- les **polices** Noto Sans (commit `53486ab` de `notofonts.github.io`) et JetBrains Mono (v2.304),
  sous SIL OFL 1.1, et les **icônes** Lucide 1.47.0 (ISC) sont versionnées dans `third_party/fonts`
  et `third_party/lucide` avec leurs licences, copiées avec elles dans `bin/resources`.

CMake trouve vcpkg via la variable `VCPKG_ROOT`, sinon via l'exécutable `vcpkg` du
`PATH` (voir `cmake/DevexVcpkg.cmake`). La version des ports est figée par
`builtin-baseline` dans `vcpkg.json`.

## Jalons

Chaque jalon se termine par une démo observable dans le projet `samples/sandbox` et des tests.

0. ✅ **Fondations** — `vcpkg.json`, Catch2, `Core` (log, assert, `Result`), dépôt Git.
1. ✅ **Fenêtre** — `Platform` sur SDL3 : fenêtre redimensionnable, événements clavier et
   souris ; `Runtime` : `Application` et boucle à pas fixe ; `Math` sur GLM.
2. ✅ **Vulkan** — instance 1.4, validation, choix du GPU, device, swapchain, couleur de
   fond, redimensionnement en direct.
3. ✅ **Premier maillage** — shaders Slang, buffers VMA, caméra (Y-up, reverse-Z),
   primitives procédurales et import glTF.
4. ✅ **Scène** — entités à UUID, sparse sets, hiérarchie, réflexion, format texte
   commun, lecture et écriture `.dvxscene`, rendu automatique de la scène.
5. ✅ **Outils** — overlay ImGui docking : hiérarchie, inspecteur par réflexion,
   statistiques, console, annulation par commandes.
6. ✅ **Assets et textures** — projet `.dvxproj`, `.dvxmeta`, cache d'artefacts binaires, imports
   en arrière-plan sur un pool de jobs, réimport à chaud, textures BC7/BC5 bindless, matériaux
   (`.dvxmat` et glTF), modèles glTF placés en entités, panneau Assets.
7. ✅ **Rendu PBR** — render graph, forward+ clustered, lumières en unités physiques, ombres en
   cascades, ciel HDR et IBL, exposition automatique, tonemapping AgX, tangentes
   MikkTSpace, énumérations dans la réflexion.
8. ✅ **Éditeur** — mode éditeur du runtime et `devex-editor`, écran d'accueil et projets
   récents, scènes en assets (ouvrir, enregistrer, modifications non enregistrées), viewport
   rendu dans une texture, caméra d'éditeur, sélection sur le GPU et contours, gizmos maison,
   grille et icônes, mode Play sur une copie de la scène avec pause et pas à pas.

9. ✅ **Gameplay en DLL** — moteur en bibliothèque partagée, modules de jeu (composants et
   systèmes), compilation par l'éditeur à chaque modification, rechargement à chaud qui conserve
   les composants, `devex-player`, scène de démarrage, bac à sable devenu projet d'exemple.

10. ✅ **Look de l'éditeur** — thème inspiré de Godot (préréglages, accent, contraste, échelle),
    Noto Sans et JetBrains Mono rendues par FreeType, icônes Lucide colorées, gestionnaire de
    projets, onglets de scènes, barre d'outils du viewport, barre d'état, réglages de l'éditeur,
    barre de titre sombre, interface mélangée en espace d'affichage.

11. ✅ **Physique** — Jolt Physics : corps rigides statiques, cinématiques et dynamiques, colliders
    (primitives, maillages, déclencheurs), corps composés, personnages, couches de collision du
    projet, requêtes, forces et contacts pour le code du jeu, interpolation, formes dans l'éditeur,
    arène jouable dans le bac à sable.

12. ✅ **Préfabs liés** — scènes imbriquées à tous les niveaux, modifications de champs, de noms,
    composants et entités ajoutés, UUID dérivés, instances non résolues conservées, mise à jour
    en direct des instances, marques et Revert dans l'inspecteur, Make Local, Save as Prefab,
    glisser-déposer, instanciation par le code du jeu, préfabs de l'arène.

13. ✅ **Export d'un jeu** — paquet `.dvxpak` (index, compression zstd, mappage mémoire), source
    d'assets abstraite, dépendances suivies depuis les scènes, dossiers toujours inclus, copie du
    moteur et du code du jeu compilé pour le build choisi, icône de l'exécutable, réglages de
    fenêtre du projet, changement de scène pour le code du jeu, fenêtre *Export Game* et
    `devex-editor --export`.

14. ✅ **Gameplay en C#** — .NET hébergé dans le moteur (`hostfxr`, tables de fonctions sans
    marshalling), composants C# devenus des types décrits à l'exécution dont la scène possède les
    données, systèmes `[GameSystem]`, API de base (scène, entités, `Transform`, entrées, temps,
    journal), attributs `[Angle]`, `[Color]`, `[PhysicsLayer]`, `[AssetType]`, `[Hidden]`,
    compilation par le SDK .NET à chaque modification et rechargement à chaud, fichiers de code
    visibles et prévisualisés dans FileSystem avec ouverture dans l'IDE, *Add Component > New
    Script…* en C# ou C++, runtime .NET embarqué dans les jeux exportés, composant `Bobber` et
    système `BobberReport` du bac à sable.

15. ✅ **C# complet** — références d'entités (UUID, suivies dans les préfabs) et listes dans la
    réflexion, l'inspecteur et les fichiers, en C++ comme en C# ; vues C# générées des composants
    du moteur (au build) et du module C++ du jeu (par l'éditeur), vérifiées par empreinte ;
    physique, préfabs, assets, scènes, temps et écran en C# ; `OnCollisionEnter`,
    `OnTriggerEnter`... ; données synchronisées par phase ; état privé conservé au rechargement ;
    erreurs dédoublonnées avec leurs lignes ; fenêtre *C# Debugging* et Play qui attend un
    débogueur ; export qui génère ses vues pour sa build ; cibles, porte et distributeur de
    caisses en C# dans l'arène ; corps en rotation qui ne sont plus reconstruits à chaque pas.

16. ✅ **Audio** — miniaudio et stb_vorbis, import WAV/FLAC/MP3/Ogg Vorbis, clips décodés au
    chargement ou pendant la lecture, `AudioSource` 2D ou spatialisée et `AudioListener` (sinon
    caméra principale), atténuation et Doppler, sources et sons ponctuels en C++ et C#, volumes
    Master et groupes du projet, pause avec Play, aperçu sonore et forme d'onde dans l'inspecteur,
    icônes et distances dans le viewport, clips dans les jeux exportés, sons du lanceur, des cibles
    et de la porte, bourdonnement mobile et ambiance en boucle dans l'arène.

17. ✅ **Animation squelettique** — import des squelettes et des animations glTF en sous-assets du
    modèle, une entité par os, `SkinnedMeshRenderer` et `Animator`, skinning dans le vertex shader
    (couleur, ombres, sélection, picking), fondu croisé entre clips, root motion optionnel vers la
    `Transform` ou le `CharacterController`, API C++ et C#, panneau Animation avec piste temporelle
    et images clés, robot rigué qui patrouille et salue dans l'arène.

18. ✅ **Éditeur de texte** — coloration syntaxique dessinée par-dessus le champ (C++, C#, CMake,
    JSON, `.dvx*`), numéros de ligne et ligne courante, recherche et remplacement, aller à la
    ligne, commentaire par raccourci, indentation automatique, espaces de fin retirés à
    l'enregistrement, autocomplétion des mots-clés et des noms du moteur, et erreurs de
    compilation marquées dans la marge.

19. ✅ **Écrans de l'éditeur** — 2D, 3D et Script au centre de la barre de menus, à la manière de
    Godot : l'écran choisi est seul au centre de la fenêtre, sans onglet, et l'écran Script montre
    la liste des fichiers du projet et le plan du fichier ouvert à côté de l'éditeur de texte.

20. ✅ **Interfaces** — polices cuites en atlas de distances signées, composants `Canvas`,
    `UiRect`, `UiImage`, `UiText`, `UiButton` et `UiLayout`, placement par ancrages et marges puis
    par conteneurs, passe de dessin dédiée après le tonemapping, survol, clic, focus au clavier et
    à la manette, manettes dans `Devex::Platform`, API C++ et C# (`Ui`), écran 2D de l'éditeur pour
    poser les éléments, et menu, réglages, pause et HUD du bac à sable.

21. ✅ **Transparence et post-traitements** — matériaux transparents triés et dessinés après
    l'opaque, prépasse de profondeur, de mouvement et de normales, anticrénelage temporel à la
    place du MSAA, occlusion ambiante en espace écran, bloom, table de couleurs, vignette, grain et
    aberration chromatique, tous réglés sur la caméra.

22. ✅ **Culling et ombres locales** — boîtes englobantes cuites à l'import, tronc de vue sur CPU
    pour la caméra, les cascades d'ombre et chaque vue locale, et ombres des spots et des lumières
    ponctuelles dans un atlas dont les tuiles vont aux lumières les plus importantes.

23. ✅ **Interfaces complètes** — champs de saisie sur une ou plusieurs lignes avec sélection,
    presse-papiers, mot de passe et texte de remplacement, saisie de texte et répétition des
    touches dans `Devex::Platform`, découpe par lot et défilement à la molette, crénage, texte
    riche avec icônes, neuf parts à la taille de la texture, curseurs et cases à cocher, liaison
    de données, thèmes `.dvxtheme` référencés par le canevas, polices étendues au latin d'Europe
    centrale et à la ponctuation courante, et écran de réglages du bac à sable qui montre le tout.

24. ✅ **Fiabilité du lecteur** — journal dans un fichier pour le lecteur, les jeux exportés et
    l'éditeur, rapport de plantage avec pile symbolisée et minidump, symboles des builds Release,
    recompilation du code périmé avant de jouer, et jeux exportés sans fenêtre console, qui
    montrent leur erreur fatale dans une boîte de dialogue.

25. ✅ **Thèmes dans l'éditeur** — thèmes appliqués à la scène éditée hors Play et avant la mise
    en page, valeurs lues une seule fois, champs écrits par un style grisés dans l'inspecteur,
    état du style et ouverture du thème depuis l'inspecteur.

26. ✅ **Intégration continue** — GitHub Actions sur Windows (Visual Studio 2026, SDK Vulkan,
    Ninja, cache des paquets vcpkg), Debug et Release avec le C#, avertissements en erreurs,
    tests sans ceux qui demandent un GPU, erreurs publiées en annotations ; en service sur `main`
    depuis le jalon 39.

27. ✅ **Profileur** — zones nommées sur chaque thread, rassemblées par image, temps GPU de chaque
    passe du render graph par timestamps, zones du jeu en C++ et en C#, mémoire des assets par
    type et pour les plus lourds, et panneau *Profiler* : barres des images, chronologie par
    thread et GPU, tableaux CPU, GPU et mémoire.

28. ✅ **Confort de l'éditeur** — sélection multiple (Ctrl et Maj dans l'arbre, rectangle dans la
    vue lu sur le GPU), clics qui descendent dans les instances de préfab, gizmo et inspecteur sur
    plusieurs entités, copier, couper, coller et dupliquer par le presse-papiers du système avec
    UUID neufs, renommage dans l'arbre, entités masquées dans la vue et gardées par projet,
    matériaux glissés sur les objets.

29. ✅ **Chargement asynchrone** — maillages et textures lus et décodés sur les workers, confiés au
    renderer une fois par frame, copies vers le GPU sans attente dans un budget par frame,
    remplaçants en attendant (rien de dessiné, textures par défaut), rechargement qui garde
    l'ancienne version jusqu'à la nouvelle, scènes chargées en arrière-plan avec progression et
    préchargement en C++ et en C#, état du chargement dans l'éditeur.

30. ✅ **Import FBX** — FBX et OBJ lus par ufbx et convertis au repère du moteur (axes, mètres,
    option d'échelle), mêmes assets qu'un glTF : hiérarchie, maillages à sous-maillages,
    matériaux PBR (cartes de rugosité et de métal assemblées), images intégrées ou voisines,
    squelettes, skinning et animations ; échelle 100 des exports Blender cuite dans la géométrie,
    fichiers voisins copiés avec le modèle, inspecteur des modèles avec leurs réglages d'import.

31. ✅ **Actions d'entrée** — actions bouton, axe et vecteur dans les réglages du projet, liées aux
    touches, boutons de souris, boutons, axes et sticks de manette, zones mortes, contextes
    activables, clavier ignoré pendant la saisie de texte, réaffectation par écoute gardée dans le
    dossier de l'utilisateur, page Input des réglages du projet, API C++ et C#, bac à sable joué
    par actions avec un écran de touches.

32. ✅ **Sauvegardes et réglages du joueur** — sauvegardes d'objets réfléchis en C++ et en C# dans
    des emplacements du dossier du joueur, avec la scène qui joue, restaurée au chargement, et une
    miniature capturée sans l'interface ; version, libellé, date et temps de jeu, sauvegarde
    précédente gardée ; réglages du joueur (volumes, plein écran, synchronisation verticale,
    valeurs du jeu) gardés et appliqués par le moteur ; démonstration dans le bac à sable.

33. ✅ **Tweens et coroutines** — tweens de tout champ numérique, vectoriel, de couleur ou de
    rotation, par code en C++ et en C# ou par le composant `Tweener`, avec délai, boucles,
    aller-retour et séquences ; 22 courbes classiques et courbes dessinées dans l'inspecteur
    (asset `.dvxcurve`) ; coroutines `co_await` en C++ et `async Coroutine` en C#, qui attendent
    le temps, les frames, des conditions, les tweens et les tâches et s'arrêtent avec leur entité
    ou leur composant ; démonstration dans le bac à sable.

34. ✅ **Particules** — émetteurs simulés sur le CPU en parallèle, réglés dans le composant
    `ParticleEmitter` (émission, formes, forces, bruit, courbes sur la vie, collisions avec la
    physique, sous-émetteurs), traînées des particules et composant `TrailRenderer`, rendu en
    billboards, particules étirées, couchées ou dressées et rubans, doux au contact des surfaces,
    éclairés ou non, triés avec les surfaces transparentes ; aperçu dans l'éditeur, API C++ et C#,
    démonstration dans le bac à sable.

35. ✅ **Sprites et caméra 2D** — caméra orthographique, textures découpées en sprites à l'import
    (unique ou grille, pixels par unité, pivot, bords) et filtrées au pixel près, composant
    `SpriteRenderer` (simple, découpé en neuf, répété, retourné, éclairé ou non), couches de tri
    nommées et ordre, animations nommées `.dvxframes` jouées par `SpriteAnimator` ; vue 2D de
    l'éditeur, inspecteurs des textures et des animations ; jeu de plateformes du bac à sable.

36. ✅ **Tuiles** — composant `Tilemap` aux cellules rangées par blocs dans la scène, retournées ou
    non, tilesets `.dvxtileset` (sprite, collision, animation et données par tuile), rendu en lot
    parmi les sprites, peinture dans la vue (pinceau, gomme, rectangle, remplissage, pipette) avec
    un trait par annulation, inspecteur des tilesets, API C++ et C# ; le niveau du jeu de
    plateformes du bac à sable en tuiles.

37. ✅ **Physique 2D** — Box2D 3.1 derrière le module `Physics2D` : corps rigides 2D statiques,
    cinématiques et dynamiques, colliders boîte, cercle, capsule et polygone, déclencheurs,
    collisions à sens unique, collider généré des tuiles, personnage « move and slide » (pentes,
    marches, sol, corniches, plateformes mobiles, poussée), requêtes, forces et contacts en C++ et
    en C#, formes dessinées dans l'éditeur ; le jeu de plateformes du bac à sable simulé.

38. ✅ **Machines à états d'animation** — asset `.dvxanimator` de paramètres, d'états et de
    transitions (conditions, temps de sortie, fondus, « Any State »), arbres de mélange 1D et 2D,
    états qui jouent des animations de sprite, joué par l'`Animator` ; panneau de graphe de nœuds
    avec son annulation, réglages dans l'inspecteur, suivi en direct pendant le jeu ; API C++ et C# ;
    le robot et le chevalier du bac à sable s'en servent.

39. ✅ **Navigation** — Recast & Detour derrière le module `Navigation` : maillage cuit dans
    l'éditeur depuis les colliders statiques (asset `.dvxnavmesh` à côté de la scène, par tuiles
    compressées), agents `NavMeshAgent` en foule avec évitement, obstacles `NavMeshObstacle` qui
    découpent le maillage pendant le jeu, requêtes de chemin, d'échantillonnage et de rayon en C++
    et en C#, maillage, agents et chemins dessinés dans la vue ; le robot et deux drones de l'arène
    marchent par la navigation.

40. ✅ **Début de Devex UI dans l'éditeur** — contrôles de `Devex::Ui` qui manquaient à un
    éditeur : popups, menus et modales, menus contextuels, infobulles, listes déroulantes, barres de
    défilement, double clic, séparateurs, dépliants, listes virtuelles et tableaux, en C++ et en
    C# ; surfaces d'interface du renderer montrées par ImGui ; panneaux de l'éditeur faits
    d'entités, logés dans une fenêtre ImGui ; le gestionnaire de projets porté ; difficulté et
    infobulles dans les réglages du bac à sable.

41. ✅ **FileSystem et Output en Devex UI** — glisser-déposer dans `Devex::Ui` (sources, cibles,
    glissers venus d'ailleurs) en C++ et en C#, et entre ses panneaux et ImGui ; FileSystem en
    arbre virtuel avec ses menus, le clavier, ses fichiers glissés vers la vue et l'inspecteur et
    les entités lâchées sur un dossier qui deviennent des préfabs ; Output dont le texte se choisit
    et se copie ; panneaux à la taille du texte d'ImGui et mélangés comme lui ; fenêtre Create
    Entity et Add Component en palette (composants et préréglages, recherche, catégories, fiche,
    favoris et récents par projet) ; sac du menu de pause du bac à sable.

42. ✅ **Arbre de scène en Devex UI** — liste virtuelle aux guides fins, entités glissées avant,
    dans ou après une autre (ordre des frères annulable), fichiers de FileSystem lâchés dans
    l'arbre, renommage dans la ligne, clavier, menus avec leurs raccourcis, œil, préfab et code au
    bout des lignes ; endroit du dépôt dans une cible en C++ et en C# ; exports explicites de la
    bibliothèque du moteur (`DEVEX_API`), passée sous la limite de Windows.

43. ✅ **Inspecteur en Devex UI** — champs numériques glissés ou tapés (`UiNumberField`, sommes
    comprises) et sélecteur de couleur (`UiColorPicker`) dans `Devex::Ui`, pour les jeux aussi ;
    inspecteur des entités en cartes repliables, vecteurs aux axes colorés, listes déroulantes,
    dépôts d'assets et d'entités, valeurs du préfab, champs grisés par les styles, plusieurs
    entités, un pas d'annulation par édition ; les assets restent en ImGui dans la même fenêtre.

44. ✅ **Inspecteur des assets en Devex UI** — une page par asset (texture, modèle, son, courbe,
    sprite frames, tileset, animator, tout autre asset), par fichier de code et par état ou
    transition de l'Animator, et le peintre de tuiles, sans plus d'ImGui dans l'Inspecteur ;
    réglages d'import appliqués par *Reimport* comme dans Godot ; `UiImage` qui montre un sprite
    et garde ses proportions, `UiPlot` pour les courbes et les ondes, en C++ et en C#.

45. ✅ **Réglages et dialogues en Devex UI** — Editor Settings et Project Settings avec leurs
    sections à gauche et un filtre comme Godot, matrice de collision aux noms penchés, Input Map
    avec sa fenêtre d'événement qui écoute la prochaine touche, fenêtres Export et C# Debugging,
    dialogues New Script, modifications non enregistrées et About ; pièces de formulaire communes
    (`FormUi`) à l'inspecteur et aux fenêtres.

46. ✅ **Statistics et Profiler en Devex UI** — moniteurs à la Godot, une courbe par mesure ;
    Profiler en barres, timeline zoomable et onglets CPU, GPU et Memory ; `UiPlot` avec une
    couleur par valeur, une série derrière, des repères, une valeur éclairée et la valeur sous le
    pointeur, en C++ et en C#.

47. ✅ **Texte mis en cache** — les lettres de chaque texte gardées d'une image à l'autre par
    élément (`ui::TextCache`), replacées seulement quand le texte, son style, sa police ou la
    taille de sa boîte changent ; largeurs mesurées gardées dans l'éditeur.

48. ✅ **Cadre de l'éditeur en Devex UI** — barre de menus, barre d'état, onglets des scènes
    (glisser pour les ranger, croix et bouton du milieu, point des modifications, **+**, molette)
    et barre d'outils de la vue ; menus décrits en données et infobulles dans une couche au-dessus
    de la fenêtre ; même barre pour les outils par-dessus un jeu.

49. ✅ **Animation et Animator en Devex UI** — timeline qui zoome et défile comme celle de Godot,
    graphe des états avec ses nœuds, ses liens et ses menus, paramètres suivis en direct pendant le
    jeu ; `UiLine` (lignes brisées et flèches) et les marques de `UiPlot` dans le moteur, en C++
    et en C#.

50. ✅ **Éditeur de texte en Devex UI** — `UiTextArea` dans le moteur (seules les lignes en vue
    sont placées, annulation qui retient aussi ce que les outils écrivent, Tab et indentation,
    mots, pages, couleurs par morceau) ; écran Script comme celui de Godot : fichiers ouverts à
    gauche, menus File, Edit et Search, recherche, remplacement, aller à la ligne, complétion.

51. ✅ **Ménage avant le dock** — dessins par-dessus la vue (sélection, cadre du jeu, poignées et
    ancres des interfaces, indications) en Devex UI sur un panneau transparent ; widgets ImGui
    sans appelant retirés.

Ensuite, sans ordre figé : CI Linux, le dock en Devex UI.

## Questions ouvertes

À trancher le moment venu, pas avant :

- **Systèmes** : exécution en parallèle, dépendances déclarées entre systèmes, systèmes actifs
  aussi en édition, ressources globales du jeu.
- **C#** : listes de structures et dictionnaires, champs de type composant (`public Door Door;`),
  systèmes C++ appelant du code C#, bouton Debug qui lance l'IDE, `dotnet` embarqué pour les
  machines sans SDK, édition du code dans l'éditeur, copie limitée aux champs changés entre les
  phases, export vers d'autres plateformes du runtime .NET.
- **Isolation du code du jeu** : protéger l'éditeur d'un plantage du module (processus séparé,
  gestion structurée des exceptions).
- **Physique** : articulations (charnières, ressorts), véhicules, ragdolls, matériaux physiques par
  collider, marqueurs de modification pour les grandes scènes, simulation dans l'éditeur (mode
  Simulate), débogage visuel des contacts, pool de jobs de Jolt fusionné avec `core::JobSystem`.
- **Physique 2D, la suite** : articulations (charnière, ressort, distance, souris), chaînes de
  segments pour les contours, polygones concaves découpés, matériaux par forme et par tuile,
  descendre d'une corniche (bas + saut), contacts des personnages avec le décor statique, lancers
  de formes et requêtes par boîte, vitesse horizontale constante sur les pentes, saut à hauteur
  variable, plateformes cinématiques qui poussent les personnages de côté, monde 2D créé
  seulement si la scène en a besoin, simulation dans l'éditeur.
- **Navigation, la suite** : liens hors maillage (sauts, échelles, *NavMeshLink*), zones et coûts
  de passage, plusieurs tailles d'agents (plusieurs surfaces actives à la fois), cuisson pendant le
  jeu et tuiles recuites autour d'un changement, cuisson sur un worker avec sa progression,
  géométrie des maillages rendus et des terrains comme source, obstacles qui ne découpent pas
  (évitement seul), agents portés par les plateformes mobiles, root motion qui conduit l'agent,
  essai d'un chemin dans l'éditeur hors jeu, cuisson automatique à l'enregistrement.
- **Préfabs** : retirer un composant ou une entité du préfab dans une instance, variantes
  explicites (une instance à la racine d'un préfab en fait déjà une), onglet ouvert sur le préfab
  d'une instance avec son contexte, édition des entités d'une instance sur place (Godot *Editable
  Children*), mise à jour des instances pendant le jeu, modifications vers des entités retirées
  gardées au lieu d'être abandonnées.
- **Export** : autres plateformes (Linux, macOS), export incrémental et paquets de mise à jour ou de
  contenu additionnel, signature de l'exécutable et installeur, retrait des bibliothèques qu'un jeu
  n'utilise pas, cuisson des textures par plateforme, export sans build Release du moteur (paquet
  d'un moteur distribué).
- **Audio** : streaming depuis le disque, occlusion, réverbération, effets et routage des groupes,
  budget de voix, plusieurs écouteurs, intégration optionnelle de FMOD/Wwise.
- **Éditeur de texte** : client LSP (clangd, Roslyn) pour une vraie complétion, les diagnostics en
  direct et l'aller à la définition ; repli de code, multi-curseur, sélection par colonnes,
  retour à la ligne automatique, tabulations alignées sur des colonnes plutôt que d'une largeur
  fixe, et recherche dans tout le projet.
- **Machines à états, la suite** : sous-machines et arbres imbriqués, couches et masques d'os,
  transitions interrompues par d'autres, conditions sur la fin d'un état, temps de sortie par
  boucle, arbres 2D directionnels simples, courbes de fondu, aperçu d'un état dans l'éditeur hors
  jeu, états communs à plusieurs contrôleurs (overrides d'Unity), valeurs des paramètres gardées
  dans les sauvegardes.
- **Animation** : couches et masques d'os,
  événements de clip, cinématique inverse, morph targets, pré-skinning en compute (colliders et
  rayons suivant la pose), réutilisation d'un clip entre squelettes différents (retargeting),
  compression des courbes.
- **Sauvegardes, la suite** : écriture sur un worker pour les grosses scènes, compression et
  signature des fichiers des jeux exportés, sauvegardes dans le nuage des plateformes, état en
  cours de la physique et des animations, champs privés marqués à garder, migrations déclarées
  par version, plusieurs miniatures ou une taille choisie, entreprise (`organization`) dans les
  réglages du projet pour le dossier du joueur.
- **Tuiles, la suite** : tuiles automatiques (terrains, règles de voisinage), tuiles tournées d'un
  quart de tour, tampons de plusieurs tuiles et palette de morceaux de carte, grilles isométriques
  et hexagonales, formes de collision par tuile (pentes, demi-tuiles), cartes découpées en morceaux pour le
  culling et l'envoi au GPU gardé d'une image à l'autre, calques de la même carte, tuiles faites de
  préfabs.
- **2D, la suite** : physique 2D, éclairage 2D (lumières 2D,
  normal maps, ombres), découpe libre des sprites dans un éditeur de sprites et atlas regroupés à
  l'import, aimantation au pixel (pixel perfect), ordre par Y pour les vues de dessus, sprites
  écrits dans la profondeur (découpés à l'alpha), événements des animations, animations de
  n'importe quel champ par des clips (comme Unity), aperçus ImGui au pixel près, culling des
  sprites par lots plutôt qu'un à un.
- **Particules, la suite** : simulation sur le GPU pour les très grands nombres, éditeur de
  dégradés de couleur plutôt que deux couleurs, particules faites de maillages, lumières portées
  par les particules, ombres qu'elles projettent, collisions contre la profondeur de l'écran,
  forces de zone (vent, attracteurs), vitesse limitée et orbites, bruit plus riche (curl),
  sous-émetteurs multiples, culling des émetteurs hors de la vue et niveaux de détail, particules
  en mouvement pour l'anticrénelage temporel (vecteurs de mouvement), aperçu de l'éditeur limité à
  la sélection.
- **Tweens et coroutines, la suite** : arrêt par jeton qui passe par les `finally`, coroutines qui
  survivent au rechargement du code, ordre ou remplacement quand deux tweens écrivent le même
  champ (aujourd'hui leur ordre n'est pas défini), temps réel et échelle du temps (ralenti, tweens
  des menus qui jouent pendant une pause du jeu), tweens le long d'un chemin et de textes (compteur
  qui défile), aperçu d'un `Tweener` et d'une courbe hors du jeu, annulation dans l'éditeur de
  courbes, icône propre aux courbes.
- **Entrées, la suite** : plusieurs joueurs sur un même écran (appareils attribués à un joueur),
  souris et molette comme axes (regarder, zoomer), modificateurs (inverser, échelle, courbe),
  combinaisons (Ctrl+S) et appuis longs ou doubles, navigation de l'interface par les actions,
  conflits signalés à la réaffectation, glyphes des boutons selon la manette, vibrations.
- **UI des jeux, la suite** : transitions et animations d'éléments, position de la fenêtre de la
  méthode de saisie sous le curseur dans les jeux, polices de repli pour les écritures non cuites,
  texte bidirectionnel et écritures complexes, sélection d'un mot au double clic, annulation dans
  un champ, édition des thèmes dans l'éditeur, infobulles stylées par le thème du canevas plutôt
  que par l'`UiWorld`, sous-menus ouverts au survol et navigation au clavier dans les menus,
  sélection multiple dans les listes, arbres virtuels tout faits (FileSystem aplatit le sien),
  recherche dans une liste déroulante, aperçu d'un glisser fait d'un élément plutôt que d'une
  étiquette, défilement automatique d'une liste quand on glisse près de son bord, texte en lecture
  seule sélectionnable tout fait (l'Output fait le sien).
- **Portage de l'éditeur sur l'UI du moteur, la suite** : l'inspecteur (champs de propriétés,
  sélecteur de couleur, édition de plusieurs entités, inspecteurs d'assets), puis les autres
  panneaux, le
  dockspace et les menus de la fenêtre en dernier (voir *Une seule interface, deux usages*) ; des
  popups et infobulles qui sortent du panneau (fenêtres à elles, comme chez Godot) ; déplacer et
  renommer des fichiers dans FileSystem (glissés sur un dossier, F2), sélection de plusieurs
  fichiers, vignettes ; dans la fenêtre de création, les descriptions et les icônes des composants
  du jeu (tirées des commentaires de leur code), et des préréglages faits de préfabs du projet ; un
  panneau redessiné seulement quand il change ; les polices de l'éditeur
  cuites une fois et gardées en cache plutôt qu'à chaque lancement ; un seul thème pour ImGui et
  `Devex::Ui` tant qu'ils cohabitent.
- **CI, la suite** : Linux, puis macOS ; les tests `[gpu]` sur un rendu logiciel (lavapipe,
  SwiftShader) ou une machine avec GPU ; actions passées à Node.js 24 (celles en v4 tournent sur
  Node.js 20, déprécié) ; cache de compilation (sccache) ; éditeur et lecteur publiés en artefacts
  à chaque version ; badge d'état dans le README.
- **Profileur** : export vers Perfetto (format de trace de Chrome), compteurs dans la chronologie
  (draw calls, mémoire, images par seconde), mémoire réellement allouée (VMA, tas du processus,
  GC de .NET), recherche d'une zone et moyenne sur plusieurs images, comparaison de deux captures,
  capture enregistrée dans un fichier, profilage d'un jeu exporté à distance.
- **Chargement asynchrone, la suite** : file de transfert dédiée pour les copies, streaming des
  gros niveaux (charger et décharger par zones), décodage des sons et des animations sur les
  workers, construction de la scène elle-même (et de ses préfabs) hors du thread principal,
  cuisson de l'IBL sans attente, priorités (ce qui est proche de la caméra d'abord),
  déchargement des assets qui ne servent plus.
- **Textures partagées** : une image utilisée par un modèle et présente dans le projet est
  importée deux fois ; relier les deux demandera de connaître son rôle (couleur, normale).
- **Import 3D, la suite** : caméras et lumières des fichiers, morph targets (glTF et FBX),
  matériaux par instance, couleurs de sommets et seconds jeux d'UV, rétablir les chemins absolus
  des images, modèles placés qui suivent leur fichier (préfabs de modèles), options d'import
  des animations (découpage d'une pile en clips, fréquence d'échantillonnage).
- **Autres plateformes de textures** : ASTC ou Basis Universal pour le mobile, produits à l'export.
- **Culling** : sur GPU avec dessin indirect, culling par occlusion, et hiérarchie spatiale pour
  les grandes scènes (aujourd'hui chaque instance est testée une par une).
- **Ombres locales** : filtrage plus doux (PCSS), tuiles gardées d'une frame à l'autre quand rien
  ne bouge, et ombres des surfaces transparentes.
- **Réflexions locales** : sondes de réflexion placées dans la scène, SSR.
- **Transparence** : résolution sans tri (OIT pondéré) pour les surfaces qui s'entrecroisent,
  ombres des surfaces transparentes, réfraction, tri par triangle plutôt que par instance.
- **Post-traitements** : profondeur de champ, flou de mouvement, volumes qui mélangent leurs
  réglages selon la position de la caméra, mise à l'échelle temporelle (rendu sous la résolution
  de l'écran), occlusion ambiante par cônes plutôt que par points.
- **Ciel procédural** : atmosphère physique liée à la lumière directionnelle.
- **Écran 2D, la suite** : poignées des éléments tournés ou mis à l'échelle, aimantation et
  repères, sélection des éléments au rectangle, aperçu à plusieurs résolutions, textes liés
  (`UiBinding`) montrés hors du jeu, canevas cachés montrés à la demande, gizmos au-dessus de
  l'interface, interfaces placées dans le monde 3D (canevas en espace monde).
- **Éditeur** : vues Scène et Jeu simultanées (plusieurs vues par frame dans le renderer), jeu
  dans un processus séparé, lignes épaisses, pivot au centre de la sélection (mode *Center* de
  Unity), listes éditées à plusieurs, entités masquées aussi dans l'écran 2D, entité active ou
  non pour le jeu (`SetActive` de Unity), déplacement au clavier dans l'arbre.
- **Apparence** : thèmes enregistrables en fichiers, icône de projet choisie par projet, polices
  pour le chinois, le japonais et le coréen (Noto CJK), barre de titre intégrée à l'éditeur.
