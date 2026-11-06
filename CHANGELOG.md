# Journal des versions

Ce qui change d'une version publiée à l'autre. Ce que promet chaque version aux projets existants est dans
[docs/compatibilite.md](docs/compatibilite.md). Les décisions et leurs raisons sont dans [docs/decisions.md](docs/decisions.md).

## À venir

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
