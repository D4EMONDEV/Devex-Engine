# Dessiner un niveau

## La grille

Un **GridMap** pose des pièces sur une grille 3D : des pièces, des couloirs et un décor faits de modules, comme le GridMap de
Godot. Une pièce n'est pas une entité, ce qui permet d'en poser des milliers.

- **La palette** : une scène ordinaire. Chacune de ses entités du haut est une pièce (une seule entité du haut sans mesh ne fait que les tenir, et les pièces sont alors dessous), avec ses meshes, ceux de ses enfants compris,
  et ses matières. Sa position dans la palette ne compte pas, ce qui laisse la place de les ranger. Une pièce se pose à sa base, au
  milieu de sa case : décalez le mesh sur un enfant si son origine est ailleurs.
- **Les collisions** : une pièce dont l'entité a un collider collide, avec les triangles de son mesh, ou de celui de son
  `MeshCollider`. La navigation marche dessus. **Collision** les coupe pour toute la grille.

### Peindre

Ajoutez un composant **Grid map** à une entité, choisissez sa **Palette** et sa **Cell Size**, puis un outil sous **Draw in the View** :

- **Paint** : cliquez et glissez dans la vue pour poser la pièce choisie. **Maj** en retire.
- **Erase** : cliquez et glissez pour retirer des pièces.
- **Rotate** ou **Espace** : tourne la pièce d'un quart de tour. Un trait montre où elle regarde.
- **Floor**, ou **[** et **]** dans la vue : l'étage peint.

**Échap** quitte l'outil, et Ctrl+Z annule un trait entier. L'arène du bac à sable en a un exemple : **Bunker**.

### Depuis le code

Pour un jeu où le joueur construit, `Grids` change la grille pendant la partie :

```csharp
var cell = Grids.CellAt(grid, hit.Position);
Grids.Set(grid, cell, piece, rotation);
int there = Grids.PieceAt(grid, cell);   // -1 quand la case est vide
Grids.Remove(grid, cell);
Vec3 position = Grids.PositionOf(grid, cell);
```

En C++ : `scene::setGridCell`, `findGridCell`, `removeGridCell`, `gridCellAt` et `gridCellPosition`. Une pièce ne porte ni script ni
lumière : ce qui en a besoin reste une entité, posée à `Grids.PositionOf`. La grille n'est pas synchronisée en multijoueur : c'est au
jeu de faire poser la même pièce à tous, par un appel à distance.

## Les blocs CSG

Les blocs donnent la forme d'un niveau sans quitter l'éditeur : des formes qui s'ajoutent les unes aux autres, ou qui
creusent les autres. Leur résultat est un seul mesh, affiché et solide. Dans l'inspecteur, ajoutez à une entité l'un de ces
composants, dans la catégorie **3D** :

- **CSG box**, **CSG sphere** et **CSG cylinder** : les formes.
- **CSG path** : une bande le long d'un `Path3D`, pour une route, un rail ou un mur. Le dessus de la bande est sur la courbe.
- **CSG combiner** : rien par lui-même, il regroupe ce qui est sous lui, pour l'ajouter ou le creuser d'un coup.

Une entité avec l'un d'eux, sans parent qui en ait un, est la **racine** d'un solide. Ce qui est sous elle s'y applique dans
l'ordre de la hiérarchie, selon son champ **Operation** :

- **Union** ajoute la forme.
- **Subtract** la creuse dans ce qui précède : une porte, une fenêtre.
- **Intersect** ne garde que ce qu'elles partagent.

Une entité sans composant CSG coupe l'arbre : ce qui est sous elle forme un autre solide. Quand une forme est sélectionnée, ou sa
racine, l'éditeur trace les formes en fil de fer, y compris ce qu'elles ont creusé : en vert ce qui ajoute, en rouge ce qui
creuse, en jaune ce qui croise.

- **Material** : chaque face garde la matière de la forme dont elle vient, y compris la paroi laissée par un trou. La texture se
  plaque à un mètre par mètre, droite sur les murs.
- **Collision**, sur la racine : le solide a un collider, trous compris, et la navigation marche dessus quand il ne bouge pas.

Le résultat se recalcule dès qu'une forme change, dans l'éditeur comme en jeu : `Entity.Get<CsgBox>().Size = taille;` suffit.
C'est rapide pour des blocs ordinaires, mais à éviter à chaque image sur de grands ensembles. Déplacer la racine ne recalcule
rien. L'arène du bac à sable en a un exemple : **Arch**, un mur percé d'une porte et d'une fenêtre, et **Rail band**.

## Les chemins

Un **Path3D** est une courbe dessinée dans la scène : un rail, le trajet d'une patrouille, le mouvement d'une caméra. Un
**PathFollower** fait avancer une entité dessus sans écrire de code.

### Dessiner

Ajoutez un composant **Path3D** (affiché « Path » dans la liste) à une entité et choisissez un outil dans l'inspecteur, sous **Draw in the View** :

- **Add** : chaque clic dans la vue ajoute un point à la fin de la courbe, à la hauteur du dernier. Gardez le bouton enfoncé
  pour tirer ses poignées et arrondir la courbe. Cliquer sur le premier point referme la courbe.
- **Move** : faites glisser un point ou une poignée. Ils se déplacent à leur hauteur, et **Ctrl** les monte ou les descend.
  Les deux poignées d'un point restent alignées, et **Maj** en tire une seule, pour faire un angle.
- **Remove** : cliquez sur un point pour le retirer.

**Échap** quitte l'outil, et Ctrl+Z annule. **Closed** referme la courbe. L'arène du bac à sable en a un exemple : un cube
qui fait l'aller-retour sur le rail **Rail**.

### Suivre

Ajoutez un composant **PathFollower** à un enfant du chemin. Il peut aussi être ailleurs dans la scène, avec le champ
**Path** qui désigne le chemin.

- **Speed** : en mètres par seconde le long de la courbe.
- **Distance** : où il en est, en mètres. Changez-la pour le placer ailleurs.
- **End** : **Stop** s'arrête au bout, **Loop** recommence ou continue le tour d'une courbe fermée, **Ping Pong** repart en sens
  inverse.
- **Orient** : il regarde où il va, son -Z le long de la courbe.
- **Playing** : décoché, il attend. **Stop** le décoche en arrivant.

Une plateforme avec un `RigidBody` cinématique pousse ce qu'elle rencontre, et porte le personnage debout dessus. Un collider sans corps qu'un script déplace le porte aussi, sans pousser le reste. Une caméra enfant d'un `PathFollower` fait un
travelling.

### Depuis le code

Les champs se changent comme ceux de tout composant :

```csharp
Entity.Get<PathFollower>().Speed = 7.0f;
```

`Paths` lit la courbe, dans les coordonnées du monde, pour placer des objets le long d'elle ou suivre un rail à la main :

```csharp
float length = Paths.Length(rail);
Vec3 position = Paths.PositionAt(rail, length * 0.5f);
Vec3 direction = Paths.DirectionAt(rail, 2.0f);
float closest = Paths.ClosestDistance(rail, player.Transform.Position);
```

Chaque appel parcourt la courbe, ce qui ne convient pas à des milliers d'appels par image. En C++, gardez un
`scene::PathCurve` tant que le chemin ne change pas : il donne `length`, `positionAt`, `directionAt` et `closestDistance`, dans
le repère du chemin. `runtime::pathPositionAt` et ses voisines donnent la même chose que `Paths`.
