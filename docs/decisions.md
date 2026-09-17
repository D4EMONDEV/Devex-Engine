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
| Architecture de rendu    | Forward+ clustered, PBR métal-rugosité (GGX), MSAA 4x              |
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
| Projets dans l'éditeur   | Écran d'accueil (projets récents, nouveau, ouvrir) ou argument     |
| Scènes                   | Assets importés (`AssetId`), ouvertes et enregistrées par l'éditeur |
| Sélection                | Simple pour l'instant                                              |
| Backend ImGui            | Officiels SDL3 + Vulkan, le backend Vulkan compilé avec volk       |
| Multi-fenêtre ImGui      | Docking dans la fenêtre principale seulement                       |
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
| `Reflection`    | description des champs (`TypeInfo`, `DEVEX_REFLECT`, `ValueKind`)         | Core, Math                    |
| `Serialization` | format texte `.dvx*` (sections, valeurs) ; plus tard archives cookées     | Core                          |
| `Asset`         | `AssetId`, données CPU (maillages, textures, matériaux, modèles), `.dvxasset`, projet | Core, Math, Reflection, Serialization |
| `AssetImport`   | base d'assets, `.dvxmeta`, importeurs (textures, `.dvxmat`, glTF)         | Asset, Scene, fastgltf, basisu, stb, efsw |
| `Render`        | façade `Renderer` / `RenderWorld` ; tout `Vk*` reste dans `src/render/vulkan` | Core, Math, Platform, Asset, Vulkan |
| `Scene`         | entités, sparse sets, hiérarchie, composants intégrés, `.dvxscene`, sous-arbres | Core, Math, Reflection, Serialization, Asset |
| `Tools`         | panneaux ImGui, annulation, éditeur (viewport, gizmos, scènes, accueil)   | Core, Platform, Render, Scene, AssetImport, ImGui |
| `Runtime`       | `Application`, boucle, mode éditeur et Play, `AssetManager`, extraction   | tous les modules ci-dessus    |

Applications au sommet : `apps/sandbox`, `apps/editor` et la DLL gameplay d'un jeu
dépendent de `Runtime`.

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

### Dépôt

```text
engine/        modules du moteur (include/ + src/)
apps/sandbox/  bac à sable des jalons et son projet (project/Sandbox.dvxproj)
apps/editor/   devex-editor, l'éditeur sans code de jeu
shaders/       sources Slang du moteur, compilées dans bin/shaders
tests/         tests Catch2, un dossier par module, données dans tests/data
scripts/       outils de développement (génération des assets d'exemple)
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
- **Types de composants** : un index attribué au premier usage dans le processus. Il n'est
  pas stable d'un binaire à l'autre ; le rechargement à chaud d'une DLL gameplay devra
  identifier les types par leur nom enregistré.
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
  vec3, vec4, quat, UUID, `AssetId` et énumérations (`EnumNames<T>` liste les noms, écrits
  en texte dans les fichiers). Des indications guident l'inspecteur (`FieldHints`) : type
  d'asset attendu, couleur, angle affiché en degrés. MSVC 19.51 ne fournit pas encore `<meta>`
  (réflexion C++26) ; ces déclarations pourront alors être générées.
- **Composants du jeu** : une struct, sa réflexion, puis `scene::registerComponent<T>()`.
  Seuls les composants enregistrés sont sauvegardés.
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
  MyGame.dvxproj              # [project format=1 name="MyGame"]
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
  se charge et range son texte dans un artefact `scene`, pour qu'un jeu charge un niveau par
  identifiant.

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
  programme à part. `devex-editor` est une application vide lancée dans ce mode ; un jeu y entre
  de la même façon (`devex-sandbox --editor`) et joue alors son propre code. Dans l'éditeur,
  `onStartup` et `onShutdown` s'exécutent normalement, mais `onEvent`, les mises à jour et
  `onRender` seulement **en mode Play**, entre `onPlayStarted` et `onPlayStopped`.
  `isEditor()` et `isPlaying()` le disent au jeu ; `requestQuit()` arrête alors le mode Play.
  La DLL gameplay rejoindra ce mode quand elle existera.
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
- **Panneaux** : hiérarchie (sélection, création, suppression, glisser-déposer pour changer
  de parent), inspecteur généré par la réflexion (nom, UUID, composants, ajout et retrait),
  statistiques (FPS, GPU, draw calls, VRAM via VMA), console (niveaux filtrables). La
  disposition par défaut est construite au premier lancement, puis sauvegardée dans
  `devex-tools.ini` à côté de l'exécutable ; *View > Reset layout* la restaure.
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
  dessiné dans une seconde passe sur le backbuffer ; ses couleurs de style, pensées en
  sRGB, sont converties en linéaire pour le swapchain sRGB.

### Éditeur

- **Fenêtre** : barre de menus (*File*, *Edit*, *Entity*, *View*) avec les boutons Play, Pause et
  pas à pas au centre ; panneau *Viewport* au centre de la disposition, hiérarchie à gauche,
  inspecteur à droite, assets, console et statistiques en bas (`devex-editor.ini`). Le titre
  donne la scène, `*` si elle a des modifications non enregistrées, le projet et l'état de jeu.
- **Écran d'accueil** : sans projet, liste des projets récents (dans le dossier de données de
  l'utilisateur, `%APPDATA%/Devex/Editor/editor.dvx`), *Open project…* et *New project…* (nom et
  dossier ; le projet reçoit `assets/scenes/Main.dvxscene`). Un `.dvxproj` passé en argument
  s'ouvre directement. Les dialogues de fichiers sont ceux du système (SDL3), sans bloquer : la
  réponse arrive par `Platform::pollEvents`.
- **Scènes** : *New scene* crée une petite scène éclairée (soleil, ciel, caméra, sol, cube) ;
  *Open scene…* ou un double-clic sur une scène du panneau Assets l'ouvre ; *Save* l'écrit dans
  son fichier, *Save as…* dans le dossier du projet, puis la base d'assets la réimporte.
  À l'ouverture d'un projet, l'éditeur rouvre la dernière scène (`.devex/editor.dvx`, avec la
  caméra), sinon la première scène du projet, sinon une nouvelle ; une scène déjà remplie par
  l'application est gardée. Les modifications non enregistrées sont repérées par l'identifiant
  d'état de l'historique (`CommandHistory::stateId`) : avant de changer de scène, de projet ou
  de quitter, l'éditeur demande de les enregistrer, de les abandonner ou d'annuler.
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
- **Création** : *Entity > Create* place une entité vide, une primitive, une lumière, une caméra
  ou un environnement au pivot de la caméra ; un modèle glissé dans le viewport se pose au sol
  sous la souris. Suppr supprime la sélection.
- **Pendant le jeu** : l'historique des modifications est mis de côté ; les modifications faites
  à la copie jouée ont leur propre historique, oublié à l'arrêt.

### Assets

- **Projet** : `ApplicationConfig::project` désigne le `.dvxproj` (ou l'éditeur l'ouvre) ; `Runtime` ouvre alors une
  `AssetDatabase` sur son dossier `assets/`. Le bac à sable ouvre `apps/sandbox/project` depuis les
  sources, pour que les modifications d'assets s'y voient en direct.
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
  modèle à venir. Le bac à sable et l'éditeur l'utilisent pour les fichiers déposés sur la fenêtre.
- **Outils** : panneau *Assets* (sources, type, statut, erreur en infobulle, réimport, sous-assets
  dépliables) ; un asset se glisse sur un champ `AssetId` de l'inspecteur, un modèle dans la
  hiérarchie (placement annulable). L'inspecteur liste les assets du type attendu.

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
| efsw                  | surveillance des fichiers | 6 ✅ |
| mikktspace            | tangentes            | 7 ✅     |
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

Ensuite, sans ordre figé : physique, préfabs liés, DLL gameplay rechargeable, export d'un jeu
(paquet d'artefacts), post-traitements (bloom, TAA), transparence, CI Linux.

## Questions ouvertes

À trancher le moment venu, pas avant :

- **Systèmes** : ordre d'exécution et planification des systèmes du jeu (aujourd'hui, le
  gameplay itère lui-même les vues dans `onFixedUpdate` et `onUpdate`).
- **Physique** : Jolt Physics est le candidat naturel (MIT, utilisé par Godot 4).
- **Audio** : SDL3 audio, miniaudio ou FMOD/Wwise en option.
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
  séparé, glisser des matériaux sur les objets du viewport, lignes épaisses.
