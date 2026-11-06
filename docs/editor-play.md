# Tester son jeu dans une fenêtre séparée

**Play (F5)** lance la **Startup Scene** de **Project Settings** quand elle est définie, même si une autre scène est
ouverte, et la scène courante sinon. Par défaut, il la lance dans `devex-player`, dans un **processus séparé**
avec sa propre fenêtre. La simulation C++/C#, la physique, l'audio et le rendu du jeu s'exécutent
dans ce processus. L'éditeur reste disponible pour travailler sur ses scènes et ses scripts.
Un plantage ou un blocage de cette partie ne détruit pas la scène éditée.

Le lancement utilise une copie temporaire de cette scène, **modifications non enregistrées
comprises**. Il n'enregistre pas ces modifications dans le fichier de scène et ne remplace pas
la scène de démarrage du projet. Une scène qui n'a pas encore de fichier peut donc être testée.
Ses ressources doivent avoir fini d'être importées, et son code doit avoir une compilation valide.
La fenêtre du jeu reprend les réglages du projet : taille, mode de fenêtre, synchronisation et icône.

## Commandes

| Dans l'éditeur | Effet sur la fenêtre de jeu |
| --- | --- |
| **F5** ou Play | Lance une partie ; un deuxième lancement est empêché tant qu'elle tourne. |
| **F7** ou Pause | Suspend/reprend la simulation, les animations et l'audio. |
| **F9** ou Step | Avance d'un pas physique lorsque la partie est en pause. |
| **F8** ou Stop | Demande la fermeture du jeu. Un processus qui ne répond plus est terminé après deux secondes. |

Les raccourcis de l'éditeur agissent quand **sa fenêtre a le clavier**. Cliquer dans la fenêtre
du jeu pour jouer ; revenir dans l'éditeur pour utiliser ses raccourcis. Fermer directement la
fenêtre du jeu termine aussi la partie et libère les commandes Play de l'éditeur.
Fermer l'éditeur ou changer de projet arrête son processus de jeu.

Les changements faits dans l'éditeur restent ceux du document en cours. Pour rejouer la scène
modifiée, arrêter puis relancer Play. Les compilations C++ et C# restent pilotées par l'éditeur ;
le lecteur suit les nouvelles bibliothèques comme lorsqu'il est lancé de façon autonome.

## Output et débogage

Les messages du jeu arrivent dans **Output** avec leur catégorie et leur niveau (information,
avertissement, erreur). Les messages sur plusieurs lignes et les chemins entre guillemets sont
transmis sans casser le canal de contrôle. Un arrêt anormal indique le code de sortie et le
dossier de ses journaux, conservés sous `.devex/play/<identifiant>/logs/`. Une partie terminée
normalement supprime sa copie temporaire. Ce cache ne se versionne pas.

Pour le C#, ouvrir **Project > C# Debugging…**. En mode séparé, démarrer Play puis attacher Rider,
Visual Studio ou VS Code à **`devex-player` et au PID affiché**, ou activer l'attente du débogueur
avant Play. Les scripts de démarrage attendent alors l'attachement ; Stop permet d'abandonner
l'attente. Le débogueur C++ peut également s'attacher au PID écrit dans Output au lancement.

## Garder la vue de jeu intégrée

Dans **Editor > Editor Settings > Compilation > Play**, régler **Run Game** sur :

- **Separate Process** : fenêtre et processus propres au jeu, mode par défaut.
- **Inside Editor** : copie jouée dans l'éditeur, avec ses vues Scene/Game et son inspection en direct.

En mode séparé, l'inspecteur, les outils de scène et le profiler de l'éditeur suivent son document
et son propre rendu. L'option Inside Editor permet de continuer à inspecter directement la copie
en cours de jeu. Ce choix est un réglage personnel de l'éditeur, enregistré pour les projets suivants.
Les applications C++ qui placent leur logique dans une sous-classe de `Application` de l'éditeur
utilisent aussi Inside Editor pour ces callbacks. Le processus séparé exécute les composants et
systèmes du module de jeu du projet.

Pour une compilation locale, la cible `devex_editor` construit aussi `devex_player` lorsque les
deux cibles sont activées. Un moteur installé doit garder les deux exécutables ensemble dans
`bin/`. Lancer le jeu depuis Play ne demande pas de faire un export.

## Tester à plusieurs

La petite flèche à côté de **Play** choisit combien de jeux il lance, de 1 à 4. Chacun a son processus et sa
fenêtre, et leur titre dit quel joueur ils sont. Le choix est personnel et se garde d'une fois à l'autre.

- Les fenêtres se partagent l'écran de l'éditeur : deux côte à côte, trois ou quatre en grille, même si le
  projet est en plein écran.
- **Connect them**, coché par défaut : le premier héberge sur un port libre, et les autres le rejoignent avant
  leur `Start`. `Multiplayer.Host` et `Multiplayer.Join` réussissent alors sans rien changer, et le code du jeu
  reste celui de la vraie partie. Décoché, c'est au jeu de se connecter, pour essayer son vrai menu ;
  `Multiplayer.TestInstance` lui dit lequel il est (1, 2...), et vaut 0 hors de ce mode.
- **Pause**, **Step** et **Stop** agissent sur tous. Une partie en pause reste dans la session. Fermer une
  fenêtre n'arrête que ce jeu, et Play se termine avec le dernier. Fermer celui qui héberge déconnecte les autres.
- **Output** réunit leurs journaux, préfixés par `[Player 2]`. Le débogueur C# vise le premier.
- À partir de deux joueurs, Play lance toujours des processus séparés, même réglé sur **Inside Editor**.

Chacun démarre quand le précédent est prêt. Tous partagent les sauvegardes et les réglages du joueur, et tous
jouent le son.
