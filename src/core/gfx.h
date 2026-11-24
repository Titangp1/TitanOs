/* TitanOs - moteur graphique 2D lissé
 *
 * Toutes les surfaces sont en ARGB 32 bits prémultiplié. Les couleurs passées aux
 * fonctions sont en 0xAARRGGBB non prémultiplié (A = opacité, 255 = opaque). */
#pragma once
#include "base.h"
#include "font.h"

typedef struct { uint32_t *px; int w, h, stride; rect_t clip; } canvas_t;

#define OPAQUE(c)   (0xFF000000u | (uint32_t)(c))
#define ALPHA(c, a) (((uint32_t)(a) << 24) | ((uint32_t)(c) & 0xFFFFFFu))

canvas_t canvas_new(int w, int h);
void     canvas_free(canvas_t *c);
void     canvas_clip(canvas_t *c, rect_t r);
void     canvas_noclip(canvas_t *c);
void     canvas_clear(canvas_t *c, uint32_t pm);   /* écrit la valeur telle quelle */

uint32_t col_mix(uint32_t a, uint32_t b, int t);   /* t de 0 à 256 */
uint32_t col_scale(uint32_t c, int f);             /* luminosité * f / 256 */

void gfx_fill(canvas_t *c, rect_t r, uint32_t argb);
void gfx_fill_grad(canvas_t *c, rect_t r, uint32_t top, uint32_t bottom);
void gfx_rrect(canvas_t *c, float x, float y, float w, float h, float r, uint32_t argb);
void gfx_rrect_grad(canvas_t *c, float x, float y, float w, float h, float r, uint32_t top, uint32_t bottom);
void gfx_rrect_stroke(canvas_t *c, float x, float y, float w, float h, float r, float t, uint32_t argb);
void gfx_circle(canvas_t *c, float cx, float cy, float r, uint32_t argb);
void gfx_ring(canvas_t *c, float cx, float cy, float r, float t, uint32_t argb);
void gfx_line(canvas_t *c, float x0, float y0, float x1, float y1, float t, uint32_t argb);
void gfx_arc(canvas_t *c, float cx, float cy, float r, float t, float a0, float a1, uint32_t argb);   /* angles en radians, 0 = droite, sens horaire */
void gfx_tri(canvas_t *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t top, uint32_t bottom);
void gfx_shadow(canvas_t *c, float x, float y, float w, float h, float r, float blur, float dy, uint32_t argb);   /* ombre décalée de dy sous la forme */
void gfx_blit(canvas_t *dst, int x, int y, const canvas_t *src, int alpha);
void gfx_blit_rect(canvas_t *dst, int x, int y, const canvas_t *src, rect_t sr, int alpha);
void gfx_copy(canvas_t *dst, int x, int y, const canvas_t *src, rect_t sr);

int  text_width(const font_t *f, const char *s);
int  text_width_n(const font_t *f, const char *s, int nbytes);
int  text_draw(canvas_t *c, const font_t *f, int x, int y, const char *s, uint32_t argb);
int  text_draw_n(canvas_t *c, const font_t *f, int x, int y, const char *s, int nbytes, uint32_t argb);
void text_fit(canvas_t *c, const font_t *f, int x, int y, int maxw, const char *s, uint32_t argb);
void text_center(canvas_t *c, const font_t *f, rect_t r, const char *s, uint32_t argb);
int  text_index_at(const font_t *f, const char *s, int x);
void mono_char(canvas_t *c, int x, int y, int cp, uint32_t argb);

/* icônes et logo (icons.c) */
void draw_logo(canvas_t *c, float cx, float top, float size, int alpha);
void draw_icon(canvas_t *c, int icon, float x, float y, float s);
