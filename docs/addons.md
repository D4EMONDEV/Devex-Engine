# Addons locaux : contenu et outils de l'éditeur

Un addon Devex est un dossier de sources réutilisables : scènes, textures, matériaux et scripts,
avec un fichier `addon.dvxaddon` qui donne son identité. Il peut aussi proposer des commandes C#
dans l'éditeur. Cette première version installe des dossiers locaux. Un catalogue en ligne, les
archives, les mises à jour et la résolution de dépendances viendront ensuite.

## Installer et utiliser

1. Ouvrir **Project > Project Settings > Addons**.
2. Cliquer **Install from Folder...**, puis choisir le dossier contenant `addon.dvxaddon`.
3. Attendre la compilation des scripts et les imports. En compilation manuelle, lancer **Ctrl+B**.
4. Le contenu est disponible dans **FileSystem > assets > addons > identifiant** ; le code se
   trouve sous **code > addons > identifiant**, inclus dans Game.csproj et le CMake du projet.
5. Pour les commandes de l'éditeur, activer **Editor Tools** sur la carte de l'addon. Les commandes
   apparaissent dans **Editor > Addon Tools**, précédées du nom de l'addon, une fois le C# chargé.

Essayer le dossier [Scene Tools](../samples/addons/scene-tools) livré dans les sources du moteur.
Il contient une scène de cube et un script de rotation utilisables dans le jeu, ainsi que les
commandes **Create Spinning Cube** et **Center Selection**. **Ctrl+Z** annule une commande et
**Ctrl+Y** la rétablit. Une caméra reste nécessaire dans la scène du jeu pour voir le cube en Play.

L'activation des outils est enregistrée dans le `.dvxproj`, propre à chaque projet. La désactiver
retire ses commandes ; ses ressources et composants restent disponibles, comme dans Godot.
Une installation n'active pas automatiquement ses commandes. Les scripts du dossier `code` font
partie du code du projet et sont compilés même lorsque les commandes de l'addon sont désactivées.

**Refresh** relit les manifestes. Une carte signale un manifeste absent, invalide ou dont l'id ne
correspond pas au dossier. L'installation refuse de remplacer un addon existant. Elle prépare
les fichiers dans le cache avant de les déplacer, refuse les liens et ne copie pas les caches
du code (`bin`, `obj`, `.git`, etc.). Les fichiers sources du package sont conservés.

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
ressources et leurs références ; les UUID doivent être uniques dans le projet. Les noms simples
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
