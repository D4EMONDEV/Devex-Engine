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
| Architecture de rendu    | Forward+ clustered, PBR métal-rugosité (GGX), MSAA 4x              |
| Gameplay                 | C++ (DLL rechargeable) et C# (.NET hébergé), au choix, ensemble    |
| Format source            |  Texte maison lisible, extensions `.dvx*`, binaire cooké à l'export|
| Import 3D                | glTF 2.0 (fastgltf) + FBX (ufbx)                                   |
| UI éditeur               | Dear ImGui (docking) au style de Godot, UI retenue maison plus tard |
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
| Sélection                | Simple pour l'instant                                              |
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
| Audio                    | miniaudio + stb_vorbis, sons 2D ou spatialisés                    |
| Clips audio              | WAV, FLAC, MP3, Ogg Vorbis ; décodés au chargement ou à la lecture |
| Écouteur                 | `AudioListener`, sinon caméra principale                         |
| Mixage                   | Volume Master et 8 groupes nommés par projet                      |
| Animation                | Squelettes glTF, clips en sous-assets du modèle                   |
| Os                       | Une entité par os, pilotée par nom                                |
| Lecture                  | Composant `Animator` : un clip, fondu croisé, root motion en option |
| Skinning                 | Dans le vertex shader, matrices d'os en buffer par frame          |

## Architecture cible

Chaque module est une bibliothèque d'objets CMake avec son API publique dans
`engine/include/devex/<module>/` et son implémentation dans `engine/src/<module>/`. Tous les
modules forment **une seule bibliothèque partagée**, `devex-engine` (`Devex::Engine`), que lient
les programmes, les tests et les modules de jeu : ils partagent ainsi un seul état du moteur
(registres, journal, renderer), et un jeu voit la même API C++ que l'éditeur. Tous ses symboles
sont exportés (`WINDOWS_EXPORT_ALL_SYMBOLS`) ; en contrepartie, un module de jeu doit être
compilé avec le même compilateur et la même configuration que le moteur.
Les dépendances sont strictement descendantes : un module ne connaît jamais un module
situé au-dessus de lui, et le graphe reste sans cycle.

| Module          | Rôle                                                                      | Dépend de                     |
| --------------- | ------------------------------------------------------------------------- | ----------------------------- |
| `Core`          | types, `Result<T>`/`Error`, assert, log, handles, UUID, allocateurs, jobs | —                             |
| `Math`          | alias `Vec3`, `Mat4`, `Quat`…, conventions de repère et de profondeur     | Core, GLM                     |
| `Platform`      | fenêtre, entrées, temps, dialogues, bibliothèques partagées, processus    | Core, SDL3                    |
| `Reflection`    | description des champs (`TypeInfo`, `DEVEX_REFLECT`, `ValueKind`)         | Core, Math                    |
| `Serialization` | format texte `.dvx*` (sections, valeurs) ; plus tard archives cookées     | Core                          |
| `Asset`         | `AssetId`, données CPU (maillages, textures, matériaux, modèles, clips audio et d'animation), `.dvxasset`, projet, paquet `.dvxpak` | Core, Math, Reflection, Serialization, zstd |
| `AssetImport`   | base d'assets, `.dvxmeta`, importeurs (textures, `.dvxmat`, glTF, sons)   | Asset, Scene, Audio, fastgltf, basisu, stb, efsw |
| `Render`        | façade `Renderer` / `RenderWorld` ; tout `Vk*` reste dans `src/render/vulkan` | Core, Math, Platform, Asset, Vulkan |
| `Scene`         | entités, sparse sets, hiérarchie, composants intégrés, `.dvxscene`, sous-arbres, préfabs | Core, Math, Reflection, Serialization, Asset |
| `Audio`         | clips, mixage et groupes, sources et écouteur de la scène                | Core, Math, Asset, Scene, miniaudio, stb |
| `Animation`     | clips d'animation, échantillonnage, fondus, squelettes des `Animator`    | Core, Math, Asset, Scene             |
| `Tools`         | panneaux ImGui, annulation, éditeur (viewport, gizmos, scènes, accueil)   | Core, Platform, Render, Scene, Audio, Animation, AssetImport, ImGui |
| `Runtime`       | `Application`, boucle, mode éditeur et Play, modules de jeu, `AssetManager`, extraction | tous les modules ci-dessus |

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
- **Frame** : ombres du soleil (4 couches d'une image `D32` 2048²), scène HDR `RGBA16F`
  multi-échantillonnée (MSAA 4x par défaut, `RendererConfig::msaaSamples`, résolue par
  moyenne) avec les maillages puis le ciel, mesure de luminance (si exposition automatique),
  sélection (si demandée), masque des objets entourés (s'il y en a), tonemapping vers la
  cible, overlay des outils, puis ImGui sur le swapchain.
- **Cible** : sans `RenderWorld::viewport`, la scène est dessinée sur tout le swapchain.
  Avec, elle l'est dans une image de cette taille (format du swapchain, au plus 8192²) que les
  outils affichent dans un panneau : `Renderer::viewportTexture()` est un identifiant de
  texture ImGui fixe, remplacé pendant le dessin par le descriptor set ImGui de l'image de la
  frame (un par contexte de frame, recréé quand l'image change).
- **Sélection à la souris** (*picking*) : `RenderWorld::pick` demande l'objet visible sous un
  pixel. Une passe dessine tous les maillages dans une cible `R32_UINT` de 1 × 1 avec une
  projection qui étire ce pixel sur toute la cible, en écrivant `MeshInstance::objectId`
  (0 = rien) et en respectant le mode alpha `mask` ; la valeur est copiée dans un buffer
  visible par le CPU et lue quand la frame est terminée, en général deux frames plus tard
  (`Renderer::takePickResults`). `Runtime` identifie chaque instance par l'index de son entité
  plus un.
- **Overlay des outils** : lignes et triangles colorés (`RenderWorld::sceneLines`,
  `overlayLines`, `overlayTriangles`) dessinés en mélange alpha après le tonemapping. Les
  lignes de scène sont cachées par les surfaces plus proches : la profondeur MSAA est résolue
  (échantillon 0) dans une image simple, et leur profondeur est légèrement avancée pour rester
  visibles sur les surfaces où elles reposent. Les autres passent devant tout. Les instances
  `outlined` sont dessinées dans un masque `R8`, puis un plein écran trace le contour orange des
  pixels hors masque à moins de deux pixels de lui. Lignes d'un pixel de large : pas encore de
  lignes épaisses ni anticrénelées.
- **Profondeur** : reverse-Z à far plane infini (`math::perspectiveReverseZ`), `D32_SFLOAT`
  effacée à 0 et test `GREATER_OR_EQUAL`. La projection de `Math` garde Y vers le haut ; le
  renderer applique la correction du clip space Vulkan (Y vers le bas).
- **Slang** : `cmake/DevexShaders.cmake` compile chaque shader d'entrée (`mesh`, `shadow`,
  `sky`, `tonemap`, `luminance`, `ibl`, `pick`, `overlay`) en un `.spv` contenant tous ses points d'entrée
  (`-fvk-use-entrypoint-name`), avec des matrices column-major comme GLM ; les modules
  importés (`common`, `pbr`) entrent dans le depfile. Une erreur de shader est une erreur de
  build. L'API Slang servira plus tard au rechargement à chaud dans l'éditeur. En Vulkan,
  `SV_VertexID` ne compte pas le premier sommet du dessin : un dessin qui commence au milieu
  d'un buffer reçoit l'adresse de ce premier sommet.
- **Données GPU** : vertex pulling. Les shaders lisent sommets et données de scène via des
  *buffer device addresses* passées en push constants (96 octets, sous le minimum garanti
  de 128) ; pas de vertex input state. Sommets de 48 octets : position, normale, UV et
  tangente. Les dispositions mémoire C++ et Slang sont
  vérifiées par `static_assert` (`src/render/vulkan/GpuData.hpp`). Le descriptor set
  global bindless porte les textures (voir ci-dessous).
- **Maillages** : `Renderer::createMesh` valide les données et les transfère de façon
  synchrone (buffer de staging VMA) ; il renvoie un `MeshHandle` générationnel
  (`core::SlotMap`). `destroyMesh` retarde la libération jusqu'à ce qu'aucune frame en
  vol ne puisse encore l'utiliser. Faces avant dans le sens antihoraire, back-face culling.
  Un maillage a des **sous-maillages** (plages d'indices) ; le `RenderWorld` contient une
  instance par sous-maillage avec son matériau.
- **Textures** : `Renderer::createTexture` envoie tous les niveaux de mip d'un `TextureData`
  (RGBA8, BC5, BC7, RGBA16F) et renvoie un `TextureHandle`. Elles vivent dans **un descriptor
  set global bindless** (set 0 : tableau de `Texture2D` indexé, `PARTIALLY_BOUND` et
  `UPDATE_AFTER_BIND`, 8192 emplacements au plus, sampler linéaire, répétition, anisotrope
  x16), qui porte aussi l'IBL, la table BRDF et le ciel. Le set 1, un par contexte de frame,
  porte la carte d'ombres, la couleur de scène résolue et le masque de sélection. Les
  emplacements 0 et 1 sont une texture blanche et une normale plate qui remplacent les textures
  absentes. Une texture détruite garde son emplacement jusqu'à la fin des frames en vol, puis
  l'emplacement repointe vers le blanc avant d'être réutilisé.
- **Matériaux** : `createMaterial` / `updateMaterial` / `destroyMaterial` sur un `MaterialDesc`
  (paramètres glTF avec des `TextureHandle`). Le renderer en tire une table `GpuMaterial` de 80
  octets dont **chaque frame en vol garde sa copie** (buffer adressé par `SceneData`), recopiée
  quand un matériau ou une texture change. Le shader utilise tous les paramètres : couleur de
  base, métal et rugosité (rugosité bornée à 0,045), normal map (BC5, Z reconstruit, échelle),
  occlusion (sur la lumière indirecte), émission et mode alpha `mask` (discard, ombres
  comprises) ; `blend` est dessiné opaque en attendant la transparence. Les matériaux `doubleSided` utilisent un second pipeline sans culling et éclairent
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
  par le swapchain.
- **Limites actuelles** : pas de culling (tout est dessiné, y compris dans chaque cascade), pas
  d'ombres des lumières locales, résolution MSAA par moyenne des valeurs HDR (contours très
  contrastés parfois crénelés), pas de réflexions locales.
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
  en degrés. La réflexion connaît aussi la taille des types et l'emplacement de chaque champ,
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
  glTF s'importe sans conversion ; FBX est converti à l'import.

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
[scene format=1]

[entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23" name="Player"]
parent = "b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44"

[component type="Transform"]
position = vec3(0, 1, 0)
rotation = quat(0, 0, 0, 1)
scale = vec3(1, 1, 1)

[component type="MeshRenderer"]
mesh = asset("00000000-0000-0000-0000-000000000001")
```

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
  `<Jeu>.exe` (le lecteur `devex-player` renommé et son icône remplacée), `<Jeu>.dvxpak`,
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
- Les actions nommées (InputMap, rebinding, manettes) viendront par-dessus plus tard.

### Outils

- **Module `Tools`** : panneaux Dear ImGui indépendants de Vulkan, affichés en overlay dans
  toute application avec **F1** (`ApplicationConfig::enableTools`, actif hors Release), ou
  autour du viewport de l'éditeur (`ToolsMode::Editor`).
- **Panneaux** (noms de Godot) : *Scene*, l'arbre des entités (icône colorée selon les
  composants, filtre, menu *+* de création, glisser-déposer pour changer de parent, double-clic
  pour cadrer) ; *Inspector*, généré par la réflexion (nom, UUID, une section repliable par
  composant avec son icône et un menu pour le retirer, propriétés sur deux colonnes, vecteurs
  aux lettres x, y, z colorées, angles en degrés, *Add Component* avec recherche) ;
  *FileSystem*, l'arborescence `res://` des sources (icône par type, état d'import, menu :
  ouvrir, placer, scène de démarrage, réimporter, copier le chemin, afficher dans l'explorateur)
  ; *Output* (police à chasse fixe, recherche, compteurs par niveau qui servent de filtres) ;
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
- **Icônes** (`src/tools/Icons`) : 98 icônes Lucide 1.47.0 (licence ISC, `third_party/lucide`) et le
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
  `Platform::pollEvents`.
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
  le CPU), sinon demande au GPU l'objet visible sous le pixel ; l'entité sélectionnée et ses
  descendants sont entourés. Une seule entité à la fois.
- **Gizmos** (maison, `src/tools/Gizmo`) : W déplacement (axes, plans, plan de la vue), E
  rotation (anneaux par axe, anneau de la vue), R échelle (axes, uniforme au centre) ; X alterne
  axes du monde et de l'entité (l'échelle est toujours locale). Taille constante à l'écran,
  poignées testées en pixels, poignée survolée ou active en jaune. Ctrl aimante par 0,5 m, 15°
  ou 0,1. Glisser modifie `Transform` en direct ; le relâchement enregistre une étape
  annulable par champ modifié. Parent quelconque : le déplacement passe par l'inverse de sa
  matrice monde.
- **Icônes et repères** : grille au sol autour de la caméra (tous les mètres, tous les dix
  mètres de haut) qui s'estompe au loin, axes X et Z colorés ; icônes des lumières et des
  caméras, portée des lumières ponctuelles, cône des spots et pyramide de la caméra quand elles
  sont sélectionnées.
- **Barre d'outils du viewport** : sélection seule (Q, sans gizmo), déplacement (W), rotation (E),
  échelle (R), axes locaux ou du monde, aimantation permanente (Ctrl l'inverse), grille, icônes
  des lumières et caméras, cadrage ; à droite, vitesse de vol, EV de la caméra de l'éditeur et
  aide des contrôles en infobulle. En Play, elle annonce l'état du jeu et le viewport est encadré
  à la couleur d'accent.
- **Création** : le menu *+* de *Scene*, *Edit > Create* ou le menu contextuel de l'arbre place une
  entité vide, une primitive, une lumière, une caméra ou un environnement au pivot de la caméra
  (ou comme enfant de l'entité choisie) ; un modèle glissé dans le viewport se pose au sol sous
  la souris. Suppr supprime la sélection.
- **Raccourcis** : Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Maj+S, Ctrl+Alt+S, Ctrl+W, Ctrl+Tab ; F5 ou Ctrl+P
  pour jouer, F7 pause, F8 arrêt, F9 pas à pas ; Ctrl+B compile le code du jeu ; Ctrl+Maj+Q
  revient au gestionnaire de projets, Ctrl+Q quitte.
- **Pendant le jeu** : l'historique des modifications est mis de côté ; les modifications faites
  à la copie jouée ont leur propre historique, oublié à l'arrêt.

### Assets

- **Projet** : `ApplicationConfig::project` désigne le `.dvxproj` (ou l'éditeur l'ouvre) ; `Runtime` ouvre alors une
  `AssetDatabase` sur son dossier `assets/`. Le bac à sable est le projet `samples/sandbox`,
  ouvert depuis les sources pour que les modifications d'assets s'y voient en direct.
- **Sources et importeurs** : chaque fichier dont l'extension a un importeur est une source ;
  `texture` (`.png`, `.jpg`, `.tga`, `.bmp`, `.hdr`, décodés par stb_image), `material`
  (`.dvxmat`), `gltf` (`.gltf`, `.glb`) et `scene` (`.dvxscene`). Les fichiers et dossiers cachés (`.`) sont ignorés.
- **`.dvxmeta`** : créé au premier scan avec un UUID aléatoire et les options par défaut de
  l'importeur. Il porte l'identité de l'asset : il se versionne avec la source. Un `.dvxmeta`
  illisible est signalé et laissé tel quel, jamais remplacé. Une source copiée avec son
  `.dvxmeta` (UUID déjà vu) reçoit de nouveaux identifiants.
- **Sous-assets** : les éléments d'un fichier (maillages, matériaux, textures d'un glTF) sont
  listés dans le `.dvxmeta` par type, **clé** et UUID. La clé est le nom glTF, un nom de repli
  (`Mesh 2`) ou le nom suivi de `#index` en cas de doublon ; une texture reçoit le suffixe de son
  rôle (` (linear)`, ` (normal)`). Une clé connue garde son UUID ; une clé disparue reste listée,
  pour que son UUID revienne avec elle.
- **Cache** : chaque import écrit ses artefacts (`imported/<uuid>.dvxasset`, écriture atomique)
  et un enregistrement texte (`sources/<uuid>.dvxsource`) : importeur et version, empreinte des
  options, taille, date et hachage XXH64 de la source et de ses dépendances, artefacts produits,
  erreur éventuelle. À l'ouverture, ces enregistrements rendent les assets disponibles
  immédiatement ; les artefacts qu'aucun enregistrement ne cite sont supprimés.
- **Détection des changements** : taille et date d'abord, contenu haché seulement si elles
  diffèrent (un fichier touché sans changement n'est pas réimporté). Changent aussi l'import :
  options, importeur, version, dépendances (buffers et images externes d'un `.gltf`), artefact
  manquant. Une source déplacée avec son `.dvxmeta` garde ses assets sans réimport.
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
  (maillage et matériaux d'un `MeshRenderer`, textures d'un matériau, modèle placé), de façon
  synchrone depuis le cache. Sur `Imported`, un asset chargé est rechargé : maillages et textures
  changent de handle (l'ancien est libéré après les frames en vol), un matériau garde le sien,
  et les matériaux sont résolus à nouveau après tout changement de texture. Les maillages
  enregistrés par l'application (primitives) ne sont jamais détruits par lui.
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
- **Ajout de fichiers** : `AssetDatabase::addFile` copie un fichier extérieur dans un dossier du
  projet, avec les buffers et images d'un `.gltf`, écrit son `.dvxmeta` et renvoie l'UUID du
  modèle à venir. L'éditeur l'utilise pour les fichiers déposés sur la fenêtre.
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

- **Import** : un fichier glTF rigué produit, à côté de ses maillages et matériaux, un squelette et
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
  composants, champs ajoutés ou retirés compris. Une partie en cours continue.
- **Lecteur** : `devex-player Projet.dvxproj` ouvre la scène de démarrage du projet (*Set as
  startup scene* dans le panneau Assets, sinon la première scène) avec son module de jeu, sans
  éditeur ; il ne compile pas le code.
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
  tabulations, annulation/rétablissement du champ actif, position ligne/colonne, **Ctrl+S**, Save All,
  Reload et ouverture dans l'IDE externe. Les scripts enregistrés sont recompilés automatiquement.
  **Editor > Panels > Text Editor** rouvre le panneau ; le masquer conserve les fichiers ouverts.
  **Ctrl+O** ouvre un fichier texte et **Ctrl+W** ferme l'onglet lorsque ce panneau a le focus.
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
| zstd                  | paquets de jeux      | 13 ✅    |
| FreeType (`imgui[freetype]`) | rendu des polices de l'éditeur | 10 ✅ |
| plutosvg              | icônes SVG de l'éditeur | 10 ✅  |
| Catch2                | tests (feature `tests`) | 0 ✅  |

Hors vcpkg :

- **.NET 10** : le SDK compile le code C# des projets et l'assembly `Devex.Managed` du moteur
  (`dotnet` cherché à la configuration, facultatif : sans lui, le moteur se construit et tourne
  sans C#) ; le runtime est chargé à l'exécution par `hostfxr`, jamais lié au moteur ;
- **Slang** est fourni par le SDK Vulkan (`C:\VulkanSDK\1.4.350.0`) ;
- **ufbx** n'existe pas dans vcpkg : ses deux fichiers (`ufbx.c`, `ufbx.h`) seront
  intégrés dans `third_party/ufbx` lors du support FBX ;
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
   cascades, ciel HDR et IBL, MSAA, exposition automatique, tonemapping AgX, tangentes
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

Ensuite, sans ordre figé : post-traitements (bloom, TAA), transparence, CI Linux.

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
- **Préfabs** : retirer un composant ou une entité du préfab dans une instance, variantes
  explicites (une instance à la racine d'un préfab en fait déjà une), onglet ouvert sur le préfab
  d'une instance avec son contexte, édition des entités d'une instance sur place (Godot *Editable
  Children*), sélection de la racine d'une instance au premier clic, mise à jour des instances
  pendant le jeu, modifications vers des entités retirées gardées au lieu d'être abandonnées.
- **Export** : autres plateformes (Linux, macOS), export incrémental et paquets de mise à jour ou de
  contenu additionnel, signature de l'exécutable et installeur, retrait des bibliothèques qu'un jeu
  n'utilise pas, cuisson des textures par plateforme, chargement asynchrone depuis le paquet,
  export sans build Release du moteur (paquet d'un moteur distribué).
- **Audio** : streaming depuis le disque, occlusion, réverbération, effets et routage des groupes,
  budget de voix, plusieurs écouteurs, intégration optionnelle de FMOD/Wwise.
- **Animation** : machine à états et blend trees dans l'éditeur, couches et masques d'os,
  événements de clip, cinématique inverse, morph targets, pré-skinning en compute (colliders et
  rayons suivant la pose), réutilisation d'un clip entre squelettes différents (retargeting),
  compression des courbes.
- **UI retenue maison** pour l'éditeur et les jeux, qui remplacera ImGui.
- **CI** : GitHub Actions Windows, puis Linux.
- **Chargement asynchrone** : lecture et envoi GPU des assets hors du thread principal, streaming
  des gros niveaux (aujourd'hui, le chargement depuis le cache est synchrone).
- **Textures partagées** : une image utilisée par un `.gltf` et présente dans le projet est
  importée deux fois ; relier les deux demandera de connaître son rôle (couleur, normale).
- **Autres plateformes de textures** : ASTC ou Basis Universal pour le mobile, produits à l'export.
- **Culling** : frustum culling sur CPU, puis sur GPU avec dessin indirect.
- **Ombres locales** : atlas d'ombres pour les spots et cubemaps pour les lumières ponctuelles.
- **Réflexions locales** : sondes de réflexion placées dans la scène, SSR.
- **Ciel procédural** : atmosphère physique liée à la lumière directionnelle.
- **Éditeur** : multi-sélection et rectangle de sélection, copier-coller et duplication, vues
  Scène et Jeu simultanées (plusieurs vues par frame dans le renderer), jeu dans un processus
  séparé, glisser des matériaux sur les objets du viewport, lignes épaisses, visibilité des
  entités (l'œil de Godot), renommage dans l'arbre.
- **Apparence** : thèmes enregistrables en fichiers, icône de projet choisie par projet, polices
  pour le chinois, le japonais et le coréen (Noto CJK), barre de titre intégrée à l'éditeur.
