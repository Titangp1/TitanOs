/* TitanOs - matériau « liquid glass » */
#pragma once
#include "gfx.h"

typedef struct {
    int      down;        /* réduction avant le flou (1, 2 ou 4) */
    int      radius;      /* rayon du flou de boîte, en pixels réduits */
    int      passes;      /* nombre de passes (3 ≈ flou gaussien) */
    float    sat;         /* saturation du fond (1 = inchangée) */
    float    bright;      /* luminosité du fond */
    uint32_t tint;        /* teinte du verre */
    float    tint_a;      /* opacité de la teinte (0-1) */
    float    refract;     /* déplacement maximal sur les bords, en pixels */
    float    band;        /* largeur de la zone de réfraction */
    float    edge_glow;   /* éclaircissement des bords */
    float    rim;         /* intensité du liseré lumineux */
    float    sheen;       /* reflet doux en haut */
} glass_t;

void glass_render(canvas_t *out, const canvas_t *scene, rect_t r, float radius, const glass_t *g);
