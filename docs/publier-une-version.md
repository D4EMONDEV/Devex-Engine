# Publier une version

Un tag `v<version>` poussé fait tout construire et tester par la CI, qui prépare une release en **brouillon**. Rien ne part en
public tant qu'on ne l'a pas relue et publiée d'un clic sur GitHub.

## Préparer

1. Monter la version dans `CMakeLists.txt`, et `engine="…"` dans `samples/sandbox/Sandbox.dvxproj`.
2. Figer les formats de cette version, comme le dit [À chaque version publiée](compatibilite.md#à-chaque-version-publiée).
3. Ajouter à [CHANGELOG.md](../CHANGELOG.md) une section `## <version> — <date>` : elle devient le texte de la release.
4. Tout compiler et lancer les tests.

## Publier

Commiter, pousser `main`, puis pousser le tag. Sans le tag, la CI ne publie rien.

```bash
git tag v0.9.0
```

```bash
git push origin v0.9.0
```

La CI fait ses vérifications habituelles sur Windows, Linux et Android. Si toutes passent, le job *Draft the release* :

- vérifie que le tag, `CMakeLists.txt`, le bac à sable, les formats figés et le journal disent la même version ;
- met dans chaque paquet le moteur de l'autre système, dans `platforms/`, avec ce qu'un jeu demande et rien de plus ;
- construit le zip et l'installateur pour Windows, et le `.tar.gz` pour Linux ;
- ajoute `SHA256SUMS.txt`, et crée la release en brouillon.

Ensuite, sur la page *Releases* du dépôt, relire le texte, essayer un fichier, et publier.

Si quelque chose ne va pas, supprimer le brouillon et le tag, corriger, et recommencer :

```bash
git push origin --delete v0.9.0
```

## Chez ceux qui ont déjà Devex

L'installateur remplace la version installée : il garde le même identifiant d'une version à l'autre, et vide `bin`, `lib`, `include`,
`cmake` et `platforms` avant de copier les nouveaux fichiers. Les projets et les réglages n'y sont pas.

La liste des projets annonce une version dès qu'elle est **publiée** : un brouillon n'est pas visible, et chacun l'apprend à sa
prochaine vérification, une fois par jour au plus. Un tag sans chiffres et points seuls, comme `v1.0.0-rc1`, n'est jamais annoncé.

## Ce que contient une release

| Fichier | Pour qui |
| --- | --- |
| `devex-<version>-windows-x64-setup.exe` | L'installateur, sans droits administrateur : raccourci, double-clic sur les `.dvxproj`, désinstallation. |
| `devex-<version>-windows-x64.zip` | Le même moteur, à extraire où l'on veut. |
| `devex-<version>-linux-x64.tar.gz` | Le moteur pour Linux, à extraire. |
| `SHA256SUMS.txt` | Pour vérifier les fichiers. |

## Sans signature

L'installateur n'est pas signé : un certificat coûte de l'argent chaque année, et SmartScreen affiche « Windows a protégé votre
ordinateur » jusqu'à ce que le fichier soit assez connu. Il faut cliquer sur *Informations complémentaires*, puis *Exécuter quand
même*. Les notes de chaque version le rappellent.

## Essayer sur sa machine

Inno Setup s'installe avec `winget install JRSoftware.InnoSetup`. Le script fait tout sauf la release, tant qu'on ne lui passe pas
`-Draft` :

```bash
cmake --install out/build/x64-release --prefix out/install/windows
```

```bash
pwsh scripts/prepare_release.ps1 -Version 0.9.0 -Windows out/install/windows -Output out/release -Iscc "$LOCALAPPDATA/Programs/Inno Setup 6/ISCC.exe"
```
