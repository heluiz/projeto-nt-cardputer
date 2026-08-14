#pragma once
#include <Arduino.h>

// ===================================================================
//  Temas visuais. Tecla T alterna, escolha salva na memoria interna.
// ===================================================================

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// fonte usada no preco grande
#define PRICEFONT_ROUND 0   // Font6, digitos arredondados
#define PRICEFONT_SEG   1   // Font7, sete segmentos

struct Theme {
    const char *name;
    uint16_t bg, hdr, text, dim, up, dn, upFill, dnFill, accent, accent2;
    bool scanlines;
    bool brackets;
    uint8_t priceFont;
};

static const Theme THEMES[] = {
    {   // 0 - neon noir
        "NEON",
        RGB565(0x05, 0x06, 0x0A), RGB565(0x16, 0x28, 0x3C),
        RGB565(0xC8, 0xF5, 0xFF), RGB565(0x3E, 0x5A, 0x75),
        RGB565(0x39, 0xFF, 0x88), RGB565(0xFF, 0x3B, 0x5C),
        RGB565(0x0E, 0x3A, 0x2A), RGB565(0x3A, 0x0E, 0x1A),
        RGB565(0x22, 0xD3, 0xEE), RGB565(0xF0, 0x36, 0x9B),
        false, true, PRICEFONT_ROUND
    },
    {   // 1 - fosforo ambar
        "AMBAR",
        RGB565(0x0A, 0x07, 0x00), RGB565(0x24, 0x1A, 0x00),
        RGB565(0xFF, 0xCF, 0x5C), RGB565(0x8A, 0x60, 0x00),
        RGB565(0x39, 0xFF, 0x88), RGB565(0xFF, 0x3B, 0x5C),
        RGB565(0x0E, 0x3A, 0x2A), RGB565(0x3A, 0x0E, 0x1A),
        RGB565(0xFF, 0xB0, 0x00), RGB565(0xFF, 0x7A, 0x00),
        true, false, PRICEFONT_SEG
    },
    {   // 2 - fosforo verde
        "MATRIX",
        RGB565(0x00, 0x08, 0x03), RGB565(0x0A, 0x2A, 0x14),
        RGB565(0xB4, 0xFF, 0xC8), RGB565(0x2E, 0x7D, 0x4E),
        RGB565(0x39, 0xFF, 0x88), RGB565(0xFF, 0x5A, 0x5A),
        RGB565(0x0A, 0x3A, 0x20), RGB565(0x3A, 0x12, 0x12),
        RGB565(0x39, 0xFF, 0x88), RGB565(0x9A, 0xFF, 0x4A),
        true, true, PRICEFONT_SEG
    }
};

#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

static int   gThemeIdx = 0;
static Theme TH = THEMES[0];

inline void applyTheme(int idx)
{
    if (idx < 0 || idx >= THEME_COUNT) idx = 0;
    gThemeIdx = idx;
    TH = THEMES[idx];
}

inline int nextTheme()
{
    applyTheme((gThemeIdx + 1) % THEME_COUNT);
    return gThemeIdx;
}
