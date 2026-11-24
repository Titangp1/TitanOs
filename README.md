# TitanOs — bêta 14

Système d'exploitation **léger** sur base Linux, avec un bureau **liquid glass** écrit en C,
sans bibliothèque, et pensé pour le **Sony VAIO série F** (Core i7, GeForce GT 330M).

Seul le **noyau Linux** vient d'ailleurs (paquet `linux-lts` d'Alpine Linux, pris tel quel).
Tout le reste est écrit pour TitanOs : les chargeurs de démarrage (BIOS et UEFI), `/init`, le
chargement des pilotes, le bureau, les animations, les sons et les applications.
Quelques programmes d'Alpine sont embarqués pour ce qui ne se réinvente pas : BusyBox (commandes du
Terminal), `wpa_supplicant` (Wi-Fi), `apk` (installer des applications) et `mke2fs`.

![Démarrage](docs/captures/demarrage.png)

![Bureau](docs/captures/bureau.png)

## Ce qu'il fait

- **Démarrage soigné, sans coupure** : dès l'allumage, le chargeur affiche le logo, le nom et les
  **crêtes**, l'animation propre à TitanOs : cinq petits sommets, comme celui du logo, qui grandissent
  et s'éclairent l'un après l'autre pendant qu'il lit la clé. Linux reprend exactement la même image,
  puis l'écran s'efface en fondu sur l'écran de verrouillage, avec un petit son de démarrage.
- **Bureau liquid glass** : chaque panneau floute ce qui est derrière lui et réfracte le fond sur ses
  bords. Il a aussi un liseré lumineux et un reflet. Trois niveaux de verre : transparent, normal, dépoli.
- **Animations** : ouverture et fermeture des fenêtres, réduction vers le dock, menus et calendrier en
  fondu, icônes du dock qui grandissent sous la souris, notifications qui glissent, déverrouillage qui
  remonte.
- **Sons** doux, synthétisés au démarrage (aucun fichier audio) : clics, ouverture et fermeture de
  fenêtres, notifications, verrouillage, démarrage.
- **Intégration au portable** :
  - batterie dans la barre ;
  - touches Fn du VAIO : luminosité et volume, avec un indicateur à l'écran ;
  - couvercle rabattu, qui verrouille la session ;
  - bouton d'alimentation, qui propose d'éteindre ;
  - températures du processeur et de la carte NVIDIA, fréquence du processeur.
- **Wi-Fi** (Intel Centrino 6200 ou Atheros AR9287 du VAIO) : liste des réseaux, mot de passe,
  réseaux enregistrés. Si l'interrupteur **WIRELESS** du VAIO (bord avant) est sur OFF, l'application
  Réseau le signale. Aussi l'**Ethernet** (Marvell Yukon) et le **partage de connexion USB** d'un téléphone.
- **Applications Linux** : `apk add htop` dans le Terminal installe un programme d'Alpine. Au premier
  démarrage, TitanOs crée un espace pour les applications dans la place libre de la clé : elles y restent
  installées.
- Vos fichiers et réglages sont gardés sur la clé, dans `Utilisateur/` et `titanos.cfg`.
- **Mises à jour automatiques** : une fois connecté à Internet, TitanOs regarde s'il existe une version plus
  récente dans les [versions publiées](../../releases) sur GitHub. Si oui, il la télécharge, vérifie ses sommes
  SHA-256 et l'installe tout seul ; il suffit ensuite de redémarrer. L'application **Mises à jour** permet aussi
  de lancer la recherche.
- **Installation sur le disque** : depuis la clé, l'application **Mises à jour** copie TitanOs sur le disque
  interne choisi, après confirmation. **Tout le disque est effacé** (Windows compris). Ensuite, l'ordinateur
  démarre sans la clé.

![Réseau et Wi-Fi](docs/captures/reseau.png)

![Système](docs/captures/systeme.png)

![Mises à jour](docs/captures/mises-a-jour.png)

### Applications

| Application | Rôle |
|---|---|
| **Fichiers** | dossiers, nouveau dossier/fichier, renommer (F2), dupliquer, supprimer avec confirmation |
| **Terminal** | vrai terminal (VT100/xterm, 256 couleurs, historique) ; `aide` liste les commandes |
| **Bloc-notes** | édition UTF-8, sélection, copier/coller, Ctrl+S / Ctrl+O |
| **Calculatrice** | souris ou clavier |
| **Paramètres** | fond, couleur, verre, nom, secondes ; clavier FR/US, pointeur, luminosité, volume, sons, fuseau horaire, heure Internet |
| **Système** | machine, températures, batterie, matériel PCI/USB avec son pilote, processus |
| **Réseau** | Wi-Fi, câble, partage USB, test de connexion |
| **Mises à jour** | version installée, mises à jour depuis GitHub, installation sur le disque interne |
| **Snake**, **À propos** | |

Raccourcis : touche Windows (menu et recherche), Alt+Tab, Alt+F4, Ctrl+Alt+T (Terminal), Windows+L (verrouiller).

## Installer sur une clé USB

1. Télécharger `titanos-beta14.img.zip` dans les [versions publiées](../../releases), puis le décompresser.
2. Écrire `titanos.img` sur une clé USB avec **Rufus, en mode DD**. Cela efface la clé.
   Prenez une clé d'au moins 2 Go : la place libre accueillera les applications.
3. **Sony VAIO (BIOS)** : touche F2, onglet Boot, activer **External Device Boot** et mettre la clé
   en premier. **PC récent (UEFI)** : désactiver **Secure Boot**.

Il faut 512 Mo de mémoire au minimum.

## Compiler

Prérequis : [LLVM](https://llvm.org) (clang, ld.lld, lld-link) et Python 3.14 ou plus récent.

```powershell
winget install LLVM.LLVM
```

```powershell
python build.py
```

Au premier lancement, le build télécharge environ 200 Mo et les garde dans `build/cache/` :
- depuis le miroir officiel d'Alpine : noyau, BusyBox, paquets, police Inter, tous vérifiés par leurs
  sommes de contrôle ;
- depuis le miroir officiel de linux-firmware : le micrologiciel Wi-Fi Intel, vérifié par son SHA-256.

Le résultat est `build/titanos.img` (256 Mo, démarre en BIOS et en UEFI) et sa version VHD.

```powershell
python build.py preview
```

Cette commande compile le bureau pour Windows et enregistre des captures dans `build/preview/`.

```powershell
python tools/testboot.py build/titanos.img build/preview/chargeur.png
```

Ce test exécute le MBR et le chargeur BIOS dans un émulateur de processeur, jusqu'à l'entrée du noyau
Linux. Il vérifie la mémoire et capture l'écran dessiné. Il demande `pip install unicorn`.

## Comment c'est construit

```
                 ┌─ BIOS ─► MBR (boot/mbr.S) ─► stage2 (boot/stage2.S) ─┐
PC allumé ──────►│                                                       ├─► noyau Linux ─► /init (TitanOs)
                 └─ UEFI ─► BOOTX64.EFI (boot/efiboot.c) ────────────────┘
```

- **stage2** :
  - choisit le meilleur mode graphique VBE et dessine l'écran de démarrage (images de `TITANOS/BOOT.IMG`) ;
  - lit le noyau et le système par blocs, en mode « unreal », en animant les crêtes au rythme de
    l'horloge du BIOS ;
  - remplit la carte mémoire et entre dans Linux par son protocole 32 bits.
- **efiboot** : dessine le même écran, puis passe le système au noyau par le protocole LoadFile2.
- **`/init`** (PID 1) :
  - monte `/proc`, `/sys` et `/dev`, et installe les commandes BusyBox ;
  - charge les pilotes des périphériques branchés à chaud ;
  - relance le bureau s'il s'arrête, et ouvre un mode de secours (shell) s'il plante en boucle.
- **La session** anime l'écran de démarrage pendant qu'un processus voisin charge les pilotes (la carte
  graphique d'abord), monte la clé et prépare l'espace des applications. Elle lance ensuite le son,
  le réseau et un « veilleur » : ce processus à part interroge le Wi-Fi et la batterie, qui peuvent
  être lents, et envoie leur état au bureau. Le bureau ne peut donc jamais attendre le matériel.

## Personnaliser

| Pour… | Modifier |
|---|---|
| ajouter une application | une fonction `app_xxx_open()` (voir `src/core/apps.c`), son icône dans `icons.c`, son nom dans `apps.c` et `desk.h` |
| changer l'effet verre | `style_for()` dans `src/core/wm.c`, et `src/core/glass.c` |
| fonds d'écran, couleurs, fuseaux | `src/core/settings.c` |
| les sons | `src/core/sound.c` (chaque son est une petite partition de notes) |
| ajouter un pilote ou un programme d'Alpine | `MODULES` et `PACKAGES` dans `tools/buildos.py` |
| les commandes du Terminal | `rootfs/etc/profile` |

## Organisation du code

```
build.py              compile tout ; « preview » pour les captures
boot/                 MBR, chargeur BIOS (stage2.S), chargeur UEFI (efiboot.c)
src/core/             le bureau, indépendant du système
  gfx.c glass.c icons.c   dessin lissé, verre, logo et icônes
  wm.c ui.c sound.c       fenêtres, dock, menus, animations, widgets, sons
  apps.c term.c files.c notes.c prefs.c sysapp.c netapp.c updapp.c   applications
src/linux/            ce qui parle au noyau (appels système directs, sans libc)
  main.c                  /init, démarrage, clé, espace des applications
  modules.c display.c input.c   pilotes, écran DRM/KMS, clavier/souris/pavé tactile
  audio.c wifi.c plat.c         son ALSA, Wi-Fi (wpa_supplicant), fichiers, système, batterie
  update.c                      mises à jour depuis GitHub, installation sur le disque
src/sim/sim.c         version Windows du bureau, pour l'aperçu
rootfs/etc/           profil du shell, script DHCP, dépôts apk
tools/                téléchargement Alpine, polices, initramfs, image disque, écran de démarrage, tests
```

## Si quelque chose ne va pas

- **En bas de l'écran de verrouillage**, si la clé, le clavier ou le pointeur manquent, une ligne de diagnostic indique le nombre de pilotes chargés,
  si la clé est montée, les claviers, souris et pavés tactiles trouvés, le nombre d'événements reçus
  (touches, mouvements) et le temps écoulé depuis le démarrage. Si ce temps s'arrête, le bureau est bloqué.
- **Sur la clé**, dès qu'elle est trouvée, TitanOs écrit `journal-titanos.txt` toutes les 5 secondes
  pendant 4 minutes : messages du noyau, pilotes, périphériques, et l'endroit où chaque processus attend.
  Branchez la clé sur Windows et envoyez ce fichier.

## Nouveautés de la bêta 14

- Écran de démarrage : les images aux bonnes proportions sont aussi utilisées quand le BIOS ne donne pas la résolution de l'écran (cas du VAIO).
- Wi-Fi : plus de recherche forcée pendant une connexion (elle pouvait l'interrompre) ; recherche seulement quand aucun réseau n'est visible.

## Nouveautés de la bêta 13

- Bouton d'arrêt en bas à gauche du bureau (avec confirmation).
- Démarrage depuis une clé alors que TitanOs est aussi installé sur le disque : c'est bien la clé qui est utilisée. TitanOs ne se rabattait plus sur le disque interne avant que la clé USB ait eu le temps d'apparaître.

## Nouveautés de la bêta 12

- **Écran de démarrage aux bonnes proportions** dès l'allumage : quand le BIOS n'offre pas la résolution de l'écran (VAIO : 1280×1024 étiré sur 1920×1080), le chargeur utilise des images préparées pour ce cas, qui une fois étirées ont exactement la forme et la place de l'écran en 1920×1080. Linux garde ensuite cette image jusqu'au pilote NVIDIA.
- Installation : tous les disques et clés sont listés, celui qui fait tourner TitanOs est indiqué (correction de la détection du disque de démarrage).

## Nouveautés de la bêta 11

- **Installer sur une clé USB ou un autre disque**, même depuis un TitanOs déjà installé : l'application Mises à jour liste tous les disques et clés (sauf celui en cours), et se met à jour quand on branche une clé.

## Nouveautés de la bêta 10

- **Wi-Fi : connexion WPA2 réparée.** Les modules de chiffrement du noyau demandés par le Wi-Fi (CCM, CTR, GCM, CMAC, TKIP, AES-NI) n'étaient pas inclus : la carte voyait les réseaux mais ne pouvait pas s'y connecter.
- Le voyant Wi-Fi du VAIO reste allumé quand la radio est active.

## Nouveautés de la bêta 9

- Mises à jour en HTTPS avec `curl` (certificats vérifiés) : le `wget` de BusyBox échouait avec GitHub.
- Wi-Fi : recherche des réseaux toutes les 15 s tant qu'on n'est pas connecté ; mot de passe refusé signalé dans Réseau ; états du Wi-Fi notés dans le journal.
- Le journal sur la clé ou le disque est mis à jour pendant toute la session.

## Nouveautés de la bêta 8

- Wi-Fi Intel : pays réglé sur la France (puissance et canaux complets) et pilote réglé pour une meilleure réception.
- Mises à jour : en cas d'échec, le message dit où ça bloque (adresse, DNS ou connexion sécurisée).

## Nouveautés de la bêta 7

- Mot de passe Wi-Fi : bouton **Afficher** pour voir ce qu'on tape.
- « Oublier ce réseau » fonctionne vraiment (le menu perdait le réseau visé).
- Le journal note les connexions Wi-Fi demandées et les réseaux oubliés.

## Nouveautés de la bêta 6

- **Fluidité** : les ombres sont gardées en image et le verre n'est recalculé que là où c'est utile. Pendant
  qu'on déplace une fenêtre, son verre reste figé et se remet à jour dès qu'elle s'arrête : environ 5 fois moins
  de calcul par image. Le processeur passe en pleine vitesse sur secteur.
- **Son** : la sortie analogique est cherchée directement auprès des pilotes, et chaque échec est noté dans
  le journal.
- **Wi-Fi** : l'interrupteur WIRELESS du VAIO est détecté et expliqué.
- **Écran de démarrage** : le chargeur lit la résolution native de l'écran (EDID) et choisit un mode de même
  format quand le BIOS en propose un, au lieu du plus grand.
- **Mises à jour automatiques** depuis GitHub et **installation sur le disque interne**.
- Le chargeur transmet à Linux la signature du disque qui a démarré : avec une clé et un disque TitanOs,
  c'est le bon qui est utilisé.

## Nouveautés de la bêta 5

- **Correction de l'écran figé** : la liste des pilotes était lue deux fois, ce qui écrasait la mémoire et
  faisait planter leur chargement. Sans pilotes, ni la carte NVIDIA, ni la clé, ni le clavier et la souris
  n'étaient pris en charge par le bureau.
- La ligne de diagnostic n'apparaît plus que si la clé, le clavier ou le pointeur manquent.

## Nouveautés de la bêta 4

- Si le bureau plante plusieurs fois, le mode de secours affiche la cause à l'écran et copie un rapport
  complet sur la clé (`rapport-titanos.txt`).

## Nouveautés de la bêta 3

- Ligne de diagnostic sur l'écran de verrouillage, journal plus complet et écrit plus tôt.

## Nouveautés de la bêta 2

- Correction : l'écran de verrouillage pouvait rester figé (souris et clavier sans effet) quand le Wi-Fi
  ou la batterie tardaient à répondre. Ils sont maintenant interrogés par un processus séparé.
- Nouvelle animation de démarrage, propre à TitanOs : les crêtes, à la place du cercle de points.
- Journal du démarrage enregistré sur la clé.

## État de la bêta

- **Vérifié** :
  - le chargeur BIOS démarre Linux dans l'émulateur et dessine bien l'écran de démarrage ;
  - le bureau, les animations et les applications sont rendus et pilotés dans l'aperçu Windows ;
  - le programme Linux et le chargeur UEFI se compilent sans erreur.
- **Essayé sur le VAIO (bêtas 1 à 5)** : depuis la bêta 5, le bureau, la clé, le clavier, le pavé tactile et
  la carte NVIDIA (1920×1080) fonctionnent.
- **À tester sur le VAIO** :
  - la fluidité, le son, le Wi-Fi (interrupteur sur ON) ;
  - l'installation sur le disque et la première mise à jour automatique ;
  - le Wi-Fi, le son, la batterie et les touches Fn ;
  - la création de l'espace des applications sur la clé ;
  - le chargeur UEFI.
- **Limites** :
  - pas d'applications graphiques Linux (Firefox, VLC…) pour l'instant : seulement celles du Terminal ;
  - pas d'accélération 3D ;
  - sur un PC sans carte NVIDIA, l'écran reste celui du BIOS (pas de réglage de la résolution) ;
  - le Bloc-notes n'avertit pas encore d'un texte non enregistré.

Licences des composants embarqués : noyau Linux, BusyBox et wpa_supplicant (GPL / BSD), police Inter
(SIL OFL 1.1), micrologiciel Intel (licence de redistribution d'Intel, linux-firmware).
