# Addons locaux : contenu et outils de l'éditeur

Un addon Devex est un dossier de sources réutilisables : scènes, textures, matériaux et scripts,
avec un fichier `addon.dvxaddon` qui donne son identité. Il peut aussi proposer des commandes C#
dans l'éditeur. Cette version crée, installe, exporte, met à jour et désinstalle des dossiers
locaux. Les archives, le catalogue en ligne et la résolution de dépendances viendront ensuite.

Tout se passe dans **Project > Addons...**, qui ouvre la page Addons de Project Settings.

## Créer un addon

1. Cliquer **Create Addon...**. Seul le nom est obligatoire : l'id en découle si on le laisse vide,
   et la fenêtre dit ce qui manque ou ce qui sera créé.
2. L'addon est créé en place, dans le projet : le manifeste et un `README.md` sous
   `assets/addons/<id>`, et avec **Start with an example editor command**, une classe C# sous
   `code/addons/<id>`, ouverte aussitôt dans Script.
3. Attendre la compilation (ou **Ctrl+B** en compilation manuelle). Ses outils étant déjà activés,
   la commande d'exemple apparaît dans **Editor > Addon Tools**, et sa carte la liste.
4. Ajouter à ces deux dossiers des scènes, des textures, des scripts et d'autres commandes, comme
   partout ailleurs dans le projet.
5. Pour le partager, voir [Exporter](#exporter-mettre-à-jour-et-désinstaller).

Le bouton **Guide** de la page ouvre ce document en ligne.

## Installer et utiliser

1. Cliquer **Install from Folder...**, puis choisir le dossier contenant `addon.dvxaddon`.
2. Une fenêtre montre l'addon avant de le copier. Elle prévient s'il contient du code, qui compile
   avec le projet et s'exécute dans l'éditeur dès l'installation, et refuse les conflits (plus bas).
3. Attendre la compilation des scripts et les imports. En compilation manuelle, lancer **Ctrl+B**.
4. Le contenu est disponible dans **FileSystem > assets > addons > identifiant** ; le code se
   trouve sous **code > addons > identifiant**, inclus dans Game.csproj et le CMake du projet.
5. Une fois le C# chargé, la carte de l'addon liste ses commandes. Activer **Editor Tools** les fait
   apparaître dans **Editor > Addon Tools**, précédées du nom de l'addon.

Essayer le dossier [Scene Tools](../samples/addons/scene-tools) livré dans les sources du moteur.
Il contient une scène de cube et un script de rotation utilisables dans le jeu, ainsi que les
commandes **Create Spinning Cube** et **Center Selection**. **Ctrl+Z** annule une commande et
**Ctrl+Y** la rétablit. Une caméra reste nécessaire dans la scène du jeu pour voir le cube en Play.

L'activation des outils est enregistrée dans le `.dvxproj`, propre à chaque projet. La désactiver
retire ses commandes ; ses ressources et composants restent disponibles, comme dans Godot.
Une installation n'active pas automatiquement ses commandes, contrairement à une création. Les scripts du dossier `code` font
partie du code du projet et sont compilés même lorsque les commandes de l'addon sont désactivées.

**Refresh** relit les manifestes. Une carte signale un manifeste absent, invalide ou dont l'id ne
correspond pas au dossier. L'installation prépare les fichiers dans le cache avant de les déplacer,
refuse les liens et ne copie pas les caches du code (`bin`, `obj`, `.git`, etc.). Les fichiers
sources du package sont conservés.

Sont refusés avant toute copie, avec la raison : un dossier sans manifeste, un dossier situé dans
`assets` ou `code` du projet, un UUID de `.dvxmeta` déjà utilisé par le projet ou deux fois par le
package, et une source C++ déclarant `DEVEX_GAME_MODULE`. Un nom de composant C# déjà pris n'est vu
qu'au chargement du code : Output l'indique et le composant est ignoré.

## Exporter, mettre à jour et désinstaller

- **Export to Folder...** écrit le package dans un nouveau dossier `<id>` du dossier choisi, hors de
  `assets` et `code` : le contenu de `assets/addons/<id>` à sa racine, et `code/addons/<id>` dans
  `code/`. C'est ce dossier qu'on partage et qu'**Install from Folder...** prend dans un autre projet.
  Augmenter la version dans le manifeste à chaque publication, et ajouter un fichier `LICENSE`.
- **Update from Folder...** remplace tous les fichiers de l'addon par ceux d'un package de même id,
  après avoir montré les deux versions. Tout est remplacé ou rien : les fichiers absents du nouveau
  package disparaissent, et les changements faits dans le projet sont perdus. **Install from
  Folder...** sur un addon déjà présent propose la même chose. Garder les `.dvxmeta` d'une version à
  l'autre conserve les références des scènes.
- **Uninstall...** supprime définitivement les deux dossiers et le réglage du projet, après
  confirmation. Les onglets de leurs fichiers se ferment, avec leurs changements non enregistrés. Les
  scènes qui utilisent ses ressources ou ses composants les gardent, non résolus.

## Créer un package

```text
mon-addon/
  addon.dvxaddon
  README.md
  LICENSE
  assets/
    ... scènes, matériaux, textures et leurs .dvxmeta
  code/
    ... scripts C# et sources C++ réutilisables
```

```ini
[addon format=1 id="studio.scene-tools" name="Scene Tools" version="0.1.0" author="Mon studio" license="MIT" description="Outils et contenu réutilisables."]
```

`id`, `name` et `version` sont obligatoires. L'id est stable, en minuscules, commence par une
lettre et utilise lettres ASCII, chiffres, tirets, underscores et points. Les séparateurs de
chemins, `..` et les noms de périphériques Windows sont refusés. Auteur, description et licence
sont facultatifs ; ajouter le vrai texte de la licence dans `LICENSE` pour partager le package.

Tout ce qui n'est pas `code/` est installé sous `assets/addons/<id>/`. Le contenu de `code/` est
installé sous `code/addons/<id>/`. Par exemple, `assets/Cube.dvxscene` devient
`res://assets/addons/<id>/assets/Cube.dvxscene`. Garder les `.dvxmeta` pour conserver les UUID des
ressources et leurs références. Les noms simples
des classes de composants C# doivent être uniques : les préfixer avec le nom de l'addon. Utiliser
aussi un namespace propre pour les commandes et les autres classes. Les sources C++ rejoignent le module du projet : un addon
de composants ne fournit pas un deuxième point d'entrée `DEVEX_GAME_MODULE`.

## Ajouter une commande C#

Placer ce fichier dans le dossier `code/` du package :

```csharp
using Devex;

namespace MonStudio.SceneTools;

public static class Tools
{
    [EditorCommand("studio.scene-tools", "Centrer la sélection")]
    public static void Center(EditorContext context)
    {
        foreach (Entity entity in context.Selection)
            if (entity.HasTransform)
                entity.Transform.Position = Vec3.Zero;
    }

    [EditorCommand("studio.scene-tools", "Créer un repère")]
    public static void CreateMarker(EditorContext context)
    {
        Entity marker = context.CreateEntity("Repère");
        marker.Transform.Position = new Vec3(0, 1, 0);
    }
}
```

Une commande est une méthode **publique, statique, synchrone**, retournant `void` et recevant
un `EditorContext`. Son attribut nomme exactement l'id du manifeste. `context.Scene` donne la
scène éditée ; `context.Selection` contient les entités sélectionnées ; `context.CreateEntity`
crée une entité racine avec un Transform. Les APIs de composants C# et natifs s'utilisent comme
dans les scripts. Start, Update et les systèmes du jeu ne sont pas lancés par cette commande.
Une signature incorrecte, ou un id que le projet n'a pas, est signalé dans Output.

L'éditeur exécute la commande sur une copie de la scène. Si elle réussit et change cette copie,
le changement complet est appliqué en une étape d'historique. Si elle lève une exception, ses
changements de scène sont abandonnés et l'erreur arrive dans Output. L'historique garde du texte
de scène pour survivre au rechargement du code. Le contexte et les vues de composants sont valides
uniquement pendant la commande. L'annulation couvre la scène ; elle ne couvre pas les fichiers
qu'un script écrit lui-même ni ses autres effets externes.

Les outils restent accessibles pendant un Play séparé et modifient la scène de l'éditeur. Ils
sont désactivés pendant le Play intégré. Les commandes sont relues après une compilation C# et
retirées au déchargement du projet. Les panneaux personnalisés, les inspecteurs d'addon, les
callbacks permanents et les plugins natifs indépendants ne font pas encore partie de cette API.
