# Vérifier les lecteurs de fichiers

Les lecteurs de terrains (`.dvxterrain`), de maillages cuits (`.dvxasset`) et de scènes
(`.dvxscene`) s'exécutent sans fenêtre ni GPU.

## Vérifications de chaque build

Les tests du label `corrupt` font partie de la suite normale, sur Windows et Linux : chaque préfixe
tronqué d'un terrain et d'un maillage, compteurs d'éléments démesurés, valeurs non finies, nombres
hors de la plage des champs `float`, appels imbriqués, et 512 mutations déterministes par lecteur.
Une entrée acceptée doit encore pouvoir s'écrire et se relire. Pour les lancer seuls :

```bash
ctest --preset test-linux-debug --label-regex corrupt --output-on-failure
```

Les appels du format texte peuvent être imbriqués jusqu'à 64 niveaux (`maxTextNesting`). Au-delà,
la lecture retourne une erreur avec la ligne et la colonne. Les champs `float` des composants
refusent les nombres non finis ou hors de leur plage avant la conversion ; les sommets, boîtes,
poids et matrices de skinning des maillages doivent aussi être finis.

## Campagnes avec libFuzzer

Le preset `linux-fuzz` utilise Clang 21 avec compiler-rt/libFuzzer, les dépendances habituelles du
moteur et le SDK Vulkan pour compiler les shaders. Il n'ouvre pas l'éditeur et ne demande pas de
GPU. Il instrumente les modules Core, Reflection, Serialization, Asset et Scene avec la couverture,
AddressSanitizer et UndefinedBehaviorSanitizer. Les cibles ne masquent pas les erreurs des
sanitizers et arrêtent la campagne à la première défaillance.

```bash
cmake --preset linux-fuzz
cmake --build --preset build-linux-fuzz --parallel 2
mkdir -p out/build/linux-fuzz/tests/fuzz/failures
```

Le build produit `devex_fuzz_terrain`, `devex_fuzz_mesh` et `devex_fuzz_scene` dans `bin/` et crée
les corpus de départ : terrain plat et maillages statique et animé encodés par le moteur, scènes
textuelles avec transformation et composant inconnu. Les mutations restent dans le dossier de build.
Le lecteur de scènes garde les préfabs non résolus, sans lire d'autres fichiers.

Une première campagne de 60 secondes par lecteur, limitée à des entrées d'un Mio et à un budget
mémoire d'un Gio :

```bash
out/build/linux-fuzz/bin/devex_fuzz_terrain out/build/linux-fuzz/tests/fuzz/corpus/terrain -max_total_time=60 -max_len=1048576 -rss_limit_mb=1024 -timeout=5 -artifact_prefix=out/build/linux-fuzz/tests/fuzz/failures/
out/build/linux-fuzz/bin/devex_fuzz_mesh out/build/linux-fuzz/tests/fuzz/corpus/mesh -max_total_time=60 -max_len=1048576 -rss_limit_mb=1024 -timeout=5 -artifact_prefix=out/build/linux-fuzz/tests/fuzz/failures/
out/build/linux-fuzz/bin/devex_fuzz_scene out/build/linux-fuzz/tests/fuzz/corpus/scene -dict=tests/fuzz/scene.dict -max_total_time=60 -max_len=1048576 -rss_limit_mb=1024 -timeout=5 -artifact_prefix=out/build/linux-fuzz/tests/fuzz/failures/
```

La campagne ne prouve pas l'absence de défauts. Elle garde les entrées qui couvrent du nouveau code
pour les prochaines exécutions et écrit tout cas qui fait échouer un sanitizer, plante, dépasse
les limites ou rompt une relecture dans `failures/`. Rejouer un cas avec la même cible :

```bash
out/build/linux-fuzz/bin/devex_fuzz_scene out/build/linux-fuzz/tests/fuzz/failures/crash-IDENTIFIANT
```

Réduire le cas avec `-minimize_crash=1`, corriger le lecteur, puis ajouter un test de régression
lisible à la suite normale. Les campagnes longues sont facultatives ; les régressions et les
mutations déterministes passent dans la CI existante.

Le montage suit les documentations de [libFuzzer](https://llvm.org/docs/LibFuzzer.html),
[AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) et
[UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html).
