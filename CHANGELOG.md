# Historique des versions

Toutes les versions sont dans les [versions publiées](../../releases). Depuis la bêta 6, TitanOs se met à jour tout seul.

## Bêta 34 (0.34.0)

**Clé d'installation, comme Windows**

La clé ne contient plus le système : elle installe TitanOs sur le disque de l'ordinateur.

- Démarrée sur la clé, TitanOs ouvre un **assistant d'installation** plein écran, à la place du bureau. Les étapes :
  1. bienvenue ;
  2. choix du disque (modèle, taille, interne ou USB) ;
  3. confirmation en rouge (« tout sera effacé, Windows compris ») ;
  4. copie, avec une barre de progression ;
  5. « Retirez la clé et redémarrez ».
- **Le disque est entièrement repartitionné** :
  - une grande partition pour TitanOs et vos fichiers (FAT32, formatée à la taille du disque) ;
  - l'espace des applications (un tiers du disque, entre 8 et 64 Go), créé au premier démarrage. Debian s'y installe, avec sa barre de progression.
- Si TitanOs est déjà installé sur un disque, l'assistant propose aussi **« Mettre à jour le TitanOs déjà installé »**, sans toucher aux fichiers. La base Debian est copiée en même temps si elle manque.
- La clé d'installation n'écrit rien sur elle-même : ni dossier Utilisateur, ni espace des applications.

**Applications : rien ne s'installe tout seul**
- Google Chrome ne se télécharge plus tout seul à la première connexion.
- Dans le Terminal, trois commandes téléchargent le paquet officiel de chaque éditeur puis l'installent avec apt (sudo est demandé) :
  - `installer-chrome` ;
  - `installer-discord` ;
  - `installer-vscode`.
- `apt install nom` installe tout programme de Debian. La Logithèque reste là pour le faire à la souris.

**Mises à jour plus sûres**
- Le chargeur de démarrage reçoit maintenant l'emplacement réel du noyau sur le disque (demandé au noyau Linux). Les mises à jour marchent donc sur tous les disques, quelle que soit leur disposition. Avant, une image de taille différente bloquait la mise à jour.

## Bêta 33 (0.33.0)

**Nouvelle base : Debian 13**

TitanOs garde son bureau, ses chargeurs, son écran de démarrage et toutes ses applications. En dessous, Debian 13 (trixie) remplace le système autonome, réduit à l'essentiel (environ 330 paquets) :
- `apt` installe n'importe quel programme Linux ;
- systemd démarre les services ;
- NetworkManager gère le réseau ;
- PipeWire gère le son.

**Installation**
- Au premier démarrage, TitanOs installe Debian depuis la clé (fichiers `DEBIAN.TXZ` et `DEBS.TAR`), avec une barre de progression sur l'écran de démarrage. Cela prend quelques minutes, une seule fois.
- Ensuite, `/init` bascule sur Debian à chaque démarrage, et systemd relance le bureau de TitanOs.
- Les TitanOs déjà installés téléchargent la base Debian (environ 300 Mo) au contrôle des mises à jour, puis l'installent au redémarrage.
- L'ancien espace des applications est effacé en tâche de fond. Les réseaux Wi-Fi enregistrés et le profil de Google Chrome sont repris.

**Applications**
- **Google Chrome** : le paquet officiel de Google, mis à jour avec Debian. Il tourne avec son bac à sable, sous le compte `titan`.
- **Logithèque** : par `apt`, avec la progression réelle du téléchargement et de l'installation.
  - LibreOffice, GIMP, Python (pip et venv) et Node.js viennent de Debian.
  - VS Code et Discord sont les paquets officiels de Microsoft et de Discord.
  - La recherche trouve tout paquet Debian.
- **Son des applications** : PipeWire, lancé avec la session de `titan`. Chrome, Discord et les autres ont le son sans réglage.

**Système**
- **Wi-Fi** : par NetworkManager.
  - Fonctionne avec WPA2, WPA3, WPA2-Entreprise et les réseaux masqués.
  - Les mots de passe ne passent jamais sur une ligne de commande.
  - La console réseau montre aussi les messages de NetworkManager, et les erreurs sont expliquées en français.
- **Câble et partage USB** : NetworkManager les configure tout seul.
- **Terminal** :
  - les commandes de Debian (`apt`, `nmcli`, `journalctl`, `systemctl`…) ;
  - `apt install` demande sudo tout seul ;
  - l'invite et les raccourcis de TitanOs sont gardés.
- **Mises à jour** : « Rechercher » met aussi à jour les programmes Debian.
- **Arrêt et veille** : par systemd, qui ferme proprement les services et les disques. Le bouton d'alimentation et le couvercle gardent le comportement de TitanOs.
- **Plus léger** :
  - sans paquets « recommandés » ;
  - sans mises à jour automatiques en tâche de fond ;
  - dpkg rapide sur clé USB ;
  - pilotes du noyau compressés (zstd).

**Sécurité**
- Si l'installation échoue, TitanOs s'ouvre sans Debian, comme avant, et `installation-debian.txt` explique pourquoi.
- Si le bureau ne démarre pas dans Debian, `journal-debian.txt` est copié sur la clé, puis TitanOs redémarre sans Debian jusqu'à la version suivante.
- Le paramètre de démarrage `titanos.nodebian` reste sur le système autonome.

Il faut maintenant une clé d'au moins 4 Go.

## Bêta 32 (0.32.0)

Version **0.32.0** (bêta 32). À partir de celle-ci, les versions suivent un numéro sémantique `0.bêta.correctif` (voir la section « Versions » du README).

**Fenêtres**
- **Ancrage** : une fenêtre lâchée contre le bord gauche ou droit prend la moitié de l'écran ; contre le haut, tout l'écran. Un aperçu en verre montre la place avant de lâcher, et la fenêtre glisse en douceur. Reprise à la souris, elle retrouve sa taille.
- **Vue d'ensemble** : souris dans le coin en haut à gauche, ou touche F3. Toutes les fenêtres glissent vers une grille, avec leur nom ; un clic en choisit une, Échap revient.

**Nouvelles fonctions**
- **Éclairage nocturne** (Paramètres → Écran) : Non, Toujours, ou Le soir (20 h à 7 h). Les couleurs deviennent plus chaudes (environ 3 400 K) en une trentaine de secondes. La carte graphique applique la teinte elle-même : rien à calculer pour le processeur.
- **Recherche de fichiers** dans le menu Démarrer : en plus des applications, les fichiers et dossiers de l'utilisateur (jusqu'à 4 niveaux), avec leur emplacement. Clic ou Entrée pour ouvrir.

**Paquets**
- **Recherche dans la Logithèque** : un mot, Entrée, et la Logithèque cherche dans les paquets d'Alpine et sur Flathub. « Installer » en un clic, avec une barre de progression. Les applications Flathub rejoignent le menu Démarrer avec leur icône. Les paquets Alpine s'utilisent dans le Terminal.
- **Terminal** : `apk add`, `apk del`, `apk upgrade` passent tout seuls par sudo.

## Bêta 31

**Fichiers : copier, couper, coller**
- Clic droit sur un fichier ou un dossier : **Copier**, **Couper**, **Coller** (aussi Ctrl+C, Ctrl+X, Ctrl+V). Clic droit dans le vide : Coller.
- Marche entre deux fenêtres Fichiers, d'une clé USB vers le disque et inversement, dossiers compris. Un nom déjà pris devient « nom (2) ».
- « Dupliquer » marche aussi sur les dossiers.

## Bêta 30

**Câble Ethernet**
- Le rapport de la console montre que la carte Marvell (pilote `sky2`) ne voit jamais le câble branché. Sur les VAIO, ses interruptions MSI se perdent : le changement de lien n'arrive jamais. Le pilote est maintenant chargé avec `disable_msi=1` (interruptions classiques), le correctif connu pour ces cartes.
- Si aucun lien n'apparaît, la carte est relancée toutes les 30 s pour réveiller sa partie physique.
- Note : certains réseaux d'école ou d'entreprise demandent une identification aussi sur le câble (802.1X). La console le montrera si c'est le cas.

**Wi-Fi**
- Le rapport confirme que la carte entend bien les réseaux (47 bornes). Les réseaux d'école (QUEST-…, JEDHA-…) demandent souvent un identifiant et un mot de passe : TitanOs les demande depuis la bêta 27.

**Console réseau**
- Les accents s'affichaient en codes (`\xc3\xa9`) : ils sont maintenant remis en clair.

## Bêta 29

**Connexion Wi-Fi : un vrai résultat**
- **« Connexion à… » ne tourne plus sans fin** : le message n'était jamais remplacé. Chaque connexion est maintenant suivie jusqu'au bout. Pendant l'attente, l'étape en cours s'affiche avec les secondes : « tentative de connexion… », « associé, échange des clés… », « demande d'une adresse… ».
- À la fin, un résultat clair :
  - « Connecté à X · adresse 192.168.1.20 » ;
  - « mot de passe refusé » ;
  - « la borne ne répond pas (trop loin ?) » ;
  - « la borne refuse la connexion » ;
  - « connecté, mais la box ne donne pas d'adresse » ;
  - ou, au bout de 35 s, « la connexion n'aboutit pas », avec la dernière raison connue.

**Console réseau**
- Nouveau bouton **Console** dans Réseau : tout ce que font la carte Wi-Fi, wpa_supplicant, le câble et le DHCP, en direct et heure par heure. Les erreurs sont en rouge, les réussites en vert.
- Les événements de wpa_supplicant sont expliqués en français. Exemples : `CTRL-EVENT-SSID-TEMP-DISABLED reason=WRONG_KEY` → « mot de passe refusé » ; `reason=15` → « échange de clés expiré : mot de passe probablement faux » ; `status_code=17` → « trop d'appareils connectés ».
- **Diagnostic complet** ajoute l'état entier du réseau : adresses, routes, DNS, état de wpa_supplicant, liaison Wi-Fi (signal, débit), pays de la radio, test d'Internet et des noms.
- **Enregistrer** crée `Documents/console-reseau.txt`, à envoyer pour obtenir de l'aide.
- Les messages de TitanOs ne sont plus limités par le noyau : aucun ne se perd, même quand il y en a beaucoup.

## Bêta 28

**Google Chrome remplace Chromium**
- Le vrai **Google Chrome** est installé par Flatpak, depuis Flathub (`com.google.Chrome`). Flatpak télécharge le paquet officiel de Google et lui fournit les bibliothèques qu'il attend (glibc), qu'Alpine n'a pas.
- Il s'installe tout seul à la première connexion à Internet : environ 1 Go à télécharger, une fois pour toutes. Une barre fluide suit l'installation.
- L'ancien Chromium est désinstallé dès que Chrome est en place. En attendant, s'il était déjà là, il reste utilisable.
- Chrome peut ouvrir et enregistrer des fichiers sur la clé, le disque et les clés USB (`/media`). Les téléchargements vont dans *Téléchargements*.
- Son icône dans le dock, le menu et sur le bureau est celle de Google Chrome.

**Applications sans droits d'administrateur, avec le son**
- Google Chrome, LibreOffice, VS Code, Discord et GIMP tournent maintenant sous le compte **titan**, comme le Terminal, et non plus en root.
- La session de titan a son bus D-Bus et un serveur de son (PulseAudio). Les vidéos de Chrome (YouTube…) et Discord ont le son. Le son passe par la sortie du haut-parleur, avec mélangeur logiciel ; il est libéré après 3 s de silence pour les sons de TitanOs.

**Wi-Fi**
- **« Actualiser » ne tourne plus à l'infini** : le message « Recherche des réseaux Wi-Fi… » n'était jamais remplacé. Au bout de 5 s, il indique le nombre de réseaux trouvés.
- Si aucun réseau n'est entendu après 15 s, la liste le dit et donne quoi vérifier, au lieu de rester sur « Recherche… ».
- **Mot de passe redemandé** : un réseau enregistré dont le mot de passe ne marche plus (refusé, ou pas de connexion après 25 s) redemande son mot de passe, ou son identifiant pour les réseaux d'entreprise. Avant, il fallait penser à « Oublier » le réseau.
- Clic droit sur un réseau enregistré : nouveau choix « Changer le mot de passe… ».
- Le voyant Wi-Fi du VAIO reste allumé tant que la radio est active, comme sous Windows : ce n'est pas un plantage.

**Bureau**
- Les noms d'icônes trop longs passent sur deux lignes (« Google / Chrome ») au lieu d'être coupés.

## Bêta 27

**Wi-Fi : tous les types de réseaux**
- **Réseaux avec identifiant et mot de passe** (WPA2-Entreprise : eduroam, écoles, entreprises) : TitanOs demande l'identifiant, souvent une adresse e-mail, puis le mot de passe. Méthodes PEAP et TTLS avec MSCHAPv2. Avant, seul le mot de passe était demandé et la connexion échouait.
- **WPA3** : les réseaux qui n'acceptent que WPA3 (SAE), comme le partage de connexion de beaucoup de téléphones récents, sont reconnus. TitanOs s'y connecte en WPA3, avec la protection des trames de gestion. Les réseaux WPA2 et WPA2/WPA3 restent en WPA2, comme avant.
- Sous chaque réseau, son type : « Mot de passe (WPA2) », « WPA3 », « Identifiant et mot de passe », ou « Ouvert ».
- **Réseaux ouverts** (FreeWifi, SFR WiFi FON...) : un message rappelle que leur page d'identification s'ouvre dans Chromium.
- **Réseau masqué…** : nouveau bouton pour se connecter à un réseau qui ne diffuse pas son nom.
- Si wpa_supplicant est occupé et refuse une recherche, TitanOs la demande directement au noyau.

**Partage de connexion d'un téléphone**
- **Par USB** : pris en charge depuis la bêta 25 (RNDIS et NCM pour Android, iPhone avec ipheth). Branchez le câble, puis activez « Partage de connexion via USB » sur le téléphone. Internet passe alors par le téléphone, avant le Wi-Fi.
- **Par Wi-Fi** : si le point d'accès du téléphone n'apparaît pas, ou si la connexion échoue, réglez-le sur la bande **2,4 GHz** et la sécurité **WPA2-Personnel** (ou WPA2/WPA3). La carte Intel 6200 date de 2010 : elle n'entend pas certains canaux 5 GHz, et ne fait pas toujours WPA3.

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
