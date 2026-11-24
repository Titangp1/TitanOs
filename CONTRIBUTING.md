# Contribuer à TitanOs

Merci de votre intérêt ! TitanOs est un projet personnel de **Titangp1**, ouvert à toutes les contributions.

## Comment proposer une modification

1. Créez une copie du dépôt (*fork*) et une branche pour votre modification.
2. Vérifiez que `python build.py` et `python build.py preview` passent sans erreur.
3. Ouvrez une *pull request* qui explique ce que vous changez et pourquoi.

L'auteur relit chaque proposition et décide de l'intégrer ou non. C'est lui qui fixe la direction du projet, publie les versions et gère les mises à jour automatiques.

## Style

- Le code est en C11, sans bibliothèque. Seules les fonctions de `src/core/lib.c` et les appels système de `src/linux/sys.h` sont disponibles.
- Les commentaires et les textes de l'interface sont en français.
- Une fonction fait une chose, avec un commentaire qui dit pourquoi elle existe.

## Licence de vos contributions

TitanOs est distribué sous la licence **GNU GPL version 3 ou ultérieure** (voir [LICENSE](LICENSE)).

En proposant une contribution, vous acceptez que :
- elle soit distribuée sous cette même licence ;
- vous en restiez l'auteur, mais **Titangp1, auteur et responsable du projet, puisse l'intégrer, la modifier et la distribuer** avec TitanOs. Cela vaut aussi pour une future version de la GPL ou une autre licence libre, si le projet en change un jour.

## Nom et logo

Le nom **TitanOs** et son logo (la montagne bleu-violet) désignent le projet de Titangp1. Vous pouvez distribuer une version modifiée, comme le permet la GPL. Elle doit alors porter un autre nom ou indiquer clairement qu'il ne s'agit pas de la version officielle.
