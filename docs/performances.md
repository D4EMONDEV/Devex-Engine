# Performances mesurées

Les mesures du moteur, avec la machine, la scène et la commande qui les donnent, pour pouvoir les
refaire et les comparer d'une machine à l'autre. Une mesure qui manque est marquée *à mesurer*.

## Comment mesurer

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
| `DEVEX_STRESS_OBJECTS` | nombre d'objets (20 000 par défaut)                           |
| `DEVEX_STRESS_LIGHTS`  | nombre de lumières locales (256 par défaut)                   |

Sous Windows (PowerShell), depuis le dossier de build Release :

```powershell
.\bin\devex_render_tests.exe "Renderer stress benchmark"
$env:DEVEX_GPU_CULLING = "0"; .\bin\devex_render_tests.exe "Renderer stress benchmark"; Remove-Item Env:DEVEX_GPU_CULLING
```

**La scène** : un champ carré de 20 000 objets espacés de 3 m (cubes, sphères UV et plans, 8
matériaux standard), 256 lumières ponctuelles, le soleil et ses quatre cascades, une caméra posée
dans un coin qui regarde à travers le champ en tournant un peu à chaque image.

## Culling sur GPU (jalon 85)

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
  Le culling par occlusion est la suite prévue pour regagner ce temps.
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
