# Compatibilité des projets

Ce qu'un projet peut attendre d'une nouvelle version de Devex, et ce qu'il faut faire quand on change un format.

## La promesse

Elle sera définitive à la 1.0. Les tests la gardent déjà.

- **Les fichiers d'un projet** s'ouvrent dans toutes les versions 1.x qui suivent, sans rien perdre.
- **L'API C#** ne fait que grandir. Ce qui doit partir est marqué `[Obsolete]`, et ne part qu'en 2.0.
- **L'API C++** peut changer : l'éditeur recompile le code du jeu, et les changements sont annoncés.

Ce qui se recalcule n'en fait pas partie : les caches d'import (`.dvxasset`) reviennent tout seuls, et les précalculs
(`.dvxnavmesh`, `.dvxprobe`, `.dvxirradiance`) se relancent depuis l'éditeur.

## À l'ouverture d'un projet

Le fichier `.dvxproj` dit quelle version l'a enregistré en dernier (`engine="0.1.0"`). Un projet qui ne le dit pas vient
d'avant cette règle, et compte comme plus ancien.

| Le projet vient | Le gestionnaire de projets | Ensuite |
| --- | --- | --- |
| de cette version | l'ouvre | rien |
| d'une version plus ancienne | prévient, avec **Open** et **Cancel** | ses fichiers sont copiés dans `.devex/backups/<version>`, et chacun prend le format actuel quand il est enregistré |
| d'une version plus récente | avertit, avec **Open Anyway** et **Cancel** ; Entrée ne confirme pas | ce que cette version ne connaît pas de ses fichiers est perdu quand ils sont enregistrés, et le projet garde la version la plus récente |

En ligne de commande (`devex-editor MonJeu.dvxproj`), personne ne peut répondre : la copie se fait et le journal le dit, et
un projet plus récent s'ouvre avec un avertissement dans le journal. Le joueur et `--export` ne modifient rien.

La copie garde les fichiers `.dvx*` faits à la main, à leur place, dont les terrains sculptés et les `.dvxmeta`. Elle
laisse de côté le code, que le gestionnaire de versions du projet garde, et ce qui se recalcule. Une copie n'en remplace
jamais une autre : la deuxième s'appelle `<version>-2`.

## Changer un format

Du plus simple au plus rare :

| Ce qui change | Ce qu'il faut faire |
| --- | --- |
| Un champ ou un composant apparaît | Rien : les anciens fichiers gardent la valeur par défaut. |
| Un champ est renommé | C++ : `.field("speed", &T::speed, {.was = "velocity"})`. C# : `[WasNamed("Velocity")]`. |
| Un composant est renommé | C++ : `type.was("Mover")`. C# : `[WasNamed("Mover")]` sur la classe. |
| Une valeur d'une énumération est renommée | Garder l'ancienne : les scènes l'écrivent en toutes lettres. |
| Le sens ou la forme d'une scène change | Monter `sceneFormatVersion`, et ajouter dans `upgradeScene` (`SceneSerializer.cpp`) l'étape `.from(ancien, …)` qui transforme le document. |
| Un autre format texte change | Monter sa version : il lit déjà les anciennes, et son lecteur garde le code qui les comprend. |
| La disposition de `.dvxterrain` change | Garder de quoi lire l'ancienne : elle est sculptée à la main et ne se recalcule pas. |

Les anciens noms restent pour toujours : un fichier peut attendre plusieurs versions avant d'être ouvert. Écrire les
fichiers se fait toujours dans le format actuel, sous les noms actuels.

## Les garde-fous

Ils vivent dans [tests/data/formats](../tests/data/formats) :

- **`schema.txt`** liste les champs des composants du moteur et leur type. Un champ disparu sans ancien nom, ou dont le type
  change, fait échouer un test qui dit quoi faire.
- **`csharp-api.txt`** liste l'API C# publique. Un membre disparu fait échouer `devex.formats.csharp_api`.
- **Un dossier par version publiée**, jamais modifié : une scène avec tous les composants aux valeurs changées, qui doit se
  relire sans perdre une valeur, un fichier de chaque autre sorte, un terrain, une sauvegarde et un projet.

Un ajout fait aussi échouer ces tests, pour que la liste reste complète. Après un changement voulu, on enregistre ce que la
version écrit, et le diff montre ce qui a bougé :

```bash
DEVEX_UPDATE_FORMATS=1 out/build/x64-debug/bin/devex_asset_tests "[format]"
```

```bash
DEVEX_UPDATE_FORMATS=1 ctest --test-dir out/build/x64-debug -R csharp_api
```

## À chaque version publiée

1. Monter la version dans `CMakeLists.txt`, et `engine="…"` dans `samples/sandbox/Sandbox.dvxproj`.
2. Lancer les deux commandes ci-dessus, puis `devex_runtime_tests "The saves of every version read"` avec la même variable :
   elles créent `tests/data/formats/<version>` avec la scène, le terrain et la sauvegarde.
3. Y copier à la main un fichier de chaque autre sorte, et un `project.dvxproj` de nom `Frozen`.
4. Ne plus jamais modifier ce dossier.
