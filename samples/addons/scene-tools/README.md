# Scene Tools

Exemple d'addon Devex sous licence MIT, comme le moteur.

Dans Project Settings > Addons, installer ce dossier. Les fichiers apparaissent dans
`assets/addons/devex.scene-tools` et `code/addons/devex.scene-tools`. Une fois le code compilé,
activer Editor Tools. **Editor > Addon Tools** propose les commandes de Scene Tools : création d'un cube
qui tourne pendant Play et le centrage de la sélection. Ctrl+Z annule toute la commande.

La scène `assets/SpinningCube.dvxscene` peut aussi être réutilisée comme prefab. Elle contient
un cube et le composant `AddonSpinner`, dont la vitesse se règle dans l'Inspector.
