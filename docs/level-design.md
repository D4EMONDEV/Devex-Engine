# Dessiner un niveau

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

Une plateforme avec un `RigidBody` cinématique pousse ce qu'elle rencontre. Une caméra enfant d'un `PathFollower` fait un
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
