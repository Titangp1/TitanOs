# Historique des versions

Toutes les versions sont dans les [versions publiées](../../releases). Depuis la bêta 6, TitanOs se met à jour tout seul.

## Bêta 20

- La recherche de mise à jour n'utilise plus l'API de GitHub. Sans compte, celle-ci n'accepte que 60 demandes par heure ; au-delà, elle répondait « erreur 403 ». TitanOs lit maintenant le numéro de version directement dans la dernière version publiée, par un lien qui n'a pas cette limite.
- Cliquer plusieurs fois sur « Rechercher maintenant » ne relance plus la recherche pendant une minute.
- Si GitHub refuse quand même, le message l'explique clairement, au lieu d'afficher « erreur 403 ».

## Bêta 19

- TitanOs est maintenant distribué sous la licence **GNU GPL version 3** (voir LICENSE, NOTICE et CONTRIBUTING.md).
- Une fois TitanOs installé sur le disque, Système et Fichiers indiquent « Disque interne (modèle du disque) » au lieu de « Clé USB TITANOS ».

## Bêta 18

**Bureau**
- Les icônes du bureau se déplacent à la souris. Elles se rangent dans une grille, échangent leur place si la case est prise, et la gardent d'un démarrage à l'autre.

**Navigateur**
- Chromium, la base libre de Google Chrome, est intégré : icône sur le bureau et dans le dock.
- Il s'installe tout seul dès la première connexion à Internet : environ 370 Mo à télécharger, 900 Mo dans l'espace des applications.
- Il s'affiche dans une fenêtre TitanOs grâce à un serveur X en mémoire (Xvfb) et à un petit client X11 écrit pour TitanOs, qui transmet la souris et le clavier (extension XTEST).

**Carte graphique et 60 Hz**
- La boucle d'affichage attendait 16 ms de trop après chaque image synchronisée : pendant les animations, une synchronisation sur deux était ratée (≈ 30 images par seconde). C'est maintenant l'écran qui donne le rythme.
- Mélanges de pixels calculés 4 par 4 (SSE2) : l'animation des menus est 3 fois plus rapide, le déplacement d'une fenêtre 5 fois plus rapide.
- Copie vers la mémoire de la carte sans passer par le cache du processeur.

## Bêta 17

**Réseau**
- Les interrupteurs radio ne sont plus réécrits en boucle. Avant, TitanOs réécrivait « débloquer la radio » toutes les 2 secondes, sur chaque interrupteur. Sur le VAIO, le pilote `sony-laptop` transmettait chaque écriture au contrôleur, qui pouvait relancer la carte Wi-Fi.
- Une adresse est demandée (DHCP) dès que le Wi-Fi se connecte, au lieu d'attendre le prochain essai.
- Les réseaux masqués sont acceptés.

**Affichage**
- Double tampon synchronisé sur le rafraîchissement de l'écran (60 Hz) : plus de déchirure, et les animations suivent le rythme de l'écran.
- La fréquence de l'écran est affichée dans Système et Paramètres.

**Animations**
- Les fenêtres s'ouvrent et se ferment avec un léger zoom, se réduisent vers leur icône du dock et en reviennent.
- L'agrandissement d'une fenêtre est animé.

**Paramètres**
- L'application est réorganisée en catégories : personnalisation, écran, son, clavier et souris, réseau, date et heure, compte, système.

**Horloge**
- Après chaque synchronisation, l'heure exacte est écrite dans l'horloge du PC, en temps universel. Avant, il y avait 1 h et quelques minutes de trop après un redémarrage.
- TitanOs se met à l'heure tout seul dès qu'Internet est disponible.

**Nouveautés**
- Capture d'écran (touche Impr. écran), enregistrée en PNG dans *Images*.
- Visionneuse d'images (PNG et BMP).
- Clés USB : montage automatique, affichage dans Fichiers et bouton *Éjecter*. FAT, exFAT, NTFS et ext4 sont pris en charge.
- Le Bloc-notes avertit avant de fermer un texte non enregistré.
- Mémoire compressée (zram).
- Pilote Intel `i915` inclus pour les autres PC.

**Fiabilité**
- Les réglages sont enregistrés de façon sûre : fichier temporaire, puis renommage.
- Le journal sur la clé est écrit moins souvent après les 4 premières minutes.
- Le mot de passe de session est protégé par PBKDF2-HMAC-SHA256 avec un sel aléatoire.

## Bêta 16
- Le bouton en bas à gauche ouvre un menu : mettre en veille, redémarrer ou éteindre.
- Mot de passe de session, demandé à l'écran de verrouillage.
- Plus de recherche de réseaux Wi-Fi pendant une connexion.

## Bêta 15
- Les réseaux Wi-Fi entendus une fois restent listés pendant 10 minutes.
- Les recherches automatiques s'espacent progressivement.

## Bêta 14
- Les images de démarrage aux bonnes proportions sont utilisées même sans EDID.
- La recherche Wi-Fi ne se lance plus pendant une connexion.

## Bêta 13
- Bouton d'arrêt sur le bureau.
- Démarrage depuis une clé alors que TitanOs est aussi sur le disque : c'est bien la clé qui est utilisée.

## Bêta 12
- Écran de démarrage aux bonnes proportions quand le BIOS n'offre pas la résolution de l'écran.
- Liste des disques corrigée.

## Bêta 11
- Installation sur une clé USB ou un autre disque, depuis n'importe quel TitanOs.

## Bêta 10
- Wi-Fi : les modules de chiffrement du noyau (CCM, GCM, CMAC…), indispensables au WPA2, sont inclus.

## Bêta 9
- Mises à jour en HTTPS avec `curl`, avec vérification des certificats.
- Mot de passe Wi-Fi refusé signalé ; journal mis à jour pendant toute la session.

## Bêta 8
- Wi-Fi réglé pour la France (puissance et canaux complets).
- Diagnostic précis quand Internet ne répond pas.

## Bêta 7
- Bouton *Afficher* pour le mot de passe Wi-Fi.
- « Oublier ce réseau » corrigé.

## Bêta 6
- Mises à jour automatiques depuis GitHub.
- Installation sur le disque interne.
- Bureau plus fluide : ombres en cache, verre figé pendant un déplacement.

## Bêta 5
- Correction majeure : la liste des pilotes était lue deux fois, ce qui écrasait la mémoire. Aucun pilote ne se chargeait, d'où l'écran figé des premières bêtas.

## Bêtas 3 et 4
- Bêtas de diagnostic : ligne d'état à l'écran, journal sur la clé, rapport de plantage en mode de secours.

## Bêta 2
- Animation de démarrage propre à TitanOs : les crêtes.

## Bêta 1
- Première version : noyau Linux d'Alpine, chargeurs BIOS et UEFI, bureau liquid glass, applications, sons.
