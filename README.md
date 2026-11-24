# TitanOs — bêta 3

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
  réseaux enregistrés. Aussi l'**Ethernet** (Marvell Yukon) et le **partage de connexion USB** d'un téléphone.
- **Applications Linux** : `apk add htop` dans le Terminal installe un programme d'Alpine. Au premier
  démarrage, TitanOs crée un espace pour les applications dans la place libre de la clé : elles y restent
  installées.
- Vos fichiers et réglages sont gardés sur la clé, dans `Utilisateur/` et `titanos.cfg`.

![Réseau et Wi-Fi](docs/captures/reseau.png)

![Système](docs/captures/systeme.png)

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
| **Snake**, **À propos** | |

Raccourcis : touche Windows (menu et recherche), Alt+Tab, Alt+F4, Ctrl+Alt+T (Terminal), Windows+L (verrouiller).

## Installer sur une clé USB

1. Télécharger `titanos-beta3.img.zip` dans les [versions publiées](../../releases), puis le décompresser.
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
  apps.c term.c files.c notes.c prefs.c sysapp.c netapp.c   applications
src/linux/            ce qui parle au noyau (appels système directs, sans libc)
  main.c                  /init, démarrage, clé, espace des applications
  modules.c display.c input.c   pilotes, écran DRM/KMS, clavier/souris/pavé tactile
  audio.c wifi.c plat.c         son ALSA, Wi-Fi (wpa_supplicant), fichiers, système, batterie
src/sim/sim.c         version Windows du bureau, pour l'aperçu
rootfs/etc/           profil du shell, script DHCP, dépôts apk
tools/                téléchargement Alpine, polices, initramfs, image disque, écran de démarrage, tests
```

## Si quelque chose ne va pas

- **En bas de l'écran de verrouillage**, une ligne de diagnostic indique le nombre de pilotes chargés,
  si la clé est montée, les claviers, souris et pavés tactiles trouvés, le nombre d'événements reçus
  (touches, mouvements) et le temps écoulé depuis le démarrage. Si ce temps s'arrête, le bureau est bloqué.
- **Sur la clé**, dès qu'elle est trouvée, TitanOs écrit `journal-titanos.txt` toutes les 5 secondes
  pendant 4 minutes : messages du noyau, pilotes, périphériques, et l'endroit où chaque processus attend.
  Branchez la clé sur Windows et envoyez ce fichier.

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
- **Essayé sur le VAIO (bêtas 1 et 2)** : le démarrage et son animation fonctionnent ; l'écran de
  verrouillage reste figé (souris et clavier sans effet). La bêta 3 sert à trouver pourquoi.
- **À tester sur le VAIO** :
  - l'écran de verrouillage et le bureau ;
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
