/* TitanOs - noms des fabricants, des puces et des classes PCI */
#include "../core/base.h"

const char *pci_vendor_name(uint16_t v) {
    switch (v) {
    case 0x8086: return "Intel";
    case 0x10DE: return "NVIDIA";
    case 0x1002: return "AMD/ATI";
    case 0x1022: return "AMD";
    case 0x11AB: return "Marvell";
    case 0x168C: return "Atheros";
    case 0x10EC: return "Realtek";
    case 0x1180: return "Ricoh";
    case 0x104D: return "Sony";
    case 0x1033: return "NEC";
    case 0x197B: return "JMicron";
    case 0x14E4: return "Broadcom";
    case 0x1B21: return "ASMedia";
    case 0x1106: return "VIA";
    case 0x1039: return "SiS";
    case 0x1217: return "O2 Micro";
    case 0x15AD: return "VMware";
    case 0x80EE: return "VirtualBox";
    case 0x1234: return "QEMU";
    case 0x1AF4: return "Red Hat (virtio)";
    }
    return "Inconnu";
}

/* Puces connues, en particulier celles du Sony VAIO série F (PCG-81112M) */
const char *pci_device_name(uint16_t v, uint16_t d) {
    static const struct { uint16_t v, d; const char *name; } known[] = {
        { 0x10DE, 0x0A29, "GeForce GT 330M" },
        { 0x10DE, 0x0A75, "GeForce 310M" },
        { 0x10DE, 0x0BE2, "Audio HDMI NVIDIA" },
        { 0x11AB, 0x4380, "Yukon 88E8057 (Ethernet)" },
        { 0x168C, 0x002E, "AR9287 (Wi-Fi)" },
        { 0x8086, 0x422C, "Centrino Advanced-N 6200" },
        { 0x8086, 0x4239, "Centrino Advanced-N 6200" },
        { 0x8086, 0x4238, "Centrino Ultimate-N 6300" },
        { 0x8086, 0x3B34, "USB 2.0 EHCI n°2 (série 5)" },
        { 0x8086, 0x3B3C, "USB 2.0 EHCI n°1 (série 5)" },
        { 0x8086, 0x3B56, "Audio HD (série 5)" },
        { 0x8086, 0x3B29, "SATA AHCI (série 5)" },
        { 0x8086, 0x3B2F, "SATA AHCI (série 5)" },
        { 0x8086, 0x3B30, "SMBus (série 5)" },
        { 0x8086, 0x3B42, "PCI Express n°1 (série 5)" },
        { 0x8086, 0x3B44, "PCI Express n°2 (série 5)" },
        { 0x8086, 0x3B46, "PCI Express n°3 (série 5)" },
        { 0x8086, 0x3B48, "PCI Express n°4 (série 5)" },
        { 0x8086, 0x3B03, "Pont LPC PM55" },
        { 0x8086, 0x2448, "Pont PCI mobile" },
        { 0x8086, 0x3B4A, "PCI Express n°5 (série 5)" },
        { 0x8086, 0x3B4C, "PCI Express n°6 (série 5)" },
        { 0x8086, 0xD150, "Registres processeur (QPI)" },
        { 0x8086, 0xD151, "Registres processeur (QPI)" },
        { 0x8086, 0xD155, "Registres processeur" },
        { 0x8086, 0xD156, "Registres processeur" },
        { 0x8086, 0xD157, "Registres processeur" },
        { 0x8086, 0xD158, "Registres processeur" },
        { 0x1180, 0xE822, "Lecteur de cartes SD/MMC" },
        { 0x1180, 0xE230, "Lecteur Memory Stick" },
        { 0x1180, 0xE832, "FireWire (IEEE 1394)" },
        { 0x8086, 0xD130, "Processeur Core i7 (DMI)" },
        { 0x8086, 0xD131, "Processeur Core i7 (DMI)" },
        { 0x8086, 0xD132, "Processeur Core i7 (DMI)" },
        { 0x8086, 0xD138, "Processeur Core i7 (PCIe)" },
    };
    for (unsigned i = 0; i < sizeof known / sizeof known[0]; i++)
        if (known[i].v == v && known[i].d == d) return known[i].name;
    if (v == 0x8086 && d >= 0x2C50 && d <= 0x2CFF) return "Registres processeur (uncore)";
    return NULL;
}

const char *pci_class_name(uint8_t c, uint8_t s, uint8_t p) {
    switch (c) {
    case 0x01:
        if (s == 0x06) return "Contrôleur SATA";
        if (s == 0x01) return "Contrôleur IDE";
        if (s == 0x08) return "Contrôleur NVMe";
        return "Contrôleur de stockage";
    case 0x02: return s == 0x80 ? "Carte réseau sans fil" : "Carte réseau";
    case 0x03: return "Carte graphique";
    case 0x04: return s == 0x03 ? "Audio HD" : "Multimédia";
    case 0x05: return "Mémoire";
    case 0x06:
        if (s == 0x00) return "Pont hôte";
        if (s == 0x01) return "Pont ISA (LPC)";
        if (s == 0x04) return "Pont PCI / PCI Express";
        return "Pont";
    case 0x07: return "Communication";
    case 0x08: return "Périphérique système";
    case 0x09: return "Entrée";
    case 0x0C:
        if (s == 0x03) {
            if (p == 0x00) return "USB 1.1 (UHCI)";
            if (p == 0x10) return "USB 1.1 (OHCI)";
            if (p == 0x20) return "USB 2.0 (EHCI)";
            if (p == 0x30) return "USB 3.0 (xHCI)";
            return "Contrôleur USB";
        }
        if (s == 0x05) return "SMBus";
        if (s == 0x00) return "FireWire";
        return "Bus série";
    case 0x0D: return "Sans fil";
    case 0x11: return "Acquisition de données";
    }
    return "Autre";
}
