/* TitanOs - secteur de démarrage MBR (démarrage BIOS classique)
 *
 * Le BIOS charge ce secteur à 0x7C00 et y saute en mode réel 16 bits.
 * On lit le chargeur de 2e étape (64 secteurs à partir du LBA 1) à 0x8000
 * avec les extensions INT 13h, puis on lui passe la main.
 * Le code doit tenir dans 440 octets (la table de partitions suit). */

    .code16
    .text
    .globl start
start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp
    ljmp    $0, $norm           /* certains BIOS sautent en 07C0:0000 */
norm:
    sti
    cld
    movb    %dl, drive

    movb    $0x41, %ah          /* extensions INT 13h présentes ? */
    movw    $0x55AA, %bx
    int     $0x13
    jc      fail
    cmpw    $0xAA55, %bx
    jne     fail

    movw    $dap, %si
    movb    $0x42, %ah          /* lecture LBA étendue */
    movb    drive, %dl
    int     $0x13
    jc      fail

    movb    drive, %dl
    ljmp    $0, $0x8000

fail:
    movw    $msg, %si
1:  lodsb
    testb   %al, %al
    jz      2f
    movb    $0x0E, %ah
    movw    $0x0007, %bx
    int     $0x10
    jmp     1b
2:  cli
    hlt
    jmp     2b

drive:  .byte 0x80
    .balign 4
dap:    .byte 0x10, 0
        .word 64                /* secteurs */
        .word 0x8000, 0x0000    /* offset, segment */
        .quad 1                 /* LBA */
msg:    .asciz "TitanOs : erreur de lecture du disque."
