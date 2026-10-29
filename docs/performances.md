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

*À mesurer.* Attendu : la RTX 4060 dessine bien plus vite que l'Iris Xe, donc sur l'ancien chemin
c'est le CPU qui la faisait attendre ; le culling sur GPU devrait y gagner nettement plus que sur le
portable. Les buffers que le CPU écrit chaque image sont maintenant placés en mémoire vidéo quand la
carte le permet (Resizable BAR), au lieu d'être lus à travers le PCIe.

| Culling | Image        | Draw calls | CPU qui enregistre la frame | Qui limite   |
|---------|--------------|-----------:|----------------------------:|--------------|
| CPU     | *à mesurer*  | 39 994     | *à mesurer*                 | *à mesurer*  |
| GPU     | *à mesurer*  | 48         | *à mesurer*                 | *à mesurer*  |
