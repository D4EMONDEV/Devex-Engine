# Journal des versions

Ce qui change d'une version publiée à l'autre. Ce que promet chaque version aux projets existants est dans
[docs/compatibilite.md](docs/compatibilite.md). Les décisions et leurs raisons sont dans [docs/decisions.md](docs/decisions.md).

## 0.9.0 — à publier

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
