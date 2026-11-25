# Historique des versions

Toutes les versions sont dans les [versions publiées](../../releases). Depuis la bêta 6, TitanOs se met à jour tout seul.

## Bêta 26

**Terminal sans droits d'administrateur, et sudo**
- **Faille corrigée** : le Terminal ouvrait un shell **root** sans aucun mot de passe, et l'invite affichait « titan » au lieu de « root ». Il tourne maintenant sous le compte **titan** (uid 1000), sans droits d'administrateur.
- **`sudo commande`** lance une commande en administrateur après le **mot de passe de session**, celui de l'écran de verrouillage (Paramètres → Compte). Comme le sudo habituel : trois essais au plus, réussite gardée 5 minutes par terminal, `sudo -i` pour un shell root, `sudo -k` pour l'oublier tout de suite. Chaque commande est notée dans le journal.
- Sans mot de passe de session, sudo refuse et explique comment en définir un.
- L'invite montre qui vous êtes : `titan@titanos:~$` en vert, `root@titanos:~#` en rouge.
- Le dossier personnel est `/home/titan`, avec des raccourcis vers vos dossiers (Documents, Images, Musique, Téléchargements, Vidéos). Il est gardé d'un démarrage à l'autre dans l'espace des applications.
- Vos fichiers sur la clé ou le disque, et ceux des clés USB branchées, appartiennent à titan : vous pouvez les modifier depuis le Terminal sans sudo.
- Le dossier de root (`/root`) n'est plus lisible par titan.

**Commandes de base**
- **bash** devient le shell du Terminal : historique, flèches, complétion.
- Ajoutées à BusyBox : **lsblk**, **lscpu**, **findmnt**, **lspci** et **lsusb** (avec les noms des appareils), **nano**, **less**, **htop**, **tree**, **zip**, **bc**, **pstree** et **killall**, **iw**.
- `aide` présente sudo et ces commandes. Les alias `eteindre` et `redemarrer` passent par sudo.

**Wi-Fi**
- La liste des réseaux reprend maintenant **toutes les bornes entendues par la carte**, lues directement dans le noyau (`iw scan dump`). Avant, seule la liste gardée par wpa_supplicant comptait.
- L'économie d'énergie de la radio est coupée dès que la carte est prête.
- Le journal compare chaque minute ce que voient wpa_supplicant et le noyau. Dans le Terminal, `sudo iw dev wlan0 scan | grep -E "SSID|signal"` lance une recherche complète et montre ce que la carte entend vraiment.

## Bêta 25

**Câble Ethernet et partage USB : refaits**
- **Le défaut principal** : quand le Wi-Fi avait déjà une route vers Internet, celle du câble était refusée (« la route existe déjà »). Le câble recevait une adresse, mais tout continuait de passer par le Wi-Fi.
- **Priorités** : chaque carte a maintenant sa priorité. Câble d'abord, puis partage USB d'un téléphone, puis Wi-Fi. Si plusieurs sont connectées, Internet passe par la meilleure. Si on la débranche, la suivante prend le relais.
- **Branchement surveillé** : le câble est surveillé en permanence. Branché, une adresse est demandée tout de suite. Débranché, son adresse et ses serveurs de noms sont retirés. Avant, l'adresse n'était demandée qu'une fois, au démarrage.
- **Serveurs de noms (DNS)** : chaque carte garde les siens, réunis dans le même ordre de priorité. Une carte ne peut plus effacer ceux d'une autre.
- **Renouvellement** : l'adresse est renouvelée sans couper la connexion (avant, elle était retirée puis remise).
- **Nom de l'ordinateur** : TitanOs se présente à la box sous le nom « titanos ».

**Wi-Fi**
- Réception de la carte Intel 6200 : la coexistence Bluetooth est désactivée (`bt_coex_active=0`). Sur les portables où un module Bluetooth séparé est câblé à la carte, elle réduit la sensibilité de la radio : peu de réseaux entendus. Le pays (France) est donné dès le chargement du pilote.
- Une fois connecté, la recherche de réseaux n'a lieu que toutes les 30 s : chaque recherche fait quitter un instant le canal de la box.
- Si le Wi-Fi reste déconnecté plus de 15 s, son adresse est retirée : Internet passe alors par le câble ou le téléphone.

**Application Réseau**
- « Tester Internet » teste chaque étape et dit ce qui ne va pas : pas d'adresse, box qui ne répond pas, Internet coupé, ou noms de sites (DNS).
- Chaque carte indique si Internet passe par elle. L'en-tête du Wi-Fi dit si Internet passe par le câble ou par le téléphone.
- Messages plus clairs : « Branché, demande d'une adresse… », « Partage de connexion désactivé sur le téléphone ».

**Journal (`journal-titanos.txt`)**
- Nouvelle partie « réseau » : adresses, routes, DNS, état de chaque carte (lien, vitesse), processus DHCP.
- Pour le Wi-Fi : pays réglé, liaison en cours, bornes vues par le noyau (outil `iw`) et par wpa_supplicant. Toutes les minutes : le nom, le signal (dBm) et le canal de chaque borne entendue.
- Chaque branchement, débranchement et adresse reçue est noté.

## Bêta 24

**Logithèque : installer des applications en un clic**
- Nouvelle application **Logithèque**, dans le menu Démarrer et le dock : **LibreOffice** (texte, tableur, présentations), **VS Code**, **Discord**, **GIMP**, **Python 3** et **Node.js**.
- LibreOffice et GIMP viennent d'Alpine. VS Code et Discord viennent de Flathub, par Flatpak. Python et Node.js s'ouvrent dans le Terminal.
- Une fois installées, les applications apparaissent dans le menu Démarrer avec leur propre icône. On peut les mettre sur le bureau, et les désinstaller depuis la Logithèque.
- Les applications graphiques ont chacune leur fenêtre TitanOs (un serveur X en mémoire par application). Leurs dialogues (Enregistrer sous…) sont centrés, leurs menus restent à leur place.
- Le bouton × demande d'abord à l'application de se fermer : elle peut proposer d'enregistrer. Un second clic force la fermeture.
- Les installations passent par une file : une à la fois, dans l'ordre.

**Chromium**
- **Téléchargements** : ils vont dans le dossier *Téléchargements*, visible dans Fichiers. Ils n'arrivent plus dans un dossier caché.
- **Son (YouTube…)** : la bibliothèque ALSA des applications est réglée sur la bonne sortie (le haut-parleur, pas le HDMI de la carte graphique), avec un mélangeur logiciel. Les sons de TitanOs libèrent la sortie dès qu'ils sont finis.
- Le pointeur prend la forme demandée par la page : main sur un lien, barre de texte dans un champ.
- Cache limité à 256 Mo.

**Pointeur**
- Nouveau pointeur, comme sur un Mac : flèche noire bordée de blanc, avec une ombre douce. Barre en I sur le texte (Terminal, Bloc-notes), double flèche dans le coin de redimensionnement, cercle qui tourne pendant qu'une application démarre.
- Il est affiché par la carte graphique (plan « curseur ») : il suit la souris tout de suite, même si l'image est en train d'être dessinée.

**Fluidité**
- Le mini-figement toutes les 1 à 2 secondes : la batterie était lue par l'ACPI toutes les 2 s. Sur certains portables, dont les VAIO, chaque lecture peut suspendre brièvement tout le processeur. Elle est maintenant lue toutes les 15 s, et tout de suite quand le noyau signale un changement (chargeur branché ou débranché).
- Clés USB, câble réseau et périphériques d'entrée sont signalés par le noyau, au lieu d'être recherchés plusieurs fois par seconde.
- Le journal compte les images lentes et les « retards du système » (processus réveillé en retard). Si un figement persiste, le journal dira d'où il vient.

**Barres de progression fluides**
- Les barres glissent vers la valeur réelle au lieu de sauter d'un état à l'autre, et avancent doucement entre deux nouvelles. Un reflet les parcourt. Si la durée est inconnue, un segment va et vient.
- Le téléchargement d'une mise à jour affiche un vrai pourcentage (« 12 Mo sur 33 »).

**Démarrage**
- Le chargeur BIOS accepte aussi les modes 16 et 24 bits. Beaucoup de cartes NVIDIA ne proposent la résolution de l'écran qu'en 16 ou 24 bits. TitanOs démarre alors directement en 1920×1080, au lieu d'un 1280×1024 étiré et flou. L'image est tramée : le dégradé reste lisse.
- La liste des modes proposés par la carte est notée dans le journal (`titanos.vbe=`). Si l'écran de démarrage est encore flou, elle montrera ce que le BIOS du PC permet.

**Place libérée**
- Après une mise à jour, le paquet téléchargé et ses fichiers (environ 100 Mo, en mémoire vive) sont effacés tout de suite.
- Au démarrage, TitanOs efface les restes de téléchargements et les rapports de plantage de Chromium.
- Les fichiers du système sont réécrits à leur place à chaque mise à jour : aucune ancienne version ne s'accumule sur le disque.

## Bêta 23

**Plus de types de fichiers**
- **Photos JPEG** : séquentielles (appareils photo, téléphones) et progressives (images du web), en couleurs ou en niveaux de gris. Une photo de 12 mégapixels s'ouvre en moins d'une seconde. Les photos prises en portrait sont remises droites (orientation EXIF).
- **GIF** (première image, transparence, entrelacement) et **PPM/PGM**, en plus de PNG et BMP.
- **Double-clic dans Fichiers** : chaque fichier s'ouvre avec la bonne application. Textes et code (txt, md, py, c, json, csv, log…) : Bloc-notes. Images : visionneuse. **PDF, vidéos (mp4, webm, mkv…), musique (mp3, ogg, flac, wav…), pages web, SVG et WebP** : Chromium. Fichier sans extension : Bloc-notes s'il contient du texte.
- **Archives** zip, tar, tar.gz, tar.xz, tar.bz2 et gz : un double-clic les extrait dans un dossier du même nom, à côté.
- Nouveau clic droit dans Fichiers : « Ouvrir avec le Bloc-notes ». Le menu est regroupé : ouvrir, modifier, créer.
- Le son est libéré dès que l'interface ne joue plus rien, pour que Chromium puisse s'en servir (vidéos, musique).

**Bureau en 1920×1080**
- La grille des icônes occupe tout l'écran. Les cases sont réparties sur toute la largeur : une icône posée tout à droite est collée au bord, comme à gauche.
- **Raccourcis** : clic droit sur le bureau → « Ajouter une application au bureau… ». Clic droit sur une application du menu Démarrer → « Ajouter au bureau ». Clic droit sur un fichier ou un dossier dans Fichiers → « Ajouter au bureau ».
- **Retirer un raccourci** : clic droit sur l'icône → « Retirer du bureau ». Seul le raccourci disparaît : le fichier ou l'application reste.
- Les fenêtres redimensionnables (Fichiers, Bloc-notes, Terminal, Image, Réseau, Système) s'ouvrent 20 % plus grandes sur un écran 1080p.
- Correction : quand un menu en remplaçait un autre, un morceau de l'ancien restait affiché.

## Bêta 22

- **Menu Démarrer** : avec Chromium, la 3e ligne d'applications passait sous le nom de l'utilisateur. Le menu affiche maintenant 6 applications par ligne, et sa hauteur suit leur nombre.
- **Chromium en grand** : la fenêtre s'ouvre agrandie, et elle peut être agrandie, rétablie ou redimensionnée. TitanOs envoie la nouvelle taille au navigateur (requêtes X11 QueryTree, GetGeometry et ConfigureWindow).

## Bêta 21

**Mise à jour par clé USB**, sans Internet, de deux façons, dans l'application Mises à jour :
- **Depuis une clé TitanOs** : démarrez sur une clé qui a une version plus récente. TitanOs trouve celui qui est installé sur le disque et propose « Mettre à jour le disque ». Le disque prend la version de la clé, en gardant vos fichiers et vos applications.
- **Avec le fichier `titanos-maj.tar.gz`** (dans chaque version publiée) : copiez-le sur n'importe quelle clé USB et branchez-la. TitanOs propose « Installer ce paquet ». Les sommes SHA-256 sont vérifiées avant toute écriture.

Chaque installation note aussi son numéro de version dans `TITANOS/VERSION`.

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
