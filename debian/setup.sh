#!/bin/sh
# TitanOs - installation de la base Debian au premier démarrage (« seconde étape »)
#
# Lancé par TitanOs dans la nouvelle racine (chroot), une fois l'archive DEBIAN.TXZ extraite
# et les paquets (DEBS.TAR) posés dans /var/cache/apt/archives. Même déroulé que debootstrap :
# paquets essentiels un par un, puis tous les autres avec dpkg, puis les réglages de TitanOs.
# Chaque étape écrit « P <pourcentage> <message> » : TitanOs l'affiche sur l'écran de démarrage.
# Le détail va dans /titanos/install.log (copié sur la clé : installation-debian.txt).

export DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true LC_ALL=C.UTF-8 LANG=C.UTF-8 HOME=/root \
       PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
A=/var/cache/apt/archives
T=/titanos
LOG=$T/install.log
p() { echo "P $1 $2"; echo "== $2 ($1 %)" >> $LOG; }
deb() { for x in "$@"; do for f in $A/${x}_*.deb; do [ -e "$f" ] && { echo "$f"; break; }; done; done; }
core() { dpkg --force-unsafe-io --force-depends --force-confnew --install $(deb "$@") </dev/null >>$LOG 2>&1 || echo "échec : $*" >> $LOG; }
# dpkg avec progression : lit les lignes « processing: <action>: <paquet> » de --status-fd
run_dpkg() {   # de à total action message dpkg-args...
    from=$1; to=$2; total=$3; act=$4; msg=$5; shift 5
    i=0
    { dpkg --force-unsafe-io --status-fd 3 "$@" </dev/null >>$LOG 2>&1 3>&4 4>&-; } 4>&1 | while read -r a b c; do
        if [ "$a" = "processing:" ] && [ "$b" = "$act:" ]; then
            i=$((i + 1))
            [ $((i % 4)) -eq 0 ] && p $((from + (to - from) * i / (total + 1))) "$msg"
        fi
    done
}

echo "TitanOs - installation de Debian, $(date)" > $LOG

# ---- 1. paquets essentiels (comme debootstrap) ----
p 42 "Installation de Debian : paquets essentiels…"
v=$(dpkg-deb -f "$(deb dpkg)" Version)
printf 'Package: dpkg\nVersion: %s\nMaintainer: unknown\nStatus: install ok installed\n\n' "$v" >> /var/lib/dpkg/status
touch /var/lib/dpkg/info/dpkg.list
/sbin/ldconfig 2>/dev/null || /usr/sbin/ldconfig
ln -sf mawk /usr/bin/awk
core base-passwd
core base-files
core dpkg
[ -e /etc/localtime ] || ln -sf /usr/share/zoneinfo/Europe/Paris /etc/localtime
core libc6
core perl-base
rm -f /usr/bin/awk
core mawk
core debconf
p 46 "Installation de Debian : paquets essentiels…"

nreq=$(wc -l < $T/required.list)
run_dpkg 46 52 $nreq unpack "Installation de Debian : paquets essentiels…" --force-depends --unpack $(deb $(cat $T/required.list))

# aucun service ne démarre pendant l'installation
printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d
chmod 755 /usr/sbin/policy-rc.d
if [ -e /usr/sbin/start-stop-daemon ]; then
    mv /usr/sbin/start-stop-daemon /usr/sbin/start-stop-daemon.REAL
    printf '#!/bin/sh\nexit 0\n' > /usr/sbin/start-stop-daemon
    chmod 755 /usr/sbin/start-stop-daemon
fi
run_dpkg 52 58 $nreq configure "Configuration des paquets essentiels…" --configure --pending --force-configure-any --force-depends

# ---- 2. le reste de la base : réseau, son, applications ----
nbase=$(wc -l < $T/base.list)
run_dpkg 58 78 $nbase unpack "Installation de Debian : réseau, son, outils…" \
    --force-depends --force-overwrite --force-confold --skip-same-version --unpack $(deb $(cat $T/base.list))
for k in 1 2 3; do
    run_dpkg 78 92 $nbase configure "Configuration de Debian…" --force-confold --force-depends --configure -a
    dpkg --audit >>$LOG 2>&1 && break
done
if [ -e /usr/sbin/start-stop-daemon.REAL ]; then mv /usr/sbin/start-stop-daemon.REAL /usr/sbin/start-stop-daemon; fi
rm -f /usr/sbin/policy-rc.d

# ---- 3. réglages de TitanOs ----
p 93 "Réglages de TitanOs…"
cp -a $T/etc/. /etc/ >>$LOG 2>&1
# dpkg sans attente d'écriture à chaque fichier (clé USB : installations bien plus rapides)
echo force-unsafe-io > /etc/dpkg/dpkg.cfg.d/titanos
# apt : sans les paquets « recommandés » (système léger), réponses par défaut sans questions
cat > /etc/apt/apt.conf.d/90titanos <<'FIN'
APT::Install-Recommends "false";
APT::Install-Suggests "false";
Dpkg::Options { "--force-confdef"; "--force-confold"; };
FIN
# le Terminal garde l'invite et les raccourcis de TitanOs (même avec le .bashrc de Debian)
for f in /etc/skel/.bashrc /root/.bashrc; do
    [ -f $f ] && ! grep -q titanos.sh $f && echo '[ -f /etc/profile.d/titanos.sh ] && . /etc/profile.d/titanos.sh' >> $f
done
echo titanos > /etc/hostname
printf '127.0.0.1\tlocalhost\n127.0.1.1\ttitanos\n::1\t\tlocalhost ip6-localhost ip6-loopback\n' > /etc/hosts
# langue et heure
if [ -e /etc/locale.gen ]; then
    sed -i 's/^# *fr_FR.UTF-8 UTF-8/fr_FR.UTF-8 UTF-8/' /etc/locale.gen
    grep -q '^fr_FR.UTF-8' /etc/locale.gen || echo 'fr_FR.UTF-8 UTF-8' >> /etc/locale.gen
    locale-gen >>$LOG 2>&1
fi
printf 'LANG=fr_FR.UTF-8\n' > /etc/default/locale
ln -sf /usr/share/zoneinfo/Europe/Paris /etc/localtime
echo Europe/Paris > /etc/timezone
# compte de l'utilisateur : titan (uid 1000), sans droits d'administrateur
if ! id titan >/dev/null 2>&1; then
    useradd -m -u 1000 -U -s /bin/bash -c Titan titan >>$LOG 2>&1
fi
for g in audio video render input netdev plugdev users; do
    getent group $g >/dev/null && usermod -aG $g titan >>$LOG 2>&1
done
mkdir -p /var/lib/systemd/linger && touch /var/lib/systemd/linger/titan     # sa session (son, D-Bus) démarre avec le PC
# pilotes du noyau de TitanOs
depmod -a "$(cat $T/kernel)" >>$LOG 2>&1
# dépôts Debian (mises à jour et sécurité)
rm -f /etc/apt/sources.list
cat > /etc/apt/sources.list.d/debian.sources <<'FIN'
Types: deb
URIs: http://deb.debian.org/debian
Suites: trixie trixie-updates
Components: main contrib non-free non-free-firmware
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg

Types: deb
URIs: http://security.debian.org/debian-security
Suites: trixie-security
Components: main contrib non-free non-free-firmware
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
FIN
# identifiant de la machine (sans lui, systemd lancerait son assistant de premier démarrage)
rm -f /etc/machine-id /var/lib/dbus/machine-id
systemd-machine-id-setup >>$LOG 2>&1 || dbus-uuidgen > /etc/machine-id
# services : le bureau TitanOs sur l'écran, NetworkManager pour le réseau
systemctl enable titanos.service NetworkManager.service systemd-timesyncd.service >>$LOG 2>&1
systemctl mask getty@tty1.service systemd-firstboot.service >>$LOG 2>&1
# démarrage plus rapide, rien en tâche de fond sans raison (les mises à jour passent par TitanOs)
systemctl disable NetworkManager-wait-online.service apt-daily.timer apt-daily-upgrade.timer     dpkg-db-backup.timer e2scrub_all.timer >>$LOG 2>&1
systemctl set-default multi-user.target >>$LOG 2>&1
# place libérée : les paquets sont installés
rm -f $A/*.deb
p 99 "Base Debian prête."
touch /etc/titanos-ready
exit 0
