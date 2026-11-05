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

**Limites connues**

- Testé surtout sur un GPU de bureau NVIDIA. Les GPU intégrés, AMD et Intel restent à essayer.
- Le multijoueur se limite au réseau local et aux adresses IP : pas de serveurs relais.
- L'export Android reste un aperçu, sans C#.
- La documentation est en français.
