# Publier un jeu sur le web

Devex exporte un jeu pour le navigateur : une page, le lecteur du jeu en WebAssembly et son paquet, dessinés avec
WebGPU. Ce guide dit ce qu'il faut, comment exporter, essayer et mettre en ligne, et ce que le navigateur change. Les
raisons des choix sont dans [decisions.md](decisions.md#le-web--plan-décidé-le-2026-11-14).

## Ce qu'il faut

- **Un projet en Compatibility**, dans **Project Settings > Rendering > Rendering Method** : c'est ce que le web dessine.
  Un projet en Forward+ est refusé en rouge par l'export. Compatibility laisse de côté l'occlusion ambiante, les reflets
  de l'écran et le brouillard volumétrique, dans l'éditeur comme dans le jeu, pour qu'on voie le jeu tel qu'il sera.
  Changer de méthode réimporte les shaders du projet, qui se compilent alors aussi pour le navigateur.
- **Rien d'autre pour un jeu sans C++** : le lecteur web vient avec l'éditeur, dans `platforms/web`, comme les modèles
  d'export de Godot.
- **Emscripten 7.0.0 pour un jeu dont le C++ fait quelque chose** : l'export compile ce code et le lie au moteur dans un
  lecteur propre au jeu. On l'installe avec emsdk, comme le NDK pour Android :

  ```bash
  git clone https://github.com/emscripten-core/emsdk.git
  cd emsdk
  ./emsdk install 7.0.0
  ./emsdk activate 7.0.0
  ```

  (`emsdk.bat` sous Windows.) L'éditeur le trouve par la variable `EMSDK` ou dans les dossiers habituels d'emsdk, ou là
  où **Editor Settings > Export** le désigne. Une autre version est refusée en rouge, avec la commande qui installe la
  bonne : le moteur livré a été construit avec celle-là.
- **Un navigateur qui a WebGPU** : les versions récentes de Chrome, Edge, Firefox et Safari (le détail par système est
  sur [caniuse.com/webgpu](https://caniuse.com/webgpu)). Sans WebGPU, la page le dit : *This browser has no WebGPU: the
  game cannot be drawn.*

## Exporter

1. Ouvrir **Project > Export Game...** et choisir **Web** dans la liste de gauche.
2. **Export Path** est `export/web` par défaut : le dossier reçoit `index.html`, le lecteur (`devex-player.js` et
   `.wasm`), `game.dvxpak` et les licences. À côté, `<Jeu>-web.zip` contient la même chose, la page à sa racine, prêt
   pour itch.io.
3. Cocher **Thread Support** au besoin (voir plus bas).
4. **Export Project** exporte ; **Run in Browser** exporte puis sert le dossier sur cet ordinateur et ouvre le
   navigateur par défaut. Le serveur ne répond qu'à `localhost`, jamais au réseau.

Les problèmes s'écrivent en rouge en bas de la fenêtre : méthode de rendu, Emscripten absent ou d'une autre version.
Le C# est signalé : il ne tourne pas encore dans un navigateur, et ses composants n'y font rien.

La même chose sans fenêtre, avec les réglages d'export enregistrés dans le projet :

```powershell
out/build/x64-release/bin/devex-editor-console.exe --export <Jeu>.dvxproj --platform web
```

Une page ouverte depuis le disque (`file://`) ne charge pas son WebAssembly : pour essayer un export, passer par **Run
in Browser** ou par un serveur.

## Thread Support

Décochée, le jeu tourne sur un seul thread, chez n'importe quel hébergeur. Cochée, l'export ajoute un second lecteur
avec threads, qui charge en arrière-plan et répartit le travail du moteur ; la page le choisit au chargement quand le
navigateur le permet, c'est-à-dire quand l'hébergeur envoie ces en-têtes :

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Sinon, elle prend le lecteur sur un thread : le jeu marche partout, plus vite là où les en-têtes sont. itch.io les
envoie quand la case **SharedArrayBuffer support** est cochée ; Run in Browser les envoie toujours ; GitHub Pages ne
les envoie pas. Le C++ d'un jeu a ses threads lui aussi, par un second kit construit avec eux.

## Mettre en ligne

Sur **itch.io** : créer le projet avec **Kind of project : HTML**, envoyer `<Jeu>-web.zip`, cocher **This file will be
played in the browser**, puis régler la taille de la fenêtre du jeu (la taille de la fenêtre du projet, par exemple
1280 × 720) et, au choix, le bouton de plein écran. Cocher **SharedArrayBuffer support** si l'export a Thread Support.

**Ailleurs**, n'importe quel hébergeur de fichiers statiques convient : y déposer le contenu du dossier exporté. Le
WebAssembly doit partir avec le type `application/wasm`, ce que font les serveurs courants. Une page en https ne peut
ouvrir que des WebSockets chiffrés, ce qui compte pour le multijoueur (voir plus bas).

## Ce que le navigateur change

- **Le son, le plein écran et la souris** : le jeu démarre tout de suite, mais le navigateur ne laisse jouer le son
  qu'après un premier clic ou une touche, et n'accorde le plein écran et la souris capturée qu'en réponse au clic ou à
  la touche suivants.
- **Les sauvegardes, les réglages et les touches du joueur** sont gardés dans IndexedDB, une base par jeu, nommée par
  son dossier, même quand itch.io sert tous les jeux du même domaine. Ils sont écrits après chaque changement et quand
  la page se cache. Effacer les données du site les efface.
- **Quitter le jeu** l'arrête, et la page dit *The game has ended.* à la place du canvas.
- **Les textures** partent compressées en BC, que lisent les navigateurs des ordinateurs ; un navigateur sans BC, sur
  un téléphone, les reçoit décodées, ce qui prend plus de mémoire.
- **Sur un écran tactile**, SDL transmet les doigts du navigateur comme ceux d'Android : `Input::touches()` les donne,
  et le premier fait la souris tant que **Emulate Mouse From Touch** est cochée. Les commandes à l'écran, **Touch
  button** et **Touch joystick**, s'affichent dès le chargement.
- **Sur un téléphone ou une tablette**, le jeu dessine comme sur Android : la carte **Mobile** de Project Settings >
  Rendering coupe les effets lourds (par défaut, tous), et le navigateur dessine les pixels de la page plutôt que tous
  ceux de l'écran, deux ou trois fois plus nombreux, avec des ombres moitié moins fines. Le son y part par paquets plus
  longs, pour ne pas grésiller quand une image tarde.
- **Le multijoueur** : un jeu dans un navigateur rejoint une partie, mais ne l'héberge pas. Il passe par un WebSocket,
  que l'hôte ouvre sur le même port qu'ENet ; depuis une page en https, il faut un proxy qui chiffre, comme Caddy :
  voir [Jouer depuis un navigateur](multiplayer.md#jouer-depuis-un-navigateur).

## Ce qui diffère encore du bureau

- **Le C#** ne tourne pas encore dans un navigateur ; le C++ et les scènes, si.
- **L'air physique** (le ciel de l'atmosphère, le disque du soleil, le voile du lointain) vient après la 1.0 : le
  navigateur montre la texture ou la couleur du ciel de l'environnement, et le brouillard de hauteur.
- **Les shaders du projet** lisent au plus huit textures par matériau, que WebGPU lie une à une ; au-delà, l'import le
  dit par un avertissement « for the web », et leurs matériaux restent standard dans le navigateur.
- **Le flou de mouvement et l'antialiasing temporel** suivent la caméra, pas un objet qui bouge seul : le navigateur
  reconstruit le mouvement à partir de la profondeur.
- **Les décalcomanies** sont rangées dans des atlas sans mips : de loin, elles scintillent un peu plus.
- **Sans Thread Support**, ou chez un hébergeur sans les en-têtes, tout le moteur tourne sur un thread : une scène
  lourde y est plus lente que sur le bureau.
