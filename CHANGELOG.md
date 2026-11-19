# Journal des versions

Ce qui change d'une version publiée à l'autre. Ce que promet chaque version aux projets existants est dans
[docs/compatibilite.md](docs/compatibilite.md). Les décisions et leurs raisons sont dans [docs/decisions.md](docs/decisions.md).

## À venir

- **Android sans Vulkan 1.3** : un téléphone qui n'a que Vulkan 1.1, comme beaucoup d'entrée de gamme, dessine avec le renderer WebGPU du web, par Dawn, au lieu de refuser le jeu ; l'APK ne demande plus que Vulkan 1.1. Ces téléphones ont l'image de Compatibility, et les shaders du projet y demandent que le projet soit en Compatibility.
- **Les téléphones 32 bits** : **Phones, 32 Bits (armeabi-v7a)** dans l'export Android, avec le préréglage `android-arm32`, pour les Android 32 bits, que font tourner bien des téléphones d'entrée de gamme même avec un processeur 64 bits.
- **L'échelle 3D sur Android** : **Render Scale** dans la carte **Mobile**, comme le `scaling_3d/scale` de Godot. Android dessine la scène avec cette part des pixels de l'écran, en largeur et en hauteur, puis l'agrandit ; l'interface garde chaque pixel. Le préréglage mobile, celui d'un projet qui n'a pas écrit la carte, la met à 50 % : un téléphone dessinait jusque-là chaque pixel de son écran, environ quatre fois plus que la page web sur le même téléphone.
- **Un renderer OpenGL** : OpenGL 3.3, et OpenGL ES 3.0 comme l'ont les téléphones, dessinent comme Vulkan le fond de l'environnement, les sprites, les tilemaps, le texte, l'interface et les captures, puis la 3D : les maillages, avec ou sans squelette, leurs matériaux, la lumière du soleil, des lumières locales (32 par image, 8 par objet, comme le Compatibility de Godot) et de l'environnement, les ombres, le brouillard, les surfaces mélangées et l'exposition automatique ; les particules et les traînées, les terrains, leurs couches et leur herbe. Il ne sert que quand on le demande (`RenderApi::OpenGl`) : les décals, les effets après la lumière, les shaders du projet, Android et WebGL2 viennent ensuite, avant qu'il ne remplace WebGPU. Ses shaders sont traduits en GLSL au build par SPIRV-Cross, une nouvelle dépendance de vcpkg.
- **Un APK propre à chaque export** : Gradle empaquetait dans l'APK de l'export d'avant, en y laissant la place des anciens fichiers, et un APK de 21 Mo en faisait 34 à l'export suivant.
- **Une source réécrite aussitôt importée** : la base d'assets prenait pour inchangé un fichier de même taille et de même date, or l'horloge des fichiers n'avance que par pas de quelques millisecondes sous Linux. Un fichier réécrit dans le même pas que son import gardait son ancien import. Comme git pour son index, une date de moins de deux secondes ne prouve plus rien, et le contenu est relu.

## 0.11.1 — 2026-11-18

Les jeux web sur téléphone : plus fluides, le son sans grésillement, et l'arène du bac à sable jouable au doigt. Et les
sauvegardes du navigateur, qu'un onglet ouvert en arrière-plan pouvait effacer. C'est toujours un aperçu.

**Télécharger**

- Windows : `devex-0.11.1-windows-x64-setup.exe`, sans droits administrateur, ou le `.zip` à extraire. L'installateur n'est pas
  signé : si Windows affiche « Windows a protégé votre ordinateur », cliquer sur *Informations complémentaires*, puis *Exécuter quand même*.
- Linux : `devex-0.11.1-linux-x64.tar.gz`, à extraire, puis lancer `bin/devex-editor`. Ubuntu 24.04 ou plus récent.
- Le lecteur web vient avec l'éditeur, dans `platforms/web`.

**Mettre à jour**

- L'installateur remplace la version déjà installée, au même endroit. Ni les projets ni les réglages n'y sont : ils restent.
- Un jeu web exporté avec la 0.11.0 garde son lecteur : l'exporter à nouveau lui donne ces corrections.
- Un projet qui n'a pas réglé la carte Android coupe désormais les effets lourds sur téléphone, Android compris ; la carte
  **Mobile** les rend.

**Nouveautés**

- **Les jeux web sur téléphone** : dans le navigateur d'un téléphone ou d'une tablette, le jeu coupe les effets lourds comme sur Android, dessine les pixels de la page plutôt que tous ceux de l'écran, et des ombres moitié moins fines ; il allait jusqu'à quelques images par seconde. Le son ne grésille plus quand une image tarde. Les commandes à l'écran s'affichent dès le chargement, sans attendre le premier doigt.
- **La carte Mobile de Project Settings > Rendering** remplace la carte Android : elle vaut pour Android et pour les navigateurs des téléphones, et un projet qui ne l'a pas réglée coupe tous les effets lourds sur téléphone.
- **Des APK sept fois plus légers** : l'export Android donne à Gradle le NDK et sa version, et Gradle épure enfin les bibliothèques de leurs symboles de débogage. L'APK de l'arène passe de 124 Mo à 17 Mo, et de plus de 400 Mo installé à une trentaine.
- **Se déplacer au doigt dans l'arène du bac à sable** : un joystick pour marcher, un pour regarder, et Run, Jump et Fire. Fire lance une balle, et la gâchette droite d'une manette aussi.
- **Les sauvegardes du navigateur ne se perdent plus** : une page qui démarrait cachée, comme un onglet ouvert derrière les autres, ou quittée des yeux pendant le chargement, pouvait écrire dans IndexedDB son dossier encore vide et y effacer les sauvegardes, les réglages et les touches du joueur. L'écriture attend maintenant que le jeu ait relu ce qu'il gardait, et rien n'est écrit quand cette lecture échoue.

## 0.11.0 — 2026-11-18

Les jeux Devex tournent dans un navigateur, en 2D comme en 3D, avec WebGPU : tout ce que dessine la méthode Compatibility, sauf
l'air physique, avec les shaders du projet. Les jeux Android se jouent au doigt, et Devex Script renomme, replie le code et a
plusieurs curseurs. C'est toujours un aperçu.

**Télécharger**

- Windows : `devex-0.11.0-windows-x64-setup.exe`, sans droits administrateur, ou le `.zip` à extraire. L'installateur n'est pas
  signé : si Windows affiche « Windows a protégé votre ordinateur », cliquer sur *Informations complémentaires*, puis *Exécuter quand même*.
- Linux : `devex-0.11.0-linux-x64.tar.gz`, à extraire, puis lancer `bin/devex-editor`. Ubuntu 24.04 ou plus récent.
- Le lecteur web vient avec l'éditeur, dans `platforms/web` : exporter un jeu pour le navigateur ne demande rien de plus,
  sauf Emscripten 7.0.0 pour un jeu dont le C++ fait quelque chose (voir [docs/web.md](docs/web.md)).

**Mettre à jour**

- L'installateur remplace la version déjà installée, au même endroit. Ni les projets ni les réglages n'y sont : ils restent. Un projet
  enregistré avec une version précédente demande confirmation à son ouverture, et ses fichiers sont copiés dans `.devex/backups/` avant
  de changer de format.
- Le code C++ et C# des jeux est recompilé à leur ouverture, et les shaders du projet sont réimportés une fois.

**Nouveautés : le web**

- **Un jeu 2D dans le navigateur** : un renderer WebGPU dessine les sprites, les tilemaps, le texte et l'interface comme Vulkan, et le lecteur web joue le paquet d'un jeu exporté, une image à chaque rafraîchissement de la page.
- **La 3D avec WebGPU** : les maillages, skinnés ou non, avec leurs matériaux, éclairés par le soleil, les lumières locales et l'environnement, sur le ciel, comme Vulkan. Les ombres du soleil et des lumières locales aussi, les lumières 2D sur les sprites, le brouillard de hauteur, les particules et les traînées. Puis le bloom et l'antialiasing temporel. L'air physique (le ciel de l'atmosphère) viendra après la 1.0.
- **Les terrains et l'herbe dans le navigateur** : WebGPU dessine les terrains (blocs à niveaux de détail, couches peintes et par règles, triplanaire, trous), leur ombre et leur herbe comme Vulkan, depuis un code partagé ; les textures des couches y sont rangées dans des tableaux.
- **Les décalcomanies dans le navigateur** : WebGPU pose les décalcomanies (albédo, normales, ORM, émission, fondus) comme Vulkan, depuis un code partagé, leurs textures rangées dans deux atlas comme le fait Godot sur mobile.
- **Les effets d'image dans le navigateur** : la profondeur de champ, le flou de mouvement, l'aberration chromatique et la table de couleurs se dessinent avec WebGPU comme avec Vulkan, depuis un code partagé. Le flou de mouvement y suit la caméra.
- **Les shaders du projet dans le navigateur** : dans un projet en Compatibility, l'import compile aussi les shaders et les graphes de shaders en WGSL, gardé dans le cache comme le SPIR-V, et WebGPU dessine avec eux comme Vulkan les maillages et leurs ombres, les sprites et les tilemaps, les particules, et le ciel, dont il tire la lumière de l'environnement ; jusqu'à huit textures par matériau. Changer de méthode de rendu réimporte les shaders ; un projet en Forward+ ne paie rien. Les sprites éclairés par les lumières 3D se dessinent aussi dans le navigateur, et `TIME` y compte des secondes, comme sur le bureau.
- **Export Web** : **Web** dans la fenêtre d'export, qui produit la page, le lecteur et le paquet du jeu, et un ZIP prêt pour itch.io, et **Run in Browser**, qui sert le jeu sur cet ordinateur et ouvre le navigateur. Un projet doit être en Compatibility ; son C++ est compilé par Emscripten 7.0.0, installé avec emsdk, et lié au moteur dans un lecteur propre au jeu ; le C# ne tourne pas encore dans un navigateur. Dans le navigateur, les sauvegardes, les réglages et les touches du joueur sont gardés dans IndexedDB, et un jeu qui quitte laisse la page dire qu'il est fini.
- **Thread Support pour le web** : cochée dans la fenêtre d'export, elle ajoute le lecteur avec threads, qui charge en arrière-plan et répartit les tâches. La page le prend quand l'hébergeur envoie les en-têtes COOP et COEP (itch.io, case « SharedArrayBuffer support », et Run in Browser), et joue sur un thread ailleurs.
- **Le multijoueur dans le navigateur** : un jeu web rejoint une partie par WebSocket, et l'hôte écoute ENet et WebSocket sur le même port, pour que les joueurs du bureau et ceux du navigateur jouent ensemble. Derrière un proxy comme Caddy, les pages en https (itch.io) se connectent en `wss://`.
- **Un guide pour publier sur le web** : [docs/web.md](docs/web.md) dit ce qu'il faut, comment exporter, essayer et mettre en ligne un jeu pour le navigateur, sur itch.io ou ailleurs, avec ou sans Thread Support, et ce qui diffère encore du bureau.
- **Premier pas vers le web** : le moteur se compile en WebAssembly (préréglage `web`, avec Emscripten), et ses tests de ce que fait un jeu passent dans Node.

**Nouveautés : le tactile et les téléphones**

- **Le multi-touch** : `Input::touches()` en C++ et `Input.Touches` en C# donnent les doigts posés sur l'écran, avec leur place, leur déplacement, et s'ils viennent d'être posés ou levés. Le premier doigt reste la souris tant que **Emulate Mouse From Touch** est cochée, et **Emulate Touch From Mouse** fait de la souris un doigt pour essayer un jeu tactile sur le bureau (Project Settings > Application).
- **Des commandes à l'écran** : **Touch button** tient une action d'entrée tant qu'un doigt est dessus, **Touch joystick** pousse une action vecteur ou axe avec le doigt qui le tire. Ils ne s'affichent que sur un écran tactile (et dans l'éditeur), et un jeu qui lit ses actions n'a rien à changer.
- **La qualité mobile** : dans Project Settings > Rendering, la carte **Android** dit ce que le jeu coupe sur un téléphone (antialiasing temporel, occlusion ambiante, reflets, bloom, effets d'objectif, brouillard volumétrique, ombres des lumières locales) ou s'il y dessine en Compatibility ; **Mobile Preset** coupe tout ce qui est lourd. Le bureau, le web et l'éditeur gardent leur image.

**Nouveautés : Devex Script**

- **Renommer un symbole et couper les lignes dans Devex Script** : **F2** renomme ce qui est sous le curseur partout où le serveur de langage le trouve, en ouvrant les fichiers touchés pour les relire avant de sauvegarder ; **Wrap Lines**, dans Editor Settings > Script Editors, coupe les lignes trop longues au bord du panneau, entre deux mots. Les zones de texte de l'interface des jeux ont la même case, `wrap`.
- **Le repli du code dans Devex Script** : une flèche dans la marge, ou **Alt+F**, replie ce que tiennent des accolades ou ce qui est indenté sous une ligne, et **Edit > Folding** replie ou déplie tout. Les zones de texte des jeux ont la même case, `folding`.
- **Plusieurs curseurs dans Devex Script**, comme dans Godot : **Alt+clic** en ajoute un, **Maj+Alt+Haut/Bas** au-dessus ou en dessous, **Ctrl+D** prend l'occurrence suivante, **Alt** et glisser sélectionne une colonne, **Échap** revient à un seul. Ce qu'on tape se fait à chacun, et s'annule d'un coup. Les zones de texte des jeux les ont aussi.

**Nouveautés : l'éditeur et les projets**

- **Forward+ ou Compatibility**, dans **Project Settings > Rendering** : Compatibility laisse de côté l'occlusion ambiante, les reflets de l'écran et le brouillard volumétrique, dans l'éditeur comme dans le jeu. C'est ce que demande l'export web.
- **Create New Project comme Godot**, dans une vraie fenêtre : nom, chemin avec **Create Folder** et **Browse**, un message en vert ou en rouge, le **renderer**, **Version Control Metadata** (Git écrit un `.gitignore` et un `.gitattributes`) et **Edit Now**. Le dossier proposé est celui du dernier projet, ou Documents.
- **Des icônes nettes** : chaque icône de l'éditeur est dessinée à la taille exacte où elle s'affiche, sur des pixels entiers, comme Godot dessine ses SVG. Elles étaient dessinées une fois en 64 pixels puis réduites, ce qui les rendait floues.
- **Editor Settings > Export** : les chemins du SDK Android, du NDK, de Java et d'Emscripten. Vides, l'éditeur trouve les outils seul et montre ce qu'il a trouvé ; choisis, ils passent avant tout, et l'export en ligne de commande les prend aussi.
- **Les add-ons en zip** : **Install...** et **Update...** prennent un `.zip` ou le `addon.dvxaddon` d'un dossier, **Export...** écrit un `.zip`. Le manifeste dit la version de Devex qu'il demande (une plus ancienne refuse de l'installer), ses dépendances (listées, en rouge celles qui manquent), sa catégorie et son icône.
- **Les traductions en PO** : le panneau Translations exporte une table pour les traducteurs, un `.pot` et un `.po` par langue (la clé dans `msgctxt`), et remplit la colonne d'une langue depuis le `.po` qu'ils rendent. Un `.po` posé dans le projet est lu comme une table.
- **Bake to Mesh** pour les blocs CSG : le solide est écrit en `.dvxmesh`, avec ses matières, et une entité le dessine à sa place avec sa collision.

## 0.10.1 — 2026-11-07

Des corrections, les licences, et la première version qu'une version précédente annonce d'elle-même : installée, la 0.10.0 la
signale sur sa liste des projets. C'est toujours un aperçu.

**Télécharger**

- Windows : `devex-0.10.1-windows-x64-setup.exe`, sans droits administrateur, ou le `.zip` à extraire. L'installateur n'est pas
  signé : si Windows affiche « Windows a protégé votre ordinateur », cliquer sur *Informations complémentaires*, puis *Exécuter quand même*.
- Linux : `devex-0.10.1-linux-x64.tar.gz`, à extraire, puis lancer `bin/devex-editor`. Ubuntu 24.04 ou plus récent.

**Mettre à jour**

- L'installateur remplace la version déjà installée, au même endroit. Ni les projets ni les réglages n'y sont : ils restent. Un projet
  enregistré avec une version précédente demande confirmation à son ouverture, et ses fichiers sont copiés dans `.devex/backups/` avant
  de changer de format.
- Le code C++ et C# des jeux est recompilé à leur ouverture.

**Nouveautés**

- **Quand Devex plante, il le dit** : une fenêtre explique ce qui s'est passé et où est le rapport, et son bouton **Open Log Folder** ouvre le dossier. Une copie du travail non enregistré, s'il y en a une, est proposée à la prochaine ouverture du projet.
- Décocher **Release Key** dans l'export Android ne ferme plus l'éditeur.
- **Les licences voyagent avec Devex** : un dossier `bin/licenses` contient celle de chaque bibliothèque, police et outil livrés, et le bouton **Licenses** de la fenêtre **About** l'ouvre. Chaque jeu exporté emporte les siennes, dans un dossier `licenses` à côté de lui, ou dans `assets/licenses` sur Android : à garder avec le jeu quand on le partage.

## 0.10.0 — 2026-11-06

Une version pour essayer la mise à jour de Devex par-dessus la précédente, et les fenêtres qui ressemblent à celles de Godot. C'est
toujours un aperçu.

**Télécharger**

- Windows : `devex-0.10.0-windows-x64-setup.exe`, sans droits administrateur, ou le `.zip` à extraire. L'installateur n'est pas
  signé : si Windows affiche « Windows a protégé votre ordinateur », cliquer sur *Informations complémentaires*, puis *Exécuter quand même*.
- Linux : `devex-0.10.0-linux-x64.tar.gz`, à extraire, puis lancer `bin/devex-editor`. Ubuntu 24.04 ou plus récent.

**Mettre à jour**

- L'installateur remplace la version déjà installée, au même endroit. Ni les projets ni les réglages n'y sont : ils restent. Un projet
  enregistré avec une version précédente demande confirmation à son ouverture, et ses fichiers sont copiés dans `.devex/backups/` avant
  de changer de format.
- Le code C++ et C# des jeux est recompilé à leur ouverture.

**Nouveautés**

- **Un avis de nouvelle version** sur la liste des projets : une fois par jour au plus, Devex demande à GitHub s'il existe une version plus récente, et un bouton à côté du numéro de version ouvre sa page. Rien des projets n'est envoyé, et sans connexion il ne dit rien. **Editor Settings > Network > Check for Updates** le désactive. La 0.9.0 n'a pas ce code : c'est la version suivante de celle-ci qui s'annoncera la première.
- **De vraies fenêtres** : Project Settings, Editor Settings, l'export et le débogage C# s'ouvrent dans une fenêtre du système, avec ses boutons, qu'on déplace et redimensionne comme les autres. Les petits dialogues restent dans l'éditeur pour l'instant.
- **Project Settings et Editor Settings comme Godot** : un arbre de catégories à gauche, les réglages sur deux colonnes à droite. **Description** et **Version** du jeu, et pour sa fenêtre **Mode** (fenêtré, plein écran sans bords, plein écran exclusif), **Resizable**, **Borderless** et **V-Sync Mode** (Disabled, Enabled, Adaptive, Mailbox).
- **Une fenêtre d'export plus propre** : la liste des plateformes à gauche, les onglets **Options** et **Resources**, les problèmes en rouge en bas, et **Export All**, qui exporte pour chaque plateforme qui a ce qu'il lui faut.
- Des flèches fines pour déplier, comme dans Godot.
- **Help > Copy System Report** dans l'éditeur, et `devex-editor --report` quand il ne démarre pas : le système, les GPU et ce qu'ils offrent, les outils trouvés et les derniers problèmes du journal, à joindre à un bug. Rien ne part tout seul.
- **Plus de terminal par défaut** sous Windows : le journal est dans le panneau Output. **Editor Settings > Compilation > Terminal** en montre un à côté de l'éditeur, et **Terminal** dans la fenêtre d'export en ouvre un avec le jeu. Pour les scripts, `devex-editor-console` attend la fin de la commande et rend son code de sortie.
- **Exporter pour l'autre système** : la fenêtre d'export propose Windows, Linux et Android. Depuis Windows, un jeu pour Linux sort en `.tar.gz`, à extraire sous Linux. Depuis Linux, un jeu pour Windows sort en dossier, avec son icône. Le moteur de l'autre système vient avec Devex, ce qui ajoute environ 10 Mo à l'installateur et à l'archive Linux. Le code C# suit, et le premier export télécharge .NET pour ce système. Le code C++ ne se compile que sur le système où il tourne : le module d'un nouveau projet, qui n'enregistre rien, est laissé de côté, et un jeu dont le C++ fait quelque chose est refusé en le disant.
- Sans synchro verticale, un jeu n'est plus bloqué au rythme de l'écran sous Wayland, comme sur Ubuntu récent : `immediate` n'y existe pas, et le moteur retombait sur FIFO au lieu de `mailbox`.
- Un modèle de bug sur GitHub, et [une page](docs/essayer-sur-une-machine.md) pour essayer Devex sur une machine.
- Si l'éditeur ne peut pas démarrer, il le dit dans une fenêtre, comme les jeux, avec le conseil de mettre à jour le pilote graphique quand Vulkan manque. Sous Linux, ce message apparaît aussi quand le programme est lancé depuis le bureau.

- Une mise à jour de l'installateur remplace aussi le moteur de l'autre système, dans `platforms/`.

## 0.9.0 — 2026-11-06

La première version publiée de Devex : un moteur de jeu 2D et 3D en C++23 sur Vulkan, avec son éditeur. C'est un aperçu : il
sert à faire de vrais petits jeux et à trouver ce qui manque avant la 1.0.

**Télécharger**

- Windows : `devex-0.9.0-windows-x64-setup.exe`, sans droits administrateur, ou le `.zip` à extraire. L'installateur n'est pas
  signé : si Windows affiche « Windows a protégé votre ordinateur », cliquer sur *Informations complémentaires*, puis *Exécuter quand même*.
- Linux : `devex-0.9.0-linux-x64.tar.gz`, à extraire, puis lancer `bin/devex-editor`. Ubuntu 24.04 ou plus récent.

**Il faut**

- Un pilote graphique avec Vulkan 1.3.
- Pour le code C# des jeux : le [SDK .NET 10](https://dotnet.microsoft.com/download).
- Pour leur code C++ : Visual Studio (outils C++), CMake et Ninja sous Windows ; `cmake`, `ninja-build` et `g++-14` sous Linux.

**Ce qu'il y a dedans**

- Un éditeur à la manière de Godot : scènes en onglets, inspecteur, gizmos, annuler et rétablir, éditeur de code avec LSP pour C# et C++, Play dans un processus séparé.
- Le rendu : forward+ PBR, ombres en cascades, sondes de réflexion, réflexions en espace écran, éclairage indirect précalculé, brouillard volumétrique, décals, TAA.
- La 2D : sprites, tuiles, physique Box2D. La 3D : physique Jolt, terrains sculptés, animations avec graphes d'états, navigation.
- Le gameplay en C# ou en C++, avec rechargement à chaud.
- Les outils de level design : chemins, blocs CSG, grilles de pièces.
- Le multijoueur en réseau local ou par adresse IP, avec réplication et RPC.
- L'export des jeux pour Windows et Linux, et en aperçu pour Android.
- Les addons locaux, les traductions et les sauvegardes des jeux.

**Nouveau pour protéger le travail**

- Toutes les 5 minutes et avant chaque Play, l'éditeur copie ce qui n'est pas enregistré. Après un plantage, il propose de le récupérer.
- Les projets disent quelle version les a enregistrés : les plus anciens sont copiés avant d'être repris, et les plus récents demandent confirmation.
- Les fichiers des versions passées restent lisibles, et des tests le vérifient à chaque changement.

**Nouveau pour les jeux**

- `Time.Scale` en C# et `Application::setTimeScale` en C++ : 0 met le jeu en pause et l'interface continue de répondre, entre 0 et 1 il ralentit.
- `Entity.TryGet` marche aussi sur les composants écrits en C#.
- `Input.MouseMode` en C# et `Window::setMouseMode` en C++ : visible, caché, capturé, ou confiné dans la fenêtre.
- Pour un écran d'options en C# : `PlayerSettings.WindowMode` (fenêtré, sans bords, plein écran), `MaxFrameRate` et `SetWindowSize`, `Screen.FrameRate` et `UsableSize`, et `PlayerSettings.VSync` qui s'applique tout de suite. Avec la synchro verticale, l'écran décide du rythme.
- Les personnages 2D et 3D suivent ce qui bouge sous leurs pieds, même un collider déplacé par un script, et en 2D une plateforme à sens unique qui monte.

**Corrigé en faisant de vrais jeux**

- Un bouton caché gardait le focus du clavier, et Entrée pouvait encore le presser.
- Un écran caché au-dessus d'un menu lui prenait le clavier.
- Une palette de grille d'une seule pièce avec un enfant faisait disparaître le sol sans un mot.
- Un projet ouvert pour la première fois démarre sur sa scène de démarrage.
- Quand le code de jeu ne compile pas, l'Output montre tout ce que dit CMake, et une erreur du compilateur passe avant le nom de l'étape qui a échoué.

**Changé dans l'éditeur**

- Play lance la Startup Scene de **Project Settings** quand elle est définie, même si une autre scène est ouverte, et la scène courante sinon.

**Limites connues**

- Testé surtout sur un GPU de bureau NVIDIA. Les GPU intégrés, AMD et Intel restent à essayer.
- Le multijoueur se limite au réseau local et aux adresses IP : pas de serveurs relais.
- L'export Android reste un aperçu, sans C#.
- Sous Windows, le code C++ d'un jeu demande un chemin de projet d'environ 130 caractères au plus. Au-delà, l'éditeur le dit et demande de rapprocher le projet de la racine du disque.
- La documentation est en français.
