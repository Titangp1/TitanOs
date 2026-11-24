/* TitanOs - polices : Inter lissée (générée au build) et police 8x16 maison */
#pragma once
#include "base.h"

typedef struct {
    uint32_t off;          /* position dans le tableau de pixels */
    uint16_t cp;           /* caractère Unicode */
    uint8_t  w, h;         /* taille du dessin */
    int8_t   ox, oy;       /* décalage par rapport au point de base (oy < 0 : au-dessus) */
    uint16_t adv;          /* avance horizontale, en 1/64 de pixel */
} glyph_t;

typedef struct {
    const glyph_t *glyphs;
    int count;
    const uint8_t *bits;
    int size, ascent, descent, line;
} font_t;

extern const font_t font_small;    /* Inter 12 px */
extern const font_t font_body;     /* Inter 14 px */
extern const font_t font_bold;     /* Inter SemiBold 14 px */
extern const font_t font_title;    /* Inter SemiBold 17 px */
extern const font_t font_h2;       /* Inter Display SemiBold 24 px */
extern const font_t font_h1;       /* Inter Display SemiBold 36 px */
extern const font_t font_clock;    /* Inter Display SemiBold 128 px (chiffres) */
extern const uint8_t font8x16[256][16];   /* police bitmap dessinée pour TitanOs */
