/* TitanOs - chargeur BIOS de 2e étape (chargé à 0x8000 par boot/mbr.S)
 *
 * Démarre le noyau Linux selon son protocole de démarrage 32 bits :
 *   1. active la ligne A20 et choisit le meilleur mode graphique VBE ;
 *   2. dessine l'écran de démarrage de TitanOs (dégradé, logo, nom) à partir de
 *      TITANOS/BOOT.IMG, puis anime les « crêtes » pendant le chargement ;
 *   3. lit le noyau (partie « mode protégé » du bzImage) et l'initramfs par blocs de
 *      127 secteurs (INT 13h étendu) et les recopie au-delà de 1 Mo en mode « unreal » ;
 *   4. complète la « zero page » (boot_params) : carte mémoire E820 et écran VBE ;
 *      le reste a été préparé par tools/mkimage.py et inscrit directement dans ce fichier ;
 *   5. passe en mode protégé 32 bits et saute dans le noyau (ESI = boot_params).
 * Linux garde l'image affichée : le passage à l'écran de TitanOs est invisible.
 * Sans mode graphique, tout se fait en mode texte, avec des points de progression.
 *
 * Plan mémoire : 0x7C00 MBR · 0x8000 ce chargeur (avec la zero page) · pile sous 0xFFF0
 *                0x10000 tampon de lecture · 0x20000 BOOT.IMG · 0x100000 noyau · initrd plus haut
 */

#define BOUNCE_SEG  0x1000
#define BOUNCE      0x10000
#define BIMG        0x20000
#define CHUNK       127
#define VBE_INFO    0x7000
#define MODE_INFO   0x7200
#define EDID_BUF    0x6E00
#define E820_TMP    0x6F00
#define ZP_E820N    0x1E8
#define ZP_E820     0x2D0
#define OFFSCR      0x3000000       /* image de travail 32 bits, si l'écran est en 16 ou 24 bits */

    .code16
    .text
    .globl s2
s2:
    jmp     real_start
    .balign 4
    .ascii  "TITN"              /* complété par tools/mkimage.py */
kern_lba:       .long 0         /* partie mode protégé du bzImage */
kern_sects:     .long 0
initrd_lba:     .long 0
initrd_sects:   .long 0
initrd_addr:    .long 0
bimg_lba:       .long 0         /* images de l'écran de démarrage */
bimg_sects:     .long 0

real_start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0xFFF0, %sp
    sti
    cld
    movb    %dl, drive
    call    add_disk_arg

    movw    $m_hello, %si
    call    print
    call    enable_a20
    call    setup_video                 /* mode graphique, s'il y en a un */
    cmpb    $0, gfx_mode
    je      1f
    movl    bimg_lba, %eax              /* images de l'écran de démarrage */
    movl    %eax, cur_lba
    movl    bimg_sects, %eax
    movl    %eax, left
    movl    $BIMG, dest
    call    load
    call    unreal
    movl    $BIMG, %esi
    cmpl    $0x4D494254, (%esi)         /* « TBIM » */
    jne     1f
    call    pick_variant
    call    draw_splash
    movb    $1, gfx
1:
    /* noyau à 1 Mo */
    movw    $m_kernel, %si
    call    tprint
    movl    kern_lba, %eax
    movl    %eax, cur_lba
    movl    kern_sects, %eax
    movl    %eax, left
    movl    $0x100000, dest
    call    load
    movw    $m_ok, %si
    call    tprint

    /* initramfs */
    movw    $m_initrd, %si
    call    tprint
    movl    initrd_lba, %eax
    movl    %eax, cur_lba
    movl    initrd_sects, %eax
    movl    %eax, left
    movl    initrd_addr, %eax
    movl    %eax, dest
    call    load
    movw    $m_ok, %si
    call    tprint

    call    detect_memory

    /* ---------- mode protégé 32 bits, puis le noyau ---------- */
    cli
    lgdt    gdtr
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmp    $0x10, $pm32

    .code32
pm32:
    movw    $0x18, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    movl    $zeropage, %esi
    xorl    %ebp, %ebp
    xorl    %edi, %edi
    xorl    %ebx, %ebx
    movl    zeropage + 0x214, %eax      /* code32_start (0x100000) */
    jmp     *%eax

/* ====================================================================== */
    .code16

print:                                  /* affiche la chaîne DS:SI (mode texte) */
    pushw   %ax
    pushw   %bx
    pushw   %si
1:  lodsb
    testb   %al, %al
    jz      2f
    movb    $0x0E, %ah
    movw    $0x0007, %bx
    int     $0x10
    jmp     1b
2:  popw    %si
    popw    %bx
    popw    %ax
    ret

tprint:                                 /* n'affiche qu'en mode texte */
    cmpb    $0, gfx_mode
    jne     1f
    call    print
1:  ret

fatal:                                  /* repasse en mode texte, affiche SI et s'arrête */
    pushw   %si
    movw    $0x0003, %ax
    int     $0x10
    popw    %si
    call    print
1:  cli
    hlt
    jmp     1b

enable_a20:
    movw    $0x2401, %ax                /* méthode BIOS */
    int     $0x15
    inb     $0x92, %al                  /* méthode « fast A20 » */
    testb   $2, %al
    jnz     1f
    orb     $2, %al
    andb    $0xFE, %al
    outb    %al, $0x92
1:  ret

/* mode « unreal » : DS et ES gardent une limite de 4 Go en revenant en mode réel */
unreal:
    cli
    pushw   %ds
    pushw   %es
    lgdt    gdtr
    movl    %cr0, %eax
    orb     $1, %al
    movl    %eax, %cr0
    jmp     1f
1:  movw    $0x18, %bx
    movw    %bx, %ds
    movw    %bx, %es
    andb    $0xFE, %al
    movl    %eax, %cr0
    jmp     2f
2:  popw    %es
    popw    %ds
    sti
    ret

/* lit « left » secteurs depuis « cur_lba » et les recopie à l'adresse 32 bits « dest » */
load:
    movw    $0, dots
ld_loop:
    movl    left, %eax
    testl   %eax, %eax
    jz      ld_done
    cmpl    $CHUNK, %eax
    jbe     1f
    movl    $CHUNK, %eax
1:  movw    %ax, dap_count
    movw    %ax, chunk
    movw    $3, tries
ld_retry:
    movw    $0, dap_off
    movw    $BOUNCE_SEG, dap_seg
    movl    cur_lba, %eax
    movl    %eax, dap_lba
    movl    $0, dap_lba + 4
    movw    $dap, %si
    movb    $0x42, %ah
    movb    drive, %dl
    int     $0x13
    jnc     ld_copy
    decw    tries
    jz      disk_error
    xorb    %ah, %ah                    /* réinitialisation du disque, puis on réessaie */
    movb    drive, %dl
    int     $0x13
    jmp     ld_retry
ld_copy:
    call    unreal
    cli
    movl    $BOUNCE, %esi
    movl    dest, %edi
    movzwl  chunk, %ecx
    shll    $7, %ecx                    /* secteurs -> mots de 4 octets */
    cld
    .byte   0x67                        /* adresses 32 bits (ESI, EDI) */
    rep movsl
    sti
    movl    %edi, dest
    movzwl  chunk, %eax
    addl    %eax, cur_lba
    subl    %eax, left
    cmpb    $0, gfx
    je      2f
    call    spin_update                 /* les crêtes s'animent pendant la lecture */
    jmp     ld_loop
2:  incw    dots
    testw   $7, dots                    /* en mode texte : un point tous les 500 Ko environ */
    jnz     ld_loop
    movw    $m_dot, %si
    call    tprint
    jmp     ld_loop
ld_done:
    ret
disk_error:
    movw    $m_disk, %si
    jmp     fatal

/* carte mémoire : INT 15h E820, recopiée dans la zero page (entrées de 20 octets) */
detect_memory:
    xorl    %ebx, %ebx
    movw    $zeropage + ZP_E820, %di
e820_loop:
    pushw   %di
    movw    $E820_TMP, %di
    movl    $1, 20(%di)                 /* attribut ACPI 3 « valide » par défaut */
    movl    $0xE820, %eax
    movl    $24, %ecx
    movl    $0x534D4150, %edx           /* « SMAP » */
    int     $0x15
    popw    %di
    jc      e820_done
    cmpl    $0x534D4150, %eax
    jne     e820_done
    cmpl    $20, %ecx
    jb      e820_next
    ja      1f
    jmp     2f
1:  testl   $1, E820_TMP + 20           /* entrée ACPI 3 marquée « à ignorer » */
    jz      e820_next
2:  pushw   %si
    movw    $E820_TMP, %si
    movw    $10, %cx
    rep movsw                           /* copie 20 octets vers DI */
    popw    %si
    incb    zeropage + ZP_E820N
    cmpb    $128, zeropage + ZP_E820N
    jae     e820_done
e820_next:
    testl   %ebx, %ebx
    jnz     e820_loop
e820_done:
    ret

/* écran : le plus grand mode VBE linéaire (jusqu'à 1920x1200), décrit dans screen_info.
 * La résolution de l'écran passe avant tout : beaucoup de cartes (NVIDIA des VAIO...) ne
 * proposent la résolution native qu'en 16 ou 24 bits ; à taille égale, 32 bits est préféré. */
setup_video:
    movw    $VBE_INFO, %di
    movl    $0x32454256, (%di)          /* « VBE2 » */
    movw    $0x4F00, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     vbe_fail
    /* résolution native de l'écran (EDID) : on préférera un mode de même format */
    pushw   %es
    movw    $0x4F15, %ax
    movw    $0x0001, %bx
    xorw    %cx, %cx
    xorw    %dx, %dx
    movw    $EDID_BUF, %di
    int     $0x10
    popw    %es
    cmpw    $0x004F, %ax
    jne     1f
    movzbl  EDID_BUF + 56, %eax
    movzbl  EDID_BUF + 58, %ecx
    andl    $0xF0, %ecx
    shll    $4, %ecx
    orl     %ecx, %eax
    movl    %eax, native_w
    movzbl  EDID_BUF + 59, %eax
    movzbl  EDID_BUF + 61, %ecx
    andl    $0xF0, %ecx
    shll    $4, %ecx
    orl     %ecx, %eax
    movl    %eax, native_h
    cmpl    $320, native_w              /* valeurs absurdes : on les ignore */
    jb      0f
    cmpl    $200, native_h
    jae     1f
0:  movl    $0, native_w
1:
    movw    VBE_INFO + 14, %ax
    movw    %ax, mode_off
    movw    VBE_INFO + 16, %ax
    movw    %ax, mode_seg

vm_next:
    movw    mode_seg, %ax
    movw    %ax, %fs
    movw    mode_off, %si
    movw    %fs:(%si), %cx
    cmpw    $0xFFFF, %cx
    je      vm_done
    addw    $2, mode_off
    movw    %cx, cur_mode
    movw    $0x4F01, %ax
    movw    $MODE_INFO, %di
    int     $0x10
    cmpw    $0x004F, %ax
    jne     vm_next
    movw    MODE_INFO, %ax              /* supporté, couleur, graphique, linéaire */
    andw    $0x0099, %ax
    cmpw    $0x0099, %ax
    jne     vm_next
    cmpw    $1024, MODE_INFO + 18       /* liste des modes, notée pour le journal */
    jb      5f
    call    vbe_note
5:  movb    MODE_INFO + 25, %al
    cmpb    $32, %al
    je      6f
    cmpb    $6, MODE_INFO + 27          /* 16 ou 24 bits : couleurs directes seulement */
    jne     vm_next
    cmpb    $24, %al
    je      6f
    cmpb    $16, %al
    jne     vm_next
6:
    movzwl  MODE_INFO + 18, %eax
    cmpl    $1920, %eax
    ja      vm_next
    cmpl    $640, %eax
    jb      vm_next
    movzwl  MODE_INFO + 20, %ebx
    cmpl    $1200, %ebx
    ja      vm_next
    cmpl    $480, %ebx
    jb      vm_next
    /* note : la surface, + 2^30 si c'est la résolution native, + 2^29 si même format */
    movl    %eax, %esi
    movl    %ebx, %edi
    imull   %ebx, %eax
    cmpl    $0, native_w
    je      2f
    cmpl    native_w, %esi
    jne     3f
    cmpl    native_h, %edi
    jne     3f
    addl    $0x40000000, %eax
    jmp     2f
3:  pushl   %eax
    movl    %esi, %eax
    imull   native_h, %eax              /* largeur x hauteur native */
    movl    %eax, %ecx
    movl    %edi, %edx
    imull   native_w, %edx              /* hauteur x largeur native */
    subl    %edx, %eax
    jns     4f
    negl    %eax
4:  shll    $5, %eax                    /* écart de format inférieur à 1/32 */
    cmpl    %ecx, %eax
    popl    %eax
    ja      2f
    addl    $0x20000000, %eax
2:  movzbl  MODE_INFO + 25, %edx        /* à taille égale : le plus de couleurs */
    shrl    $3, %edx
    addl    %edx, %eax
    cmpl    best_area, %eax
    jbe     vm_next
    movl    %eax, best_area
    movw    cur_mode, %ax
    movw    %ax, best_mode
    /* screen_info (début de la zero page) */
    movb    $0x23, zeropage + 0x0F      /* VIDEO_TYPE_VLFB */
    movw    MODE_INFO + 18, %ax
    movw    %ax, zeropage + 0x12        /* largeur */
    movw    MODE_INFO + 20, %ax
    movw    %ax, zeropage + 0x14        /* hauteur */
    movzbl  MODE_INFO + 25, %eax
    movw    %ax, zeropage + 0x16        /* bits par pixel */
    movl    MODE_INFO + 40, %eax
    movl    %eax, zeropage + 0x18       /* adresse du framebuffer */
    movzwl  VBE_INFO + 18, %eax
    movl    %eax, zeropage + 0x1C       /* mémoire vidéo, en blocs de 64 Ko */
    movw    MODE_INFO + 16, %ax
    cmpw    $0x0300, VBE_INFO + 4
    jb      1f
    cmpw    $0, MODE_INFO + 50
    je      1f
    movw    MODE_INFO + 50, %ax         /* VBE 3 : octets par ligne en mode linéaire */
1:  movw    %ax, zeropage + 0x24
    movl    MODE_INFO + 31, %eax        /* tailles et positions rouge, vert, bleu */
    movl    %eax, zeropage + 0x26
    movl    MODE_INFO + 35, %eax
    movl    %eax, zeropage + 0x2A
    movw    MODE_INFO, %ax
    movw    %ax, zeropage + 0x34        /* attributs du mode */
    jmp     vm_next

vm_done:
    call    add_vbe_arg
    cmpw    $0, best_mode
    je      vbe_fail
    movw    $0x4F02, %ax
    movw    best_mode, %bx
    orw     $0x4000, %bx                /* framebuffer linéaire */
    int     $0x10
    cmpw    $0x004F, %ax
    jne     vbe_fail
    movb    $1, gfx_mode
    movl    zeropage + 0x18, %eax
    movl    %eax, fb_base
    movzwl  zeropage + 0x24, %eax
    movl    %eax, fb_pitch
    movzwl  zeropage + 0x12, %eax
    movl    %eax, scr_w
    movzwl  zeropage + 0x14, %eax
    movl    %eax, scr_h
    /* dessin direct en 32 bits, couleurs dans l'ordre habituel (rouge 16, vert 8, bleu 0) ;
     * sinon, on dessine dans une image de travail 32 bits, convertie ensuite (flush) */
    cmpw    $32, zeropage + 0x16
    jne     2f
    cmpb    $16, zeropage + 0x27
    jne     2f
    cmpb    $8, zeropage + 0x29
    jne     2f
    cmpb    $0, zeropage + 0x2B
    jne     2f
    ret
2:  movzwl  zeropage + 0x16, %eax       /* octets par pixel */
    addl    $7, %eax
    shrl    $3, %eax
    cmpl    $2, %eax
    jb      3f
    cmpl    $4, %eax
    ja      3f
    movl    %eax, real_bpp
    movl    fb_base, %eax
    movl    %eax, real_base
    movl    fb_pitch, %eax
    movl    %eax, real_pitch
    movl    $OFFSCR, fb_base
    movl    scr_w, %eax
    shll    $2, %eax
    movl    %eax, fb_pitch
    movb    $1, use_off
    ret
3:  movb    $0, gfx_mode                /* mode réglé pour Linux, mais pas de dessin */
    ret
vbe_fail:
    /* pas de mode graphique : on reste en texte, le pilote de la carte prendra le relais */
    movw    $m_novbe, %si
    call    print
    movb    $0, zeropage + 0x00
    movb    $3, zeropage + 0x06         /* mode 3 : texte 80x25 */
    movb    $80, zeropage + 0x07
    movb    $25, zeropage + 0x0E
    movb    $0x22, zeropage + 0x0F      /* VIDEO_TYPE_VGAC */
    movw    $16, zeropage + 0x10
    ret

/* ====================================================================== */
/* Dessin de l'écran de démarrage (en mode unreal : adresses 32 bits)     */
/* ====================================================================== */

/* couleur du dégradé à la ligne ECX -> EAX (0x00RRGGBB) */
grad:
    pushl   %ebx
    pushl   %ecx
    pushl   %edx
    pushl   %esi
    pushl   %edi
    movl    %ecx, %eax
    shll    $8, %eax
    xorl    %edx, %edx
    movl    scr_h, %ebx
    decl    %ebx
    jz      1f
    divl    %ebx                        /* t = y * 256 / (hauteur - 1) */
    jmp     2f
1:  xorl    %eax, %eax
2:  movl    %eax, %edi
    movl    bimg_base, %esi
    xorl    %ebx, %ebx                  /* résultat */
    movb    $16, %cl                    /* canaux : décalage 16, 8, 0 */
3:  movl    32(%esi), %eax              /* couleur du haut */
    shrl    %cl, %eax
    andl    $0xFF, %eax
    movl    36(%esi), %edx              /* couleur du bas */
    shrl    %cl, %edx
    andl    $0xFF, %edx
    subl    %eax, %edx
    imull   %edi, %edx
    sarl    $8, %edx
    addl    %edx, %eax
    shll    %cl, %eax
    orl     %eax, %ebx
    subb    $8, %cl
    jns     3b
    movl    %ebx, %eax
    popl    %edi
    popl    %esi
    popl    %edx
    popl    %ecx
    popl    %ebx
    ret

/* adresse dans le framebuffer du pixel (rx, ECX) -> EDI */
fb_addr:
    movl    %ecx, %edi
    imull   fb_pitch, %edi
    addl    fb_base, %edi
    movl    rx, %eax
    shll    $2, %eax
    addl    %eax, %edi
    ret

/* remplit (rx, ry, rw, rh) avec le dégradé */
fill_bg:
    movl    ry, %ecx
    movl    rh, %ebx
1:  testl   %ebx, %ebx
    jz      2f
    call    fb_addr
    call    grad
    pushl   %ecx
    movl    rw, %ecx
    cld
    .byte   0x67
    rep stosl
    popl    %ecx
    incl    %ecx
    decl    %ebx
    jmp     1b
2:  ret

/* pose une image BGRA prémultipliée (ESI) en (rx, ry), taille rw x rh */
blit_pm:
    movl    ry, %ecx
    movl    rh, %ebx
1:  testl   %ebx, %ebx
    jz      9f
    call    grad
    movl    %eax, bgcol
    call    fb_addr
    pushl   %ecx
    pushl   %ebx
    movl    rw, %ecx
2:  movl    (%esi), %eax
    addl    $4, %esi
    movl    %eax, %edx
    shrl    $24, %edx                   /* opacité de la source */
    jz      3f                          /* transparent : le fond déjà dessiné reste */
    movl    $255, %ebx
    subl    %edx, %ebx                  /* 255 - opacité */
    pushl   %ecx
    movl    bgcol, %ecx
    andl    $0xFF00FF, %ecx
    imull   %ebx, %ecx
    shrl    $8, %ecx
    andl    $0xFF00FF, %ecx
    movl    bgcol, %edx
    andl    $0x00FF00, %edx
    imull   %ebx, %edx
    shrl    $8, %edx
    andl    $0x00FF00, %edx
    orl     %edx, %ecx
    addl    %ecx, %eax                  /* fond atténué + source prémultipliée */
    andl    $0xFFFFFF, %eax
    popl    %ecx
    movl    %eax, (%edi)
3:  addl    $4, %edi
    decl    %ecx
    jnz     2b
    popl    %ebx
    popl    %ecx
    incl    %ecx
    decl    %ebx
    jmp     1b
9:  ret

/* pose une couverture 8 bits (ESI) de couleur fgcol en (rx, ry), taille rw x rh */
blit_a:
    movl    ry, %ecx
    movl    rh, %ebx
1:  testl   %ebx, %ebx
    jz      9f
    call    grad
    movl    %eax, bgcol
    call    fb_addr
    pushl   %ecx
    pushl   %ebx
    movl    rw, %ecx
2:  movzbl  (%esi), %edx                /* couverture */
    incl    %esi
    testl   %edx, %edx
    jz      4f
    movl    $256, %ebx
    subl    %edx, %ebx                  /* 256 - a */
    pushl   %ecx
    movl    fgcol, %eax
    andl    $0xFF00FF, %eax
    imull   %edx, %eax
    movl    bgcol, %ecx
    andl    $0xFF00FF, %ecx
    imull   %ebx, %ecx
    addl    %ecx, %eax
    shrl    $8, %eax
    andl    $0xFF00FF, %eax
    pushl   %eax
    movl    fgcol, %eax
    andl    $0x00FF00, %eax
    imull   %edx, %eax
    movl    bgcol, %ecx
    andl    $0x00FF00, %ecx
    imull   %ebx, %ecx
    addl    %ecx, %eax
    shrl    $8, %eax
    andl    $0x00FF00, %eax
    popl    %ecx
    orl     %ecx, %eax
    popl    %ecx
    movl    %eax, (%edi)
4:  addl    $4, %edi                    /* couverture nulle : on ne touche à rien */
    decl    %ecx
    jnz     2b
    popl    %ebx
    popl    %ecx
    incl    %ecx
    decl    %ebx
    jmp     1b
9:  ret

draw_splash:
    call    unreal
    movl    $0, rx                      /* fond */
    movl    $0, ry
    movl    scr_w, %eax
    movl    %eax, rw
    movl    scr_h, %eax
    movl    %eax, rh
    call    fill_bg
    movl    bimg_base, %esi                 /* logo */
    movzwl  4(%esi), %eax
    movl    %eax, rw
    movzwl  6(%esi), %eax
    movl    %eax, rh
    movl    scr_w, %eax
    shrl    $1, %eax
    movl    rw, %edx
    shrl    $1, %edx
    subl    %edx, %eax
    movl    %eax, rx
    movl    scr_h, %eax
    shrl    $1, %eax
    movswl  20(%esi), %edx
    addl    %edx, %eax
    movl    %eax, ry
    movl    40(%esi), %eax
    addl    bimg_base, %eax
    movl    %eax, %esi
    call    blit_pm
    movl    bimg_base, %esi                 /* nom « TitanOs » */
    movzwl  8(%esi), %eax
    movl    %eax, rw
    movzwl  10(%esi), %eax
    movl    %eax, rh
    movl    scr_w, %eax
    movzwl  26(%esi), %edx
    subl    %edx, %eax
    shrl    $1, %eax
    movswl  24(%esi), %edx
    addl    %edx, %eax
    movl    %eax, rx
    movl    scr_h, %eax
    shrl    $1, %eax
    movswl  22(%esi), %edx
    addl    %edx, %eax
    movl    %eax, ry
    movl    $0xF8FAFC, fgcol
    movl    44(%esi), %eax
    addl    bimg_base, %eax
    movl    %eax, %esi
    call    blit_a
    movl    $0, rx                      /* image de travail : tout l'écran recopié */
    movl    $0, ry
    movl    scr_w, %eax
    movl    %eax, rw
    movl    scr_h, %eax
    movl    %eax, rh
    call    flush
    movw    $0xFFFF, cur_frame
    movb    $1, gfx
    call    spin_update
    ret

/* une image des « crêtes », choisie d'après le compteur du BIOS (18,2 par seconde) */
spin_update:
    call    unreal                      /* (modifie EAX et BX) */
    movl    0x46C, %eax                 /* tops d'horloge du BIOS */
    movl    bimg_base, %esi
    movzwl  16(%esi), %ebx              /* nombre d'images */
    xorl    %edx, %edx
    divl    %ebx
    cmpw    %dx, cur_frame
    je      9f
    movw    %dx, cur_frame
    pushl   %edx
    /* zone de l'animation : centrée horizontalement, haut = centre + anim_top */
    movzwl  12(%esi), %eax
    movl    %eax, rw
    movzwl  14(%esi), %eax
    movl    %eax, rh
    movl    scr_w, %eax
    shrl    $1, %eax
    movl    rw, %edx
    shrl    $1, %edx
    subl    %edx, %eax
    movl    %eax, rx
    movl    scr_h, %eax
    shrl    $1, %eax
    movswl  28(%esi), %edx
    addl    %edx, %eax
    movl    %eax, ry
    call    fill_bg                     /* efface l'image précédente */
    /* image courante : off_anim + numéro x largeur x hauteur */
    popl    %eax
    movl    bimg_base, %esi
    movzwl  12(%esi), %ecx
    imull   %ecx, %eax
    movzwl  14(%esi), %ecx
    imull   %ecx, %eax
    addl    48(%esi), %eax
    movl    52(%esi), %ecx              /* couleur */
    movl    %ecx, fgcol
    addl    bimg_base, %eax
    movl    %eax, %esi
    call    blit_a
    call    flush
9:  ret

/* image de travail -> écran 15, 16 ou 24 bits (ou 32 bits dans un autre ordre), pour la
 * zone (rx, ry, rw, rh). Tramage 2 x 2 : les dégradés restent lisses en 16 bits. */
.macro CHAN sh, szo, poso
    movl    %esi, %ebx
    shrl    $\sh, %ebx
    andl    $0xFF, %ebx
    movzbl  zeropage + \szo, %ecx      /* bits de la couleur */
    pushl   %eax
    movl    $256, %eax
    shrl    %cl, %eax
    imull   %edi, %eax                  /* tramage : 0 à 3 quarts d'un pas */
    shrl    $2, %eax
    addl    %eax, %ebx
    cmpl    $255, %ebx
    jbe     1f
    movl    $255, %ebx
1:  popl    %eax
    negl    %ecx
    addl    $8, %ecx
    shrl    %cl, %ebx
    movzbl  zeropage + \poso, %ecx
    shll    %cl, %ebx
    orl     %ebx, %eax
.endm

/* EAX = 0x00RRGGBB, EDX = x, ECX = y -> EAX : pixel au format de l'écran */
conv_px:
    pushl   %ebx
    pushl   %ecx
    pushl   %esi
    pushl   %edi
    movl    %eax, %esi
    movl    %edx, %edi
    andl    $1, %edi
    andl    $1, %ecx
    shll    $1, %ecx
    orl     %ecx, %edi
    movzbl  bayer(%edi), %edi
    xorl    %eax, %eax
    CHAN    16, 0x26, 0x27
    CHAN    8, 0x28, 0x29
    CHAN    0, 0x2A, 0x2B
    popl    %edi
    popl    %esi
    popl    %ecx
    popl    %ebx
    ret

flush:
    cmpb    $0, use_off
    je      9f
    pushal
    movl    ry, %ecx
    movl    rh, %ebx
1:  testl   %ebx, %ebx
    jz      8f
    movl    %ecx, %esi                  /* source : image de travail */
    imull   fb_pitch, %esi
    addl    $OFFSCR, %esi
    movl    rx, %eax
    shll    $2, %eax
    addl    %eax, %esi
    movl    %ecx, %edi                  /* destination : l'écran */
    imull   real_pitch, %edi
    addl    real_base, %edi
    movl    rx, %eax
    imull   real_bpp, %eax
    addl    %eax, %edi
    pushl   %ebx
    movl    rx, %edx
    movl    rw, %ebp
    testl   %ebp, %ebp
    jz      7f
2:  movl    (%esi), %eax
    addl    $4, %esi
    call    conv_px
    cmpl    $2, real_bpp
    jne     3f
    movw    %ax, (%edi)
    addl    $2, %edi
    jmp     5f
3:  cmpl    $3, real_bpp
    jne     4f
    movw    %ax, (%edi)
    shrl    $16, %eax
    movb    %al, 2(%edi)
    addl    $3, %edi
    jmp     5f
4:  movl    %eax, (%edi)
    addl    $4, %edi
5:  incl    %edx
    decl    %ebp
    jnz     2b
7:  popl    %ebx
    incl    %ecx
    decl    %ebx
    jmp     1b
8:  popal
9:  ret

/* liste des modes VBE (WxHxB,) : ajoutée à la ligne de commande, Linux la garde dans le journal */
vbe_note:
    pushal
    movw    vbe_len, %di
    cmpw    $200, %di
    ja      9f
    addw    $vbe_list, %di
    movzwl  MODE_INFO + 18, %eax
    call    put_dec
    movb    $'x', (%di)
    incw    %di
    movzwl  MODE_INFO + 20, %eax
    call    put_dec
    movb    $'x', (%di)
    incw    %di
    movzbl  MODE_INFO + 25, %eax
    call    put_dec
    movb    $',', (%di)
    incw    %di
    subw    $vbe_list, %di
    movw    %di, vbe_len
9:  popal
    ret

/* EAX -> chiffres décimaux en DS:DI (DI avance) */
put_dec:
    pushl   %ebx
    pushl   %ecx
    pushl   %edx
    movl    $10, %ebx
    xorl    %ecx, %ecx
1:  xorl    %edx, %edx
    divl    %ebx
    pushw   %dx
    incw    %cx
    testl   %eax, %eax
    jnz     1b
2:  popw    %dx
    addb    $'0', %dl
    movb    %dl, (%di)
    incw    %di
    decw    %cx
    jnz     2b
    popl    %edx
    popl    %ecx
    popl    %ebx
    ret

/* « titanos.vbe=... » à la fin de la ligne de commande */
add_vbe_arg:
    cmpw    $0, vbe_len
    je      9f
    movw    $cmdline, %di
1:  cmpb    $0, (%di)
    je      2f
    incw    %di
    jmp     1b
2:  movw    $m_vbearg, %si
3:  lodsb
    testb   %al, %al
    jz      4f
    movb    %al, (%di)
    incw    %di
    jmp     3b
4:  movw    $vbe_list, %si
    movw    vbe_len, %cx
    decw    %cx                         /* sans la dernière virgule */
5:  testw   %cx, %cx
    jz      6f
    lodsb
    movb    %al, (%di)
    incw    %di
    decw    %cx
    jmp     5b
6:  movb    $0, (%di)
9:  ret

/* ajoute « titanos.disk=xxxxxxxx » à la ligne de commande : la signature du disque qui a
 * démarré (MBR, octet 440). Linux reconnaît ainsi la bonne copie de TitanOs quand il y en
 * a plusieurs (clé USB et disque interne). */
add_disk_arg:
    movw    $cmdline, %di
1:  cmpb    $0, (%di)
    je      2f
    incw    %di
    jmp     1b
2:  movw    $m_diskarg, %si
3:  lodsb
    testb   %al, %al
    jz      4f
    stosb
    jmp     3b
4:  movl    0x7DB8, %edx                /* signature du MBR chargé par le BIOS (0x7C00 + 440) */
    movw    $8, %cx
5:  roll    $4, %edx
    movb    %dl, %al
    andb    $0x0F, %al
    addb    $'0', %al
    cmpb    $'9', %al
    jbe     6f
    addb    $39, %al                    /* a à f */
6:  stosb
    loop    5b
    movb    $0, (%di)
    ret

/* Le BIOS ne propose pas toujours la résolution de l'écran (VAIO : 1280x1024 sur un écran
 * 1920x1080, étiré par l'écran). BOOT.IMG peut contenir une 2e série d'images, dessinée
 * pour ce mode et cet écran : une fois étirée, elle a les bonnes proportions. */
pick_variant:
    movl    $BIMG, %esi
    movl    %esi, bimg_base
    movl    56(%esi), %eax              /* position de la variante (0 : aucune) */
    testl   %eax, %eax
    jz      9f
    movzwl  60(%esi), %ecx
    cmpl    scr_w, %ecx
    jne     9f
    movzwl  62(%esi), %ecx
    cmpl    scr_h, %ecx
    jne     9f
    cmpl    $0, native_w                /* écran inconnu (EDID refusé par le BIOS) : on suppose */
    je      8f                          /* l'écran pour lequel la variante est faite */
    movzwl  64(%esi), %ecx
    cmpl    native_w, %ecx
    jne     9f
    movzwl  66(%esi), %ecx
    cmpl    native_h, %ecx
    jne     9f
8:
    addl    $BIMG, %eax
    cmpl    $0x4D494254, (%eax)
    jne     9f
    movl    %eax, bimg_base
9:  ret

/* ---------------- données ---------------- */
drive:      .byte 0x80
gfx_mode:   .byte 0                     /* mode graphique réglé et dessinable */
gfx:        .byte 0                     /* écran de démarrage affiché */
use_off:    .byte 0                     /* dessin dans l'image de travail (écran 16/24 bits) */
bayer:      .byte 0, 2, 3, 1            /* tramage 2 x 2 */
    .balign 2
mode_off:   .word 0
mode_seg:   .word 0
cur_mode:   .word 0
best_mode:  .word 0
chunk:      .word 0
tries:      .word 0
dots:       .word 0
cur_frame:  .word 0xFFFF
vbe_len:    .word 0
    .balign 4
best_area:  .long 0
bimg_base:  .long BIMG
native_w:   .long 0
native_h:   .long 0
cur_lba:    .long 0
left:       .long 0
dest:       .long 0
fb_base:    .long 0
fb_pitch:   .long 0
scr_w:      .long 0
scr_h:      .long 0
rx:         .long 0
ry:         .long 0
rw:         .long 0
rh:         .long 0
bgcol:      .long 0
real_base:  .long 0
real_pitch: .long 0
real_bpp:   .long 4
fgcol:      .long 0
dap:        .byte 0x10, 0
dap_count:  .word 0
dap_off:    .word 0
dap_seg:    .word 0
dap_lba:    .quad 0

    .balign 8
gdt:
    .quad 0
    .quad 0
    .quad 0x00CF9A000000FFFF            /* 0x10 code 32 bits, 4 Go */
    .quad 0x00CF92000000FFFF            /* 0x18 données, 4 Go */
gdt_end:
gdtr:
    .word gdt_end - gdt - 1
    .long gdt

m_diskarg: .asciz " titanos.disk="
m_vbearg:  .asciz " titanos.vbe="
vbe_list:  .fill 220, 1, 0
m_hello:  .asciz "TitanOs beta 31 - demarrage BIOS\r\n"
m_kernel: .asciz "  Noyau Linux "
m_initrd: .asciz "  Systeme TitanOs "
m_ok:     .asciz " OK\r\n"
m_dot:    .asciz "."
m_disk:   .asciz "\r\nErreur : lecture du disque impossible."
m_novbe:  .asciz "  Pas de mode graphique VBE : demarrage en mode texte.\r\n"

/* zero page (boot_params) et ligne de commande, préparées par tools/mkimage.py */
    .balign 16
zeropage:
    .ascii  "TZPG"
    .fill   4092, 1, 0
cmdline:
    .ascii  "TCMD"
    .fill   508, 1, 0
