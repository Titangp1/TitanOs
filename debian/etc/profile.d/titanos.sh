# TitanOs : réglages du Terminal (Debian), lus par bash à l'ouverture d'une session
[ -n "$BASH_VERSION" ] || return 0 2>/dev/null
case $- in *i*) ;; *) return 0 ;; esac
export EDITOR=nano PAGER=less
# invite : titan en vert ($), root en rouge (#)
if [ "$(id -u)" = 0 ]; then
    PS1='\[\e[1;31m\]root\[\e[0m\]@\[\e[36m\]\h\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]# '
else
    PS1='\[\e[1;32m\]\u\[\e[0m\]@\[\e[36m\]\h\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]\$ '
fi

alias ls='ls --color=auto'
alias ll='ls -l'
alias la='ls -la'
alias effacer='clear'
alias ouvrir='cd'
alias copier='cp'
alias deplacer='mv'
alias supprimer='rm'
alias memoire='free -m'
alias disques='df -h'
alias reseau='ip addr'
alias heure='date'
alias eteindre='sudo systemctl poweroff'
alias redemarrer='sudo systemctl reboot'

# apt : les commandes qui modifient le système passent toutes seules par sudo
apt() {
    if [ "$(id -u)" != 0 ]; then
        case "$1" in install|remove|purge|update|upgrade|full-upgrade|autoremove|clean) sudo /usr/bin/apt "$@"; return $? ;; esac
    fi
    command apt "$@"
}

# applications de leurs éditeurs (paquets .deb officiels), installées avec apt
_titanos_deb() {   # nom adresse
    local f=/tmp/$1.deb
    echo "Téléchargement de $1…"
    curl -fL --retry 3 -o "$f" "$2" && sudo apt install -y "$f"
    local r=$?
    rm -f "$f"
    return $r
}
installer-chrome()  { _titanos_deb google-chrome https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb; }
installer-discord() { _titanos_deb discord 'https://discord.com/api/download?platform=linux&format=deb'; }
installer-vscode()  { _titanos_deb vscode 'https://update.code.visualstudio.com/latest/linux-deb-x64/stable'; }

aide() {
    cat <<'FIN'
Vous êtes « titan », sans droits d'administrateur. Pour une commande d'administration :
  sudo commande             demande le mot de passe de session (Paramètres -> Compte)
  sudo -i                   shell root ; « exit » pour revenir
Fichiers et dossiers : ls, cd, pwd, cat, cp, mv, rm, mkdir, tree, find, nano fichier.txt
Système : htop, free -m, df -h, lsblk, lscpu, lspci, lsusb, findmnt, uptime, journalctl -b
Réseau : ip addr, nmcli, ping 1.1.1.1, iw dev, curl URL
Applications : apt install nom (sudo est demandé tout seul), apt search mot, apt remove nom
  installer-chrome, installer-discord, installer-vscode : paquets officiels de Google, Discord, Microsoft
  Les applications graphiques connues (LibreOffice, GIMP, VS Code, Discord...) rejoignent le menu Démarrer
Alias en français : effacer, memoire, disques, reseau, heure, eteindre, redemarrer
Vos fichiers : ~/Documents, ~/Images, ~/Musique, ~/Téléchargements, ~/Vidéos (sur la clé ou le disque)
FIN
}

if [ -t 1 ] && [ -z "$TITANOS_BIENVENUE" ]; then
    export TITANOS_BIENVENUE=1
    printf '\033[1;35mTitanOs\033[0m - Debian %s - noyau Linux %s\n' "$(cat /etc/debian_version 2>/dev/null)" "$(uname -r)"
    printf 'Tapez \033[1maide\033[0m pour la liste des commandes ; \033[1msudo\033[0m pour l'\''administration.\n\n'
fi
