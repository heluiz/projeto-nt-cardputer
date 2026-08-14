#pragma once
#include <M5Cardputer.h>
#include "Theme.h"

// ===================================================================
//  Tela de inicializacao em estilo terminal.
//  Nao e enfeite: cada linha mostra em que etapa o aparelho parou.
// ===================================================================

#define BOOT_MAX_LINES 9

class BootScreen {
public:
    void begin(M5Canvas *canvas)
    {
        g = canvas;
        count = 0;
        draw();
    }

    // status: 0 = em andamento, 1 = ok, 2 = falha, 3 = aviso
    void step(const char *label, const char *detail, int status)
    {
        if (count >= BOOT_MAX_LINES) {
            for (int i = 1; i < BOOT_MAX_LINES; i++) lines[i - 1] = lines[i];
            count = BOOT_MAX_LINES - 1;
        }
        snprintf(lines[count].label, sizeof(lines[count].label), "%s", label);
        snprintf(lines[count].detail, sizeof(lines[count].detail), "%s", detail ? detail : "");
        lines[count].status = status;
        count++;
        draw();
    }

    // atualiza a ultima linha, para etapas demoradas
    void update(const char *detail, int status)
    {
        if (count == 0) return;
        snprintf(lines[count - 1].detail, sizeof(lines[count - 1].detail), "%s", detail ? detail : "");
        lines[count - 1].status = status;
        draw();
    }

    void finish(const char *msg)
    {
        done = true;
        snprintf(readyMsg, sizeof(readyMsg), "%s", msg);
        draw();
    }

private:
    struct Line {
        char label[22];
        char detail[24];
        int  status;
    };

    M5Canvas *g = nullptr;
    Line lines[BOOT_MAX_LINES];
    int  count = 0;
    bool done = false;
    char readyMsg[24] = "";

    void draw()
    {
        if (!g) return;

        g->fillScreen(TH.bg);
        g->setFont(&fonts::Font0);
        g->setTextSize(1);

        g->setTextColor(TH.accent2, TH.bg);
        g->drawString("CARDPUTER://trader", 5, 3);
        g->setTextColor(TH.dim, TH.bg);
        const char *v = "v2";
        g->drawString(v, 235 - g->textWidth(v), 3);
        g->drawFastHLine(4, 13, 232, TH.hdr);

        for (int i = 0; i < count; i++) {
            int y = 18 + i * 11;

            const char *tag;
            uint16_t col;
            switch (lines[i].status) {
                case 1:  tag = "[ OK ]"; col = TH.accent;  break;
                case 2:  tag = "[FALHA]"; col = TH.dn;     break;
                case 3:  tag = "[ ?? ]"; col = TH.accent2; break;
                default: tag = "[ .. ]"; col = TH.dim;     break;
            }

            g->setTextColor(col, TH.bg);
            g->drawString(tag, 5, y);

            g->setTextColor(lines[i].status == 2 ? TH.dn : TH.text, TH.bg);
            g->drawString(lines[i].label, 50, y);

            if (lines[i].detail[0]) {
                g->setTextColor(TH.dim, TH.bg);
                int w = g->textWidth(lines[i].detail);
                g->drawString(lines[i].detail, 235 - w, y);
            }
        }

        if (done) {
            int y = 18 + count * 11 + 4;
            if (y > 124) y = 124;
            g->setTextColor(TH.up, TH.bg);
            char l[32];
            snprintf(l, sizeof(l), "> %s", readyMsg);
            g->drawString(l, 5, y);
            g->fillRect(5 + g->textWidth(l) + 2, y, 5, 8, TH.up);
        }

        g->pushSprite(0, 0);
    }
};
