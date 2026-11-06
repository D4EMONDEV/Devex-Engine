# Essayer Devex sur une machine

Devex a surtout tourné sur un PC de bureau avec un GPU NVIDIA. Essayer une autre machine est ce qui peut le plus l'aider : un GPU
intégré, AMD ou Intel, ou un vrai bureau Linux. Cette page dit quoi faire, en une demi-heure environ, et quoi renvoyer.

| Machine | Système | État |
| --- | --- | --- |
| RTX 4060 | Windows 11 | Essayée : les deux jeux de démonstration, avec le mode sans bords |
| lavapipe, sans GPU | Linux, sur la CI | Les tests graphiques, sans fenêtre visible |
| Intel Iris Xe, GPU intégré | Ubuntu 26.04 | **À essayer** |
| AMD | | **Personne encore** |

## Installer

Prendre `devex-<version>-linux-x64.tar.gz` ou l'installateur Windows sur la page [Releases](https://github.com/D4EMONDEV/Devex-Engine/releases),
ou le dernier état du dépôt dans l'onglet *Actions* : ouvrir une exécution verte de `main`, puis télécharger `devex-linux-x64` ou `devex-windows-x64`.

Sous Ubuntu, Vulkan et ce qui construit le code des jeux :

```bash
sudo apt install libvulkan1 mesa-vulkan-drivers vulkan-tools
```

```bash
sudo apt install cmake ninja-build g++-14 dotnet-sdk-10.0
```

Si un paquet n'existe pas sous ce nom, le noter : c'est déjà un résultat.

## Le rapport

Il dit le système, les GPU et ce qu'ils offrent, les outils trouvés, et les derniers problèmes du journal. **Rien ne part tout seul** : relire le rapport avant de l'envoyer, car les lignes du
journal peuvent contenir des chemins de fichiers.

- Dans l'éditeur : **Help > Copy System Report** le copie, et l'écrit dans le dossier `logs` de l'éditeur.
- Si l'éditeur ne démarre pas, depuis un terminal, dans le dossier extrait (`bin\devex-editor-console --report rapport.txt` sous Windows) :

```bash
bin/devex-editor --report rapport.txt
```

La ligne **Can run Devex** dit, GPU par GPU, s'il convient et sinon pourquoi. Sous Linux, **XDG_SESSION_TYPE** dit si le bureau utilise Wayland ou X11.

## Quoi essayer

Noter ce qui ne va pas, même si c'est petit, et les FPS affichés en bas de l'éditeur.

1. **Démarrer** l'éditeur, en double-cliquant si possible, pas seulement depuis un terminal.
2. **Créer un projet** 3D, y mettre un cube et une lumière, enregistrer.
3. **Bouger la vue**, et passer en 2D et revenir.
4. **Écrire un script C#** depuis l'éditeur, et attendre *Code ready*. Puis un script C++ s'il y a de quoi le construire.
5. **Play**, puis Stop. Fermer aussi la fenêtre du jeu à la main.
6. **La fenêtre du jeu** : la redimensionner, la réduire puis la rouvrir, passer à une autre fenêtre et revenir. Une image figée ou une attente sans fin est à signaler.
7. **Les modes** fenêtré, sans bords et plein écran, si le jeu les propose.
8. **Le son et une manette**, s'il y en a une.
9. **Exporter** le projet (**Project > Export Game...**), lancer le jeu exporté depuis le gestionnaire de fichiers.
10. **Un jeu venu de l'autre système** : sous Windows, **Project > Export Game...** avec **Linux** choisi dans sa liste de gauche donne un `.tar.gz`. L'extraire sur la machine Linux et lancer le jeu.
11. **Une vraie charge** : ouvrir un jeu déjà fait, copié sur la machine avec son dossier, ou `samples/sandbox` du dépôt, et noter les FPS. Sur un GPU intégré, c'est là que le manque de marge se voit.

## Renvoyer

Ouvrir un [rapport de bug](https://github.com/D4EMONDEV/Devex-Engine/issues/new?template=bug.yml) avec le rapport et la liste de ce qui n'a pas marché. Même si tout marche, le rapport
est utile : il permet de remplir le tableau ci-dessus.
