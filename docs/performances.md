# Performances mesurées

Les mesures du moteur, avec la machine, la scène et la commande qui les donnent, pour pouvoir les
refaire et les comparer d'une machine à l'autre. Une mesure qui manque est marquée *à mesurer*.

## Comment mesurer

Le banc garde les LODs de géométrie désactivés par défaut pour conserver les comparaisons des
jalons précédents ; `DEVEX_MESH_LOD=1` les active. Le renderer des jeux les utilise par défaut
pour les maillages qui en possèdent. Les mesures du jalon 90 comparent explicitement les deux.

Le banc de mesure est un test caché, à lancer en **Release** sur la machine mesurée :

```bash
devex_render_tests "Renderer stress benchmark"
```

Il ouvre une fenêtre cachée de 1280×720 sans couche de validation, dessine 60 images pour se
chauffer puis en mesure 300, et imprime le temps moyen d'une image, les draw calls, puis le temps de
chaque partie sur le CPU et de chaque passe sur le GPU. Variables d'environnement :

| Variable               | Effet                                                         |
|------------------------|---------------------------------------------------------------|
| `DEVEX_GPU_CULLING=0`  | le CPU culle et dessine chaque instance, comme avant le jalon 85 |
| `DEVEX_GPU_OCCLUSION=0` | conserve le culling du frustum GPU, désactive seulement l'occlusion |
| `DEVEX_STRESS_OCCLUDERS=1` | ajoute quatre grands murs opaques devant le champ d'objets |
| `DEVEX_PRESENT_MODE` | `immediate`, `mailbox` ou `fifo` (défaut) ; le mode obtenu est imprimé |
| `DEVEX_STRESS_OBJECTS` | nombre d'objets (20 000 par défaut)                           |
| `DEVEX_STRESS_LIGHTS`  | nombre de lumières locales (256 par défaut)                   |
| `DEVEX_STRESS_FRAMES` | images mesurées après les 60 de chauffe, de 1 à 10 000 (300 par défaut) |
| `DEVEX_STRESS_SKINNED=1` | anime les trois maillages par deux os, 2 000 objets par défaut |
| `DEVEX_GPU_SKINNING=0` | garde le skinning des personnages dans leurs dessins individuels |
| `DEVEX_STRESS_FOG=1` | remplit l'air de brouillard volumétrique (densité 0,02), éclairé par le soleil et les lumières |

Le temps total couvre toutes les images mesurées ; le détail des zones porte sur les 300 dernières
au plus, limite de l'historique du profileur. Le nombre de frames CPU retenues et de frames GPU
terminées est imprimé. Les attentes de la fence du contexte (*Wait for the GPU*), de l'acquisition
de l'image et de la présentation sont données à part. Les compteurs GPU de la caméra sont ceux
d'une frame terminée, relus sans ajouter d'attente. Les zones parentes comprennent leurs enfants :
additionner toutes les lignes CPU compterait plusieurs fois le même travail.

Sous Windows (PowerShell), depuis le dossier de build Release :

```powershell
.\bin\devex_render_tests.exe "Renderer stress benchmark"
$env:DEVEX_GPU_CULLING = "0"; .\bin\devex_render_tests.exe "Renderer stress benchmark"; Remove-Item Env:DEVEX_GPU_CULLING
```

**La scène** : un champ carré de 20 000 objets espacés de 3 m (cubes, sphères UV et plans, 8
matériaux standard), 256 lumières ponctuelles, le soleil et ses quatre cascades, une caméra posée
dans un coin qui regarde à travers le champ en tournant un peu à chaque image.

## Culling sur GPU (jalon 85)

Ces mesures précèdent l'occlusion. Pour retrouver le même chemin avec le moteur actuel, mettre
`DEVEX_GPU_OCCLUSION=0` dans les deux essais.

### Portable : Intel Iris Xe (Linux)

Ubuntu 26.04, Intel Iris Xe Graphics (TGL GT2), Mesa 26.0.8, Vulkan 1.4, sur secteur en mode
performance. Une même machine portable ne donne pas toujours les mêmes temps : deux séries ont été
mesurées le même soir, chacune avec le même binaire pour les deux chemins.

| Série | Culling | Image   | Draw calls | CPU qui enregistre la frame | Qui limite |
|-------|---------|---------|-----------:|----------------------------:|------------|
| 1     | CPU     | 24,6 ms | 39 994     | 23,9 ms                     | le CPU     |
| 1     | GPU     | 18,3 ms | 48         | 3,3 ms                      | le GPU     |
| 2     | CPU     | 17,4 ms | 39 994     | 16,6 ms                     | les deux   |
| 2     | GPU     | 17,2 ms | 48         | 2,4 ms                      | le GPU     |

Détail de la série 2, en millisecondes par image :

| Partie                         | Culling CPU | Culling GPU |
|--------------------------------|------------:|------------:|
| CPU : enregistrer et soumettre | 16,61       | 2,38        |
| CPU : dont culling             | 0,02        | 0,99        |
| CPU : lumières                 | 0,33        | 0,37        |
| CPU : attente du GPU           | 0,01        | 13,72       |
| GPU : culling (compute)        | —           | 0,38        |
| GPU : prépasse                 | 5,72        | 6,44        |
| GPU : ombrage de la scène      | 6,68        | 6,95        |
| GPU : cascades d'ombres        | 1,47        | 1,68        |
| GPU : anticrénelage            | 0,74        | 0,63        |
| GPU : occlusion ambiante       | 0,31        | 0,30        |
| GPU : bloom                    | 0,31        | 0,29        |
| GPU : tonemapping              | 0,31        | 0,28        |

**Ce qu'on en tire** :

- Le CPU passe de 16,6 à 2,4 ms par image (de 23,9 à 3,3 ms dans la série 1) : il enregistre
  48 dessins au lieu de près de 40 000.
- Sur l'Iris Xe, le GPU met déjà environ 17 ms à dessiner cette scène : quand le CPU est rapide
  (série 2), l'image ne va presque pas plus vite, elle attend le GPU. Quand le CPU est plus lent
  (série 1), elle gagne un quart de son temps.
- Le GPU fait un peu plus de travail : la passe de culling (0,4 ms), et une prépasse plus lente de
  0,7 ms, parce que les objets ne sont plus dessinés du plus proche au plus loin mais lot par lot.
  Le jalon 87 ajoute le culling par occlusion pour les scènes où les objets se cachent.
- L'image est identique au pixel près entre les deux chemins (test *The GPU culls and draws the
  instances of standard materials as the CPU does*).

### Bureau : i7-14700KF et RTX 4060 8 Go (Windows)

Windows 11 Pro, NVIDIA GeForce RTX 4060, pilote 621.01, Vulkan 1.4.357, écran 2560 px à 240 Hz ;
build Release du 30 octobre 2026 (après `029e161`), présentation FIFO. Deux séries, chacune avec le
même binaire pour les deux chemins.

| Série | Culling | Image  | Draw calls | CPU qui enregistre la frame | Qui limite             |
|-------|---------|--------|-----------:|----------------------------:|------------------------|
| 1     | CPU     | 7,4 ms | 39 994     | 6,6 ms                      | le CPU                 |
| 1     | GPU     | 6,1 ms | 48         | 1,9 ms                      | ni l'un ni l'autre     |
| 2     | CPU     | 7,4 ms | 39 994     | 6,6 ms                      | le CPU                 |
| 2     | GPU     | 6,2 ms | 48         | 1,9 ms                      | ni l'un ni l'autre     |

Détail de la série 1, en millisecondes par image :

| Partie                         | Culling CPU | Culling GPU |
|--------------------------------|------------:|------------:|
| CPU : enregistrer et soumettre | 6,58        | 1,91        |
| CPU : dont culling             | 0,02        | 1,01        |
| CPU : lumières                 | 0,48        | 0,49        |
| CPU : attente du GPU           | 0,00        | 3,33        |
| GPU : culling (compute)        | —           | 0,04        |
| GPU : prépasse                 | 1,62        | 1,45        |
| GPU : ombrage de la scène      | 1,72        | 1,81        |
| GPU : cascades d'ombres        | 0,24        | 0,24        |
| GPU : anticrénelage            | 0,10        | 0,10        |
| GPU : occlusion ambiante       | 0,06        | 0,06        |
| GPU : bloom                    | 0,04        | 0,04        |
| GPU : tonemapping              | 0,03        | 0,03        |

**Ce qu'on en tire** :

- Les passes de la RTX 4060 tiennent en 3,8 ms, environ 4,5 fois moins que sur l'Iris Xe : sur
  l'ancien chemin, c'est bien le CPU qui la fait attendre, avec 6,6 ms pour enregistrer près de
  40 000 dessins.
- Le culling sur GPU divise ce temps du CPU par 3,5 (1,9 ms), mais l'image ne passe que de 7,4 à
  6,1 ms (−18 %) : le CPU n'a qu'environ 2,5 ms de travail et le GPU 3,8 ms, le reste est de
  l'attente (3,3 ms d'attente du GPU). Ni l'un ni l'autre ne limite donc l'image ; l'attente vient
  vraisemblablement de la présentation en FIFO de la fenêtre cachée, qui n'a pas été mesurée à
  part. Une mesure sans synchronisation verticale dirait le vrai plafond de ce chemin.
- Contrairement au portable, la prépasse est plus rapide avec le culling sur GPU (1,45 contre
  1,62 ms) ; l'ombrage de la scène est un peu plus lent (1,81 contre 1,72 ms).
- Le culling sur GPU ne coûte que 0,04 ms au GPU, mais 1,0 ms au CPU, qui prépare ses données.

## Occlusion GPU (jalon 87)

i7-14700KF, RTX 4060 8 Go, Windows 11, pilote NVIDIA 621.01, Vulkan 1.4.357. Build Release du
31 octobre 2026, validation désactivée, présentation **Immediate demandée et obtenue**. Deux séries
avec le même binaire, 60 images de chauffe puis 300 mesurées à 1280 × 720 ; 300 frames CPU retenues,
298 frames GPU terminées. Le culling du frustum GPU reste actif dans tous ces essais.

La scène dégagée est celle du jalon 85. L'autre ajoute quatre grands murs opaques au même champ de
20 000 objets et 256 lumières. La caméra tourne légèrement pendant les deux essais. Les colonnes
GPU additionnent les durées des passes, pas le temps de présentation.

| Série | Scène | Occlusion | Image | Passes GPU | Draw calls | Instances cachées à la fin |
|-------|-------|-----------|------:|-----------:|-----------:|--------------------------:|
| 1 | Dégagée | Non | 7,57 ms | 3,48 ms | 48 | 0 |
| 1 | Dégagée | Oui | 7,51 ms | 3,51 ms | 72 | 0 |
| 1 | Murs | Non | 7,64 ms | 3,52 ms | 48 | 0 |
| 1 | Murs | Oui | 5,03 ms | 0,76 ms | 72 | 18 926 |
| 2 | Dégagée | Non | 7,47 ms | 3,45 ms | 48 | 0 |
| 2 | Dégagée | Oui | 7,65 ms | 3,50 ms | 72 | 0 |
| 2 | Murs | Non | 7,58 ms | 3,52 ms | 48 | 0 |
| 2 | Murs | Oui | 5,15 ms | 0,75 ms | 72 | 18 926 |

Détail de la série 2 derrière les murs, en millisecondes par image :

| Partie | Sans occlusion | Avec occlusion |
|--------|---------------:|---------------:|
| CPU : enregistrer et soumettre, hors présentation | 1,86 | 2,44 |
| CPU : dont préparation du culling et lecture des résultats | 1,06 | 1,56 |
| CPU : attente de la fence du contexte | 0,004 | 0,004 |
| CPU : acquisition de l'image | 0,002 | 0,001 |
| CPU : présentation | 4,82 | 1,83 |
| GPU : culling du frustum | 0,028 | 0,027 |
| GPU : première prépasse | 1,295 | 0,066 |
| GPU : pyramide de profondeur | — | 0,016 |
| GPU : test d'occlusion | — | 0,012 |
| GPU : prépasse de récupération | — | 0,004 |
| GPU : ombrage de la scène | 1,678 | 0,111 |
| GPU : cascades d'ombres | 0,246 | 0,246 |

**Ce qu'on en tire** :

- Derrière les murs, 18 926 instances sur 20 004 sont cachées, trois autres hors du frustum. Le
  temps de frame baisse de **32 à 34 %**, et le travail mesuré des passes GPU d'environ **79 %**.
  Les ombres gardent les objets qui les projettent et leur coût ne baisse pas.
- Le nombre de draw calls indirects monte de 48 à 72 : la récupération ajoute une ligne de lots,
  même quand certains sont vides. Le gain vient des instances qui ne sont plus dessinées dans ces
  appels, pas d'une baisse du nombre d'appels.
- Sans obstacle, aucune instance n'est cachée. La pyramide et le test coûtent ensemble environ
  0,03 ms GPU ; le temps total varie de 7,47 à 7,65 ms. Cette optimisation ne garantit donc pas
  un gain dans une scène dégagée. Elle est évitée sous huit instances standard éligibles et peut
  être désactivée par `RendererConfig::gpuOcclusion`.
- Le CPU fait environ 0,5 ms de préparation supplémentaire lorsqu'il réutilise la liste des objets
  cachés. Cette liste est un tableau trié réutilisé entre frames, sans allocation par objet.
- Même en mode Immediate, `vkQueuePresentKHR` prend ici du temps. Les attentes sont maintenant
  mesurées séparément : le temps total d'une frame n'est ni le temps GPU seul, ni une mesure de la
  latence entre une entrée et l'écran. Ces chiffres concernent cette machine et cette scène ;
  l'Iris Xe reste **à mesurer** pour ce jalon.

Pour reproduire les quatre configurations depuis la racine du dépôt, dans PowerShell :

```powershell
$env:DEVEX_GPU_CULLING = "1"
$env:DEVEX_PRESENT_MODE = "immediate"
$env:DEVEX_STRESS_OBJECTS = "20000"
$env:DEVEX_STRESS_LIGHTS = "256"
$env:DEVEX_STRESS_FRAMES = "300"
foreach ($walls in 0..1) {
    foreach ($occlusion in 0..1) {
        $env:DEVEX_STRESS_OCCLUDERS = [string]$walls
        $env:DEVEX_GPU_OCCLUSION = [string]$occlusion
        .\out\build\x64-release\bin\devex_render_tests.exe "Renderer stress benchmark"
    }
}
Remove-Item Env:DEVEX_GPU_CULLING, Env:DEVEX_GPU_OCCLUSION, Env:DEVEX_PRESENT_MODE
Remove-Item Env:DEVEX_STRESS_OBJECTS, Env:DEVEX_STRESS_LIGHTS, Env:DEVEX_STRESS_FRAMES, Env:DEVEX_STRESS_OCCLUDERS
```

Pour l'observer dans l'éditeur, ouvrir `samples/sandbox/Sandbox.dvxproj`, puis
`assets/scenes/occlusion.dvxscene`. Les moniteurs *GPU Instances Tested*, *GPU Outside Frustum* et
*GPU Occluded Instances* de Statistics affichent la dernière frame terminée de la caméra
principale, avec quelques frames de décalage. Cliquer dans la vue en Play pour capturer la souris,
WASD pour voler, Espace/Ctrl gauche pour monter/descendre, Maj gauche pour accélérer. La scène a
trois écrans opaques et 120 cubes ; passer à côté d'un écran fait réapparaître les cubes dans la
frame où ils sont visibles.

## Skinning compute et lots animés (jalon 88)

Même i7-14700KF et RTX 4060, Windows 11, pilote 621.01, build Release du 31 octobre 2026,
validation désactivée, présentation Immediate demandée et obtenue. Deux séries de chaque
configuration : 60 images de chauffe puis 300 mesurées à 1280 × 720, 256 lumières ; 300 frames CPU
retenues et 298 frames GPU terminées. Le culling GPU et l'occlusion sont demandés dans les deux
chemins. Sans skinning compute, les maillages animés gardent le chemin d'origine : le CPU enregistre
leurs dessins un par un et leurs **vertex shaders GPU** déforment les sommets à chaque passe.
Les quatre murs, quand présents, sont des instances statiques. Les comparaisons ci-dessous
mesurent donc le chemin complet (préparation, regroupement et culling des poses), pas seulement
le déplacement d'un calcul d'un shader à un autre.

La variante animée du benchmark reprend cubes, sphères et plans, chacun avec deux os dont les
rotations évoluent à chaque image. Le champ de 2 000 objets représente **414 171 sommets** de poses ;
celui de 20 000 en représente **4 140 171**. Ce sont des maillages de stress, pas des modèles de
personnages de production.

### 20 000 objets animés

| Série | Scène | Skinning | Image | Passes GPU | CPU enregistrement hors présentation | Draw calls |
|-------|-------|----------|------:|-----------:|--------------------------------------:|-----------:|
| 1 | Dégagée | Par dessin | 17,53 ms | 5,40 ms | 8,35 ms | 39 994 |
| 1 | Dégagée | Compute + lots | 16,77 ms | 8,59 ms | 4,36 ms | 72 |
| 1 | Murs | Par dessin | 17,52 ms | 5,24 ms | 8,44 ms | 39 996 |
| 1 | Murs | Compute + lots | 12,86 ms | 4,25 ms | 4,83 ms | 72 |
| 2 | Dégagée | Par dessin | 17,81 ms | 5,24 ms | 8,68 ms | 39 994 |
| 2 | Dégagée | Compute + lots | 16,96 ms | 8,61 ms | 4,42 ms | 72 |
| 2 | Murs | Par dessin | 17,34 ms | 5,26 ms | 8,29 ms | 39 996 |
| 2 | Murs | Compute + lots | 13,09 ms | 4,32 ms | 4,85 ms | 72 |

Détail de la série 2 derrière les murs, en millisecondes par image :

| Partie | Par dessin | Compute + lots |
|--------|-----------:|---------------:|
| CPU : préparation du culling | 0,120 | 3,021 |
| CPU : dont préparation des poses | 0,005 | 1,355 |
| CPU : présentation | 6,772 | 6,043 |
| CPU : attente de la fence du contexte | 0,006 | 0,006 |
| GPU : préparation des poses | — | 3,486 |
| GPU : première prépasse | 2,761 | 0,075 |
| GPU : récupération des objets redevenus visibles | — | 0,005 |
| GPU : ombrage de la scène | 1,977 | 0,124 |
| GPU : cascades d'ombres | 0,254 | 0,250 |

18 899 instances sont cachées à la fin sur 20 004, et trois hors du frustum. Le temps de frame
baisse de **24 à 27 %** derrière les murs et d'environ **4 à 5 %** dans le champ dégagé. Le temps
CPU d'enregistrement hors présentation baisse de près de moitié dans ce dernier cas, mais le
travail GPU **augmente** : toutes les poses sont préparées et leurs attributs écrits dans un buffer
puis relus. Le nombre de draw calls seul ne dit donc pas le temps de frame.

### 2 000 objets animés

| Scène | Par dessin, deux séries | Compute + lots, deux séries |
|-------|------------------------:|-----------------------------:|
| Dégagée | 4,17 / 4,17 ms | 4,17 / 4,20 ms |
| Murs | 4,18 / 4,17 ms | 4,15 / 4,03 ms |

Le CPU enregistre en environ 0,55 à 0,61 ms hors présentation au lieu de 1,00 à 1,01 ms, mais le
temps total change peu. Le compute prend environ 0,25 ms GPU et sa préparation 0,12 ms CPU.
Il n'y a **pas de gain de FPS garanti** pour un petit nombre de personnages, un modèle différent ou
un autre GPU. Sur l'Iris Xe, ce jalon reste **à mesurer**.

### Mémoire et reproduction

Une pose préparée garde 64 octets par sommet, avec positions courante et précédente, normale et
tangente. Les 4 140 171 sommets du gros essai demandent **265 Mo utiles par contexte de frame** ;
la croissance géométrique des buffers et les deux frames en vol peuvent réserver environ 1 Go pour
ces sorties. Les personnages hors du frustum sont aussi préparés afin de mesurer leurs limites
exactes. `RendererConfig::gpuSkinning=false` permet de garder l'ancien chemin, et les matériaux
transparents et shaders de projet le gardent déjà. L'évaluation des animations et des matrices
d'os reste sur le CPU.

Le regroupement des poses réutilise une table contiguë à adressage ouvert, sans allocation de
nœud par personnage et par frame ; les sous-maillages compatibles réutilisent leurs attributs.
La zone CPU *Skinning data* et la passe GPU *Skinning* permettent de distinguer les deux coûts.

Depuis la racine du dépôt, PowerShell :

```powershell
$env:DEVEX_STRESS_SKINNED = "1"
$env:DEVEX_STRESS_OBJECTS = "20000" # ou 2000 pour le petit essai
$env:DEVEX_STRESS_LIGHTS = "256"
$env:DEVEX_STRESS_FRAMES = "300"
$env:DEVEX_PRESENT_MODE = "immediate"
$env:DEVEX_GPU_CULLING = "1"
$env:DEVEX_GPU_OCCLUSION = "1"
foreach ($walls in 0..1) {
    foreach ($skinning in 0..1) {
        $env:DEVEX_STRESS_OCCLUDERS = [string]$walls
        $env:DEVEX_GPU_SKINNING = [string]$skinning
        .\out\build\x64-release\bin\devex_render_tests.exe "Renderer stress benchmark"
    }
}
Remove-Item Env:DEVEX_STRESS_SKINNED, Env:DEVEX_STRESS_OBJECTS, Env:DEVEX_STRESS_LIGHTS, Env:DEVEX_STRESS_FRAMES
Remove-Item Env:DEVEX_PRESENT_MODE, Env:DEVEX_GPU_CULLING, Env:DEVEX_GPU_OCCLUSION
Remove-Item Env:DEVEX_STRESS_OCCLUDERS, Env:DEVEX_GPU_SKINNING
```

Le sandbox contient aussi `assets/scenes/skinning.dvxscene` : **96 robots qui marchent sur place**,
un écran opaque et une caméra libre (mêmes commandes que la scène d'occlusion). Les moniteurs
*GPU Skinned Poses* et *GPU Skinned Vertices* décrivent la préparation de la vue principale
courante ; les trois moniteurs de visibilité restent ceux d'une frame GPU terminée. Le
générateur accepte `python scripts/generate_sample_assets.py --skinning-only`.

## Poses utiles, vues partagées et mémoire (jalon 89)

Mesures du 1er novembre 2026, sur le même i7-14700KF et la RTX 4060, Windows 11,
pilote 621.01, build Release sans validation, présentation Immediate demandée et obtenue.
La référence est `c03a604`, avec le benchmark étendu aux vues supplémentaires. Avant et après :
60 frames de chauffe puis 300 mesurées, 256 lumières, image principale à 1280 × 720 ;
les deux vues supplémentaires, quand présentes, sont à 640 × 360. Deux séries par cas,
sans compilation ni autre test GPU pendant les mesures.

| Scène | Avant, deux séries | Après, deux séries | Mémoire vidéo avant → après |
|-------|-------------------:|-------------------:|---------------------------:|
| 20 000 poses, une caméra, dégagé | 16,21 / 18,28 ms | 16,23 / 17,73 ms | 1 319,28 → 877,12 Mio |
| 20 000 poses, une caméra, murs | 14,58 / 13,58 ms | 14,28 / 14,35 ms | 1 319,28 → 877,12 Mio |
| 2 000 poses, trois caméras | 7,51 / 7,44 ms | 6,26 / 6,32 ms | 867,85 → 692,50 Mio |

La colonne mémoire est celle du processus rapportée par le budget Vulkan/VMA ; elle comprend
les autres buffers et images, pas seulement les poses. Les sorties de skinning et leurs limites
réservent désormais **570 Mio** pour les deux contextes du cas de 20 000 poses, au lieu d'environ
1 Gio avec le doublement précédent. Les attributs restent à 64 octets par sommet, sans
quantification supplémentaire : c'est l'allocation qui change.

Dans les scènes à une caméra, presque toutes les poses sont nécessaires à la caméra ou aux
cascades : la réduction de mémoire est nette, mais ces mesures ne montrent pas de gain de temps
de frame constant. Borner les poses a aussi un coût CPU : *Skinning data* passe de
1,49 / 1,70 ms à 3,03 / 3,14 ms dans le cas dégagé à 20 000 poses.

Avec trois caméras, **2 000 poses sont calculées une fois**, puis **4 000 poses de vues sont
réutilisées**, au lieu de préparer chaque vue séparément. Le temps de frame baisse de **15 à 17 %**
dans cet essai. Les compteurs de poses et de sommets décrivent maintenant toutes les vues de
la frame courante ; auparavant ils ne décrivaient que la vue principale.

### Personnages hors des vues utiles

Un essai supplémentaire place 75 % des 20 000 poses loin derrière les caméras et hors des vues
des ombres : **15 000 poses sont écartées avant le compute**, 5 000 poses / 1 035 171 sommets
sont préparés. Les sorties réservent **142,62 Mio**, le budget rapporte **450,75 Mio** pour le
processus ; temps de frame de **10,70 / 10,41 ms**. Il s'agit d'une autre disposition, sans mesure
avant/après correspondante : ces temps ne se comparent pas directement à ceux du champ dégagé.

Les boîtes par os sont construites à l'arrivée du maillage ; leur union transformée borne
ensuite chaque pose sans parcourir tous ses sommets. Les sommets sans poids suivent la matrice
de l'instance. Les poids signés, matrices non affines ou limites incertaines conservent la pose.
Une ombre hors écran reste un motif de préparation, et une demande de picking conserve toutes
les poses. Les limites exactes restent calculées sur le GPU pour le culling fin et l'occlusion.
Les personnages simplement cachés derrière un mur sont toujours préparés s'ils passent ces tests.

### Allocation et vérifications

Chaque contexte de frame possède des pages communes aux vues : une vue écrit une page pour ses
poses manquantes, les autres en lisent les résultats. Une page soumise n'est plus réallouée
dans cette frame. Sa dernière fence protège son réemploi, y compris quand seules les vues
supplémentaires ont été soumises avant un échec d'acquisition du swapchain principal.

Les buffers de sortie grandissent de 12,5 %, arrondis à 64 Kio. Après 30 utilisations sous la
moitié de leur capacité, ils sont réduits ; les pages inactives sont libérées après 120 frames.
Le moniteur **Skinning Memory** compte les sorties et limites réservées de tous les contextes,
sans les matrices d'os ni les jobs. **Skinning Poses Skipped** et **Skinning Poses Reused**
comptent des poses par vue, sans recompter leurs sous-maillages.

Les tests vérifient les limites face aux sommets déformés, les poids non normalisés et les
échelles inversées ; ils comparent images, ombres, TAA et picking au chemin par dessin, avec
des poses éloignées de la matrice de l'instance, plusieurs vues, caméra déplacée, pose qui
revient dans le champ et liste réordonnée. Un autre test retire une foule, puis les vues, et
vérifie la réduction, la libération et la réutilisation des allocations. Les mesures sur un
GPU intégré restent à faire ; l'évaluation des animations et matrices d'os demeure au CPU.

Depuis la racine, les paramètres du benchmark animé restent les mêmes ; pour ces cas :

```powershell
$env:DEVEX_STRESS_SKINNED = "1"
$env:DEVEX_STRESS_OBJECTS = "2000" # 20000 pour une seule caméra
$env:DEVEX_STRESS_LIGHTS = "256"
$env:DEVEX_STRESS_FRAMES = "300"
$env:DEVEX_PRESENT_MODE = "immediate"
$env:DEVEX_GPU_CULLING = "1"
$env:DEVEX_GPU_OCCLUSION = "1"
$env:DEVEX_GPU_SKINNING = "1"
$env:DEVEX_STRESS_VIEWS = "2" # caméras supplémentaires, 0..4
$env:DEVEX_STRESS_OFFSCREEN = "0" # pourcentage derrière les caméras, 0..100
$env:DEVEX_STRESS_OCCLUDERS = "0"
.\out\build\x64-release\bin\devex_render_tests.exe "Renderer stress benchmark"
Remove-Item Env:DEVEX_STRESS_SKINNED, Env:DEVEX_STRESS_OBJECTS, Env:DEVEX_STRESS_LIGHTS,
    Env:DEVEX_STRESS_FRAMES, Env:DEVEX_PRESENT_MODE, Env:DEVEX_GPU_CULLING,
    Env:DEVEX_GPU_OCCLUSION, Env:DEVEX_GPU_SKINNING, Env:DEVEX_STRESS_VIEWS,
    Env:DEVEX_STRESS_OFFSCREEN, Env:DEVEX_STRESS_OCCLUDERS
```

## Niveaux de détail (jalon 90)

Mesures du **1er novembre 2026**, i7-14700KF, RTX 4060, Windows 11 et pilote 621.01,
Release avec avertissements en erreurs, sans validation. Le benchmark génère les mêmes niveaux
dans les deux variantes ; `DEVEX_MESH_LOD=0` conserve la géométrie originale, `1` active le choix
par vue avec le budget par défaut d'un pixel. Même caméra mobile, 256 lumières et paramètres
de culling, occlusion et skinning ; 60 frames de chauffe puis 300 mesurées, deux séries de
chaque cas, sans compilation ou autre test concurrent pendant les mesures. Présentation
Immediate demandée et obtenue, image principale à 1280 × 720, vues supplémentaires à 640 × 360.

| Scène | LOD désactivé, deux séries | LOD actif, deux séries | Mémoire vidéo sans → avec |
|-------|---------------------------:|----------------------:|--------------------------:|
| 20 000 poses, dégagé, une vue | 16,74 / 16,95 ms | 15,62 / 15,32 ms | 877,12 → 524,75 Mio |
| 20 000 poses, murs, une vue | 14,67 / 14,15 ms | 15,12 / 14,97 ms | 877,12 → 524,75 Mio |
| 2 000 poses, trois vues | 6,24 / 6,25 ms | 6,72 / 6,82 ms | 692,50 → 564,50 Mio |

Dans le gros champ, 13 333 instances de sphères et de plans emploient un niveau, les cubes étant
déjà trop simples. Les 20 000 poses sont toujours préparées, mais leurs sommets passent de
**4 140 171 à 1 574 672**, soit **62 % de moins**. Les sorties réservent **217,62 Mio** au lieu de
570,00 Mio, et le budget vidéo du processus baisse d'environ 40 %. Le temps de frame baisse
d'environ **7 à 10 %** dans le champ dégagé. Derrière les murs, il augmente en revanche de
**3 à 6 %** : l'occlusion avait déjà enlevé une grande partie des dessins, et le coût CPU de choix
des niveaux et des lots supplémentaires dépasse ici le travail GPU économisé.

Dans la seconde série dégagée, la passe GPU *Skinning* passe de 3,051 à 0,768 ms. Les lots
supplémentaires font en revanche passer les draw calls de 72 à 93 : leur nombre seul ne mesure
pas le gain. **LOD Triangles Saved** indique 5 331 008 triangles potentiellement évités avant
culling ; cela ne correspond pas au nombre effectivement rasterisé derrière un mur.

À trois vues et 2 000 objets, les temps augmentent de **8 à 9 %** dans cet essai : le gain GPU
ne compense pas le choix des niveaux par le CPU. Les résolutions différentes font aussi choisir
des niveaux différents pour huit poses : 2 008 poses sont préparées au lieu de 2 000, mais
seulement 159 952 sommets au lieu de 414 171 ; 3 992 poses de vues restent réutilisées.
Les buffers de sortie réservent 22,50 Mio au lieu de 57,25 Mio. Les niveaux ajoutent eux-mêmes
du stockage et des buffers de géométrie, et leur sélection coûte du CPU. Les modèles, leurs
déformations et le matériel peuvent changer ce bilan.

### Échantillonnage des animations sur le CPU

Un banc séparé, *Animation LOD stress benchmark*, mesure `AnimationWorld::update` en Release :
256 personnages, 64 joints animés chacun, pistes de translation et rotation, clip partagé,
60 updates de chauffe puis 300 mesurées à un pas simulé de 1/60 s. Les spectateurs sont loin ;
les animators sans root motion emploient donc 5 Hz quand le LOD est activé.

| Variante | Deux séries, par update | Poses calculées sur 300 updates | Poses tenues |
|----------|------------------------:|-------------------------------:|-------------:|
| Chaque frame | 1,316 / 1,320 ms | 76 800 | 0 |
| LOD à 5 Hz | 0,152 / 0,149 ms | 6 400 | 70 400 |

Le temps de cette mise à jour baisse de **88 à 89 %** ; les calculs sont déphasés par identité
pour répartir les ticks entre frames. Ce banc n'inclut ni le recalcul des matrices mondiales,
ni la physique, ni le rendu : ce pourcentage ne décrit pas une hausse équivalente des FPS.
Il ne réduit pas le nombre d'os ni leurs matrices et n'interpole pas les poses tenues. Les
événements et transitions avancent chaque frame, les changements de state/clip et seek posent
immédiatement le squelette ; le root motion conserve le plein débit. Les tests vérifient ces
comportements et le retour immédiat près d'une caméra.

### Vérifications et reproduction

Les LODs générés sont testés pour les matériaux, poids, attributs des sommets, réduction,
encodage/décodage et rejets des données invalides. Les images sont comparées au choix explicite
des mêmes maillages : modèles statiques et animés, perspective et orthographique, mouvements,
ombres, TAA, contours et picking. Deux vues qui demandent des niveaux différents gardent leurs
poses distinctes ; l'hystérésis est vérifiée autour du seuil. La scène de démonstration
`assets/scenes/lods.dvxscene` est générable avec `--lods-only`.

Les 77 tests du renderer passent en Debug ; les régressions LOD/skinning/occlusion passent aussi
en Release. Les tests sans GPU passent (deux cas de liens symboliques ignorés faute de droits
Windows). L'export Release d'une copie isolée du sandbox avec la scène LOD réussit, et la
bibliothèque exportée charge sa dépendance meshoptimizer depuis le dossier autonome.
Ces mesures ne couvrent pas encore un GPU intégré.

Depuis la racine, reprendre les variables du benchmark animé ci-dessus et alterner
`$env:DEVEX_MESH_LOD = "0"` / `"1"` avant de lancer `Renderer stress benchmark`.
Pour le banc CPU seul :

```powershell
$env:DEVEX_ANIMATION_LOD = "0" # puis "1" pour la seconde variante
.\out\build\x64-release\bin\devex_animation_tests.exe "Animation LOD stress benchmark"
Remove-Item Env:DEVEX_ANIMATION_LOD
```

## Brouillard volumétrique (jalon 97)

Bureau : i7-14700KF et RTX 4060 (Windows 11, pilote 621.01), le 3 novembre 2026, présentation
`immediate`, banc d'essai lancé avec `DEVEX_STRESS_FOG=1` (densité 0,02, 256 lumières locales, le
soleil et ses cascades). Build Debug : les temps du CPU n'y valent rien, mais les shaders, et donc
les temps du GPU, sont ceux du Release.

| Passe du GPU                | Sans brouillard | Avec brouillard |
|-----------------------------|----------------:|----------------:|
| Volumetric fog (compute)    | —               | 0,21 ms         |
| Scene                       | 2,26 ms         | 2,06 à 2,30 ms  |
| Prepass                     | 2,08 ms         | 1,86 à 2,05 ms  |

**Ce qu'on en tire** : les 921 600 froxels (160 × 90 × 64) éclairés par le soleil à travers ses
ombres et par les lumières de leurs grappes, puis intégrés, coûtent 0,21 ms, bien sous le 1 ms
visé ; la scène, qui lit deux couches du brouillard intégré par pixel, ne change pas au-delà des
écarts d'une mesure à l'autre.
