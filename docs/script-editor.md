# Devex Script : édition et services de langage

Devex Script édite les fichiers C#, C++, CMake, JSON et `.dvx*` dans l'écran
**Script**. Le C# et le C++ peuvent utiliser un serveur de langage externe via
LSP pour analyser le projet et le texte en cours de saisie.

## Ouvrir et configurer

Dans FileSystem, sélectionner le fichier puis **Edit in Devex Script** dans
l'inspecteur. Ce bouton ouvre toujours l'éditeur intégré, même si le double-clic
est configuré pour Rider ou un autre IDE. **File > Open…** et **Ctrl+O** ouvrent
aussi un fichier texte.

Dans **Editor > Editor Settings > Script Editors**, les cartes **C# Language
Server** et **C++ Language Server** règlent séparément :

- **Enabled** : activer l'analyse LSP de ce langage ;
- **Executable** : chemin du serveur ; vide, Devex le détecte automatiquement ;
- **Arguments** : arguments du serveur, séparés comme une ligne de commande,
  avec des guillemets pour les chemins contenant des espaces ;
- **Restart Language Servers** : arrêter les sessions, relancer la détection et
  reconnecter les fichiers ouverts.

Les arguments sont transmis directement au programme, sans shell. Le serveur
doit parler LSP sur son entrée et sa sortie standard. Les réglages sont propres
à l'utilisateur et sont sauvegardés automatiquement. Le choix de l'IDE externe
et celui du serveur de langage restent indépendants.

La barre au bas du texte indique le langage et l'état de la connexion. Un
serveur absent, désactivé ou arrêté laisse disponibles la coloration syntaxique,
la recherche, l'édition et la complétion locale des mots-clés et identifiants.

## C# : csharp-ls et Game.csproj

Le serveur détecté par défaut est
[csharp-ls](https://github.com/razzmatazz/csharp-language-server). Avec le SDK
.NET requis par sa version, une installation globale se fait avec :

```powershell
dotnet tool install --global csharp-ls
```

Devex recherche notamment `csharp-ls.exe` sous Windows, ou `csharp-ls` sous
Linux, dans `~/.dotnet/tools`, dans le `PATH` et dans le dossier
`language-servers` à côté de son exécutable. Une installation portable est aussi
possible, en remplaçant le chemin ci-dessous par celui du binaire utilisé :

```powershell
dotnet tool install csharp-ls --tool-path "D:/Devex Engine/out/build/x64-debug/bin/language-servers"
```

Après l'installation, utiliser **Restart Language Servers**. Devex n'installe
pas automatiquement ces outils et n'ajoute aucun paquet NuGet au jeu pour LSP.

Le serveur démarre dans le dossier `code/` du projet et charge **Game.csproj**.
Ce projet, généré par Devex, référence `Devex.Managed.dll` et les vues C# des
composants natifs. C'est ce contexte qui permet de proposer, par exemple,
`Entity.IsValid` ou `Entity.Name`, plutôt que seulement les mots présents dans
le fichier. Ouvrir le projet dans Devex et compiler le code une première fois
avec **Ctrl+B** prépare ces fichiers et restaure les dépendances .NET.

## C++ : clangd et les commandes de compilation

Installer [clangd](https://clangd.llvm.org/installation) ou indiquer le chemin
d'un autre serveur LSP C++ compatible. La détection cherche `clangd` dans le
`PATH`, le dossier `language-servers` du binaire et, sous Windows, dans
`Program Files/LLVM/bin`.

Devex transmet à clangd le dossier du `compile_commands.json` du cache natif
actif de la configuration du moteur utilisée, sous `.devex/code/`. **Ctrl+B** produit ce
contexte de compilation à partir du `code/CMakeLists.txt` généré par le moteur.
Si une reconstruction déplace la base de compilation, Devex reconnecte la
session C++ automatiquement. Sans base, clangd reçoit des options de secours
C++23 et les chemins de headers Devex disponibles près du moteur ; une vraie
compilation reste nécessaire pour les macros et dépendances propres au jeu.

## Écrire, comprendre et naviguer

| Action | Raccourci ou geste |
| --- | --- |
| Proposer une complétion | Automatique après une courte pause, ou **Ctrl+Espace** |
| Choisir une proposition | **Haut/Bas**, puis **Tab** ou **Entrée**, ou clic |
| Fermer les propositions | **Échap** |
| Aller à la définition | **F12** ou **Ctrl+clic** sur le symbole |
| Trouver les références | **Maj+F12** |
| Informations sur un symbole | Survol, ou **Ctrl+I** au curseur |
| Formater le document | **Alt+Maj+F** |
| Afficher les problèmes | Bouton **Problems** ou **Ctrl+Maj+M** |
| Rechercher / remplacer | **Ctrl+F** / **Ctrl+H** |
| Aller à une ligne | **Ctrl+G** |
| Commenter / décommenter | **Ctrl+K** |
| Sauvegarder / annuler | **Ctrl+S** / **Ctrl+Z** |

La complétion LSP utilise le type du symbole et les références du projet.
Les propositions remplacent la portion indiquée par le serveur, y compris la
fin d'un identifiant déjà saisi. Les imports supplémentaires et le formatage
modifient le **texte en mémoire** ; une annulation revient sur l'opération et
la sauvegarde reste explicite. Si un serveur propose un snippet, Devex insère
son texte avec ses valeurs par défaut, sans navigation entre placeholders.

Les erreurs et avertissements apparaissent dans la marge et sur les plages de
texte concernées. Le survol en donne le message. **Problems** rassemble les
diagnostics des fichiers ouverts, avec les erreurs de la dernière compilation
pour les langages sans connexion LSP. Un clic sur une ligne ouvre le fichier au
bon emplacement. Une recherche de références utilise le même panneau ; cliquer
sur **References** (ou **Ctrl+Maj+M**) revient aux diagnostics.

Les définitions et références ouvrent les fichiers locaux fournis par le
serveur. Devex ne décompile pas encore les définitions dans les assemblies .NET
ni les emplacements virtuels qu'un serveur expose avec une URI autre que
`file://`.

## Analyse, sauvegarde et hot reload

L'analyse LSP reçoit les modifications non sauvegardées avec une cadence
limitée, dans un processus séparé. Elle ne sauvegarde pas le script, ne compile
pas la DLL de gameplay et ne déclenche pas de hot reload. Les réglages existants
de **Compilation** décident toujours de la reconstruction du jeu à la
sauvegarde ; **Ctrl+B** reste la commande de compilation manuelle.

Une réponse de complétion ou de formatage correspondant à un ancien texte est
ignorée. Les diagnostics portant une ancienne version sont écartés. Fermer un
fichier le retire de la session ; changer de projet ou quitter l'éditeur arrête
ses processus de langage. Une panne du serveur apparaît dans la barre d'état,
sans boucle de redémarrage automatique : utiliser **Restart Language Servers**.

Les échanges et leur lecture sont bornés à chaque mise à jour pour ne pas
bloquer l'interface. Les positions LSP en UTF-16 sont converties en offsets
UTF-8 du champ, y compris en présence d'accents ou d'emoji.

## Périmètre actuel

La coloration et la liste de méthodes utilisent encore les analyseurs locaux
de Devex. LSP ajoute complétion, diagnostics, informations au survol,
définitions, références et formatage selon les capacités du serveur. Les code
actions, le renommage sémantique, la coloration par semantic tokens, le repli
du code et les curseurs multiples ne sont pas encore implémentés. CMake, JSON,
shaders et fichiers `.dvx*` conservent leurs outils locaux.

Les serveurs sont des outils de développement facultatifs : ils ne sont pas
nécessaires pour lancer ou exporter le jeu.
