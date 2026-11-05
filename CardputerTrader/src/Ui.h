#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <string.h>
#include "Data.h"
#include "Theme.h"

// ---------------------------------------------------------------
//  Cores vem do tema ativo (Theme.h)
// ---------------------------------------------------------------
#define C_BG     TH.bg
#define C_HDR    TH.hdr
#define C_TEXT   TH.text
#define C_DIM    TH.dim
#define C_UP     TH.up
#define C_DN     TH.dn
#define C_UPF    TH.upFill
#define C_DNF    TH.dnFill
#define C_ACC    TH.accent
#define C_ACC2   TH.accent2

inline uint16_t colorFor(float v) { return (v > 0) ? C_UP : (v < 0 ? C_DN : C_DIM); }

// ===============================================================
//  Elementos pequenos
// ===============================================================

// alto-falante: mudo, so posicao, ou tudo
inline void drawSoundIcon(M5Canvas &g, int x, int y, int mode)
{
    uint16_t col = (mode <= 0) ? C_DN : (mode == 1 ? C_DIM : C_UP);

    // corpo e cone
    g.fillRect(x, y + 3, 2, 3, col);
    g.fillTriangle(x + 2, y + 6, x + 5, y + 1, x + 5, y + 8, col);

    if (mode <= 0) {
        // risco atravessando: mudo
        g.drawLine(x + 6, y + 1, x + 10, y + 7, C_DN);
        g.drawLine(x + 6, y + 7, x + 10, y + 1, C_DN);
    } else {
        // uma onda para posicao, duas para tudo
        g.drawPixel(x + 7, y + 3, col);
        g.drawPixel(x + 7, y + 4, col);
        g.drawPixel(x + 7, y + 5, col);
        if (mode >= 2) {
            g.drawPixel(x + 9, y + 2, col);
            g.drawPixel(x + 10, y + 3, col);
            g.drawPixel(x + 10, y + 4, col);
            g.drawPixel(x + 10, y + 5, col);
            g.drawPixel(x + 9, y + 6, col);
        }
    }
}

inline void drawBatteryIcon(M5Canvas &g, int x, int y, int pct, uint16_t bg)
{
    uint16_t col = (pct <= 15) ? C_DN : (pct <= 35 ? C_ACC2 : C_DIM);
    g.drawRect(x, y, 16, 8, col);
    g.fillRect(x + 16, y + 2, 2, 4, col);
    int w = (pct * 14) / 100;
    if (w < 0) w = 0;
    if (w > 14) w = 14;
    if (w > 0) g.fillRect(x + 1, y + 1, w, 6, col);
}

inline void drawWifiBars(M5Canvas &g, int x, int y, int rssi, bool linked)
{
    int bars = 0;
    if (linked) {
        if (rssi > -60) bars = 4;
        else if (rssi > -70) bars = 3;
        else if (rssi > -80) bars = 2;
        else bars = 1;
    }
    for (int i = 0; i < 4; i++) {
        int h = 2 + i * 2;
        uint16_t col = (i < bars) ? (bars >= 3 ? C_UP : C_ACC2) : C_HDR;
        if (!linked) col = (i == 0) ? C_DN : C_HDR;
        g.fillRect(x + i * 3, y + 8 - h, 2, h, col);
    }
}

// seta do ultimo negocio
inline void drawTickArrow(M5Canvas &g, int x, int y, int tick)
{
    if (tick > 0) {
        g.fillTriangle(x, y + 9, x + 5, y, x + 10, y + 9, C_UP);
    } else if (tick < 0) {
        g.fillTriangle(x, y, x + 10, y, x + 5, y + 9, C_DN);
    }
}

// Faixa superior: nome completo do contrato ativo, seguido de um
// marcador por contrato. Antes eu escrevia todos os nomes e o ultimo
// nao cabia; com marcadores cabem ate dez sem cortar nenhum.
inline void drawStrip(M5Canvas &g, const Snapshot &s, int active, int wRight)
{
    g.setFont(&fonts::Font0);
    g.setTextSize(1);

    const char *nome = (s.n > 0) ? s.q[active].sym : "sem contrato";
    g.setTextColor(C_ACC, C_BG);
    g.drawString(nome, 3, 1);

    int x = 3 + g.textWidth(nome) + 9;
    int limit = 240 - wRight;

    for (int i = 0; i < s.n; i++) {
        if (x + 6 > limit) {
            g.setTextColor(C_DIM, C_BG);
            g.drawString("+", x, 1);
            break;
        }

        uint16_t col = colorFor(s.q[i].pct);
        if (i == active) g.fillRect(x, 1, 5, 8, col);
        else             g.drawRect(x, 2, 5, 6, col);

        if (s.q[i].alert && (millis() / 400) % 2 == 0)
            g.fillRect(x - 1, 10, 7, 1, C_ACC2);

        x += 7;
    }
}

// ===============================================================
//  Grafico
// ===============================================================
inline void drawSparkArr(M5Canvas &g, const float *pts, int n, int dec, float refLine,
                         int x, int y, int w, int h, bool labels, int gridEverySec, int secPerPt)
{
    if (n < 2) {
        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        const char *msg = "aguardando historico...";
        g.drawString(msg, x + (w - g.textWidth(msg)) / 2, y + h / 2 - 4);
        return;
    }

    float mn = pts[0], mx = pts[0];
    for (int i = 1; i < n; i++) {
        if (pts[i] < mn) mn = pts[i];
        if (pts[i] > mx) mx = pts[i];
    }
    float range = mx - mn;
    if (range < 1e-6f) { mn -= 0.5f; mx += 0.5f; range = mx - mn; }

    bool up = (pts[n - 1] >= pts[0]);
    uint16_t cLine = up ? C_UP : C_DN;
    uint16_t cFill = up ? C_UPF : C_DNF;

    if (w > 240) w = 240;
    static int16_t ys[240];

    for (int col = 0; col < w; col++) {
        float t    = (float)(n - 1) * (float)col / (float)(w - 1);
        int   i0   = (int)t;
        int   i1   = (i0 + 1 < n) ? i0 + 1 : n - 1;
        float frac = t - (float)i0;
        float v    = pts[i0] * (1.0f - frac) + pts[i1] * frac;

        int py = y + h - 1 - (int)((v - mn) / range * (float)(h - 1));
        if (py < y) py = y;
        if (py > y + h - 1) py = y + h - 1;
        ys[col] = (int16_t)py;
    }

    for (int col = 0; col < w; col++)
        g.drawFastVLine(x + col, ys[col], y + h - ys[col], cFill);

    if (TH.scanlines)
        for (int sy = y + 1; sy < y + h; sy += 3) g.drawFastHLine(x, sy, w, C_BG);

    // grade de tempo
    if (gridEverySec > 0 && secPerPt > 0) {
        float ptsPerGrid = (float)gridEverySec / (float)secPerPt;
        if (ptsPerGrid > 0.5f) {
            float colStep = ptsPerGrid * (float)(w - 1) / (float)(n - 1);
            if (colStep >= 8.0f) {
                for (float gx = (float)w - colStep; gx > 0; gx -= colStep)
                    for (int gy = y; gy < y + h; gy += 3)
                        g.drawPixel(x + (int)gx, gy, C_HDR);
            }
        }
    }

    if (refLine >= mn && refLine <= mx) {
        int ry = y + h - 1 - (int)((refLine - mn) / range * (float)(h - 1));
        for (int dx = x; dx < x + w; dx += 5) g.drawPixel(dx, ry, C_DIM);
    }

    for (int col = 1; col < w; col++)
        g.drawLine(x + col - 1, ys[col - 1], x + col, ys[col], cLine);

    if (labels) {
        char b[20];
        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        fmtPrice(b, sizeof(b), mx, dec);
        g.drawString(b, x + w - g.textWidth(b) - 2, y + 1);
        fmtPrice(b, sizeof(b), mn, dec);
        g.drawString(b, x + w - g.textWidth(b) - 2, y + h - 9);
    }
}

inline void drawSpark(M5Canvas &g, const Quote &q, int x, int y, int w, int h, bool labels)
{
    drawSparkArr(g, q.spark, q.sparkN, q.dec, q.prev, x, y, w, h, labels, 60, q.sint);
}

// ===============================================================
//  Moldura de alerta
// ===============================================================
inline void drawAlertFrame(M5Canvas &g, bool on)
{
    if (!on) return;
    if ((millis() / 400) % 2) return;
    g.drawRect(0, 0, 240, 135, C_ACC2);
    g.drawRect(1, 1, 238, 133, C_ACC2);
}

// ===============================================================
//  Tela 1: painel
// ===============================================================
inline void drawDashboard(M5Canvas &g, const Snapshot &s, int idx, bool connOk, int bat, uint32_t ageMs, int som)
{
    const Quote &q = s.q[idx];
    bool stale = !connOk;
    uint16_t cVar = stale ? C_DIM : colorFor(q.chg);

    g.fillScreen(C_BG);

    // ---- faixa de contratos + status ----
    drawStrip(g, s, idx, 104);

    g.setFont(&fonts::Font0);
    char pc[16];
    snprintf(pc, sizeof(pc), "%+.2f%%", q.pct);
    int pw = g.textWidth(pc);
    g.setTextColor(stale ? C_DIM : colorFor(q.pct), C_BG);
    g.drawString(pc, 174 - pw, 1);

    drawSoundIcon(g, 178, 1, som);
    drawWifiBars(g, 192, 1, (int)WiFi.RSSI(), WiFi.status() == WL_CONNECTED);
    drawBatteryIcon(g, 206, 1, bat, C_BG);

    // ---- preco ----
    // A fonte NUNCA muda dentro de um tema. Se o numero nao couber,
    // reduzimos as casas decimais, nao o tamanho das letras.
    char price[24];
    g.setTextColor(cVar, C_BG);
    if (TH.priceFont == PRICEFONT_SEG) g.setFont(&fonts::Font7);
    else                               g.setFont(&fonts::Font6);

    int dcs = q.dec;
    fmtPrice(price, sizeof(price), q.last, dcs);
    while (g.textWidth(price) > 222 && dcs > 0) {
        dcs--;
        fmtPrice(price, sizeof(price), q.last, dcs);
    }
    int pxw = g.textWidth(price);
    int py = 12;
    g.drawString(price, 4, py);

    drawTickArrow(g, 4 + pxw + 6, 32, stale ? 0 : q.tick);

    // ---- variacao e spread ----
    g.setFont(&fonts::Font0);
    char chg[20];
    fmtSigned(chg, sizeof(chg), q.chg, q.dec);
    g.setTextColor(cVar, C_BG);
    g.drawString(chg, 4, 62);

    if (q.bid > 0 && q.ask > 0) {
        char sp[24];
        snprintf(sp, sizeof(sp), "B %.*f  A %.*f", q.dec, q.bid, q.dec, q.ask);
        g.setTextColor(C_DIM, C_BG);
        g.drawString(sp, 236 - g.textWidth(sp), 62);
    }

    g.drawFastHLine(0, 72, 240, C_HDR);

    // ---- grafico ----
    drawSpark(g, q, 3, 74, 234, 31, false);

    // ---- posicao ----
    g.setFont(&fonts::Font2);
    char posTxt[48];
    if (q.pos == 0) {
        g.setTextColor(C_DIM, C_BG);
        snprintf(posTxt, sizeof(posTxt), "SEM POSICAO  %s", s.acct);
        g.drawString(posTxt, 3, 107);
    } else if (false) {
    } else {
        char avg[16];
        fmtPrice(avg, sizeof(avg), q.avg, q.dec);
        snprintf(posTxt, sizeof(posTxt), "%s %d @ %s",
                 q.pos > 0 ? "COMP" : "VEND", q.pos > 0 ? q.pos : -q.pos, avg);
        g.setTextColor(q.pos > 0 ? C_UP : C_DN, C_BG);
        g.drawString(posTxt, 3, 107);

        char upl[16];
        fmtMoney(upl, sizeof(upl), q.upl);
        g.setTextColor(colorFor(q.upl), C_BG);
        g.drawString(upl, 236 - g.textWidth(upl), 107);
    }

    if (q.nord > 0) {
        char od[16];
        snprintf(od, sizeof(od), "%d ORD", q.nord);
        g.setFont(&fonts::Font0);
        g.setTextColor(C_ACC2, C_BG);
        g.drawString(od, 236 - g.textWidth(od), 96);
    }

    // ---- rodape ----
    g.setFont(&fonts::Font0);
    char dia[24];
    snprintf(dia, sizeof(dia), "DIA %+.2f", s.total);
    int dw = g.textWidth(dia);
    g.setTextColor(s.accOk ? colorFor(s.total) : C_DIM, C_BG);
    g.drawString(dia, 236 - dw, 125);

    char o[16], h[16], l[16], ohl[52];
    fmtPrice(o, sizeof(o), q.open, q.dec);
    fmtPrice(h, sizeof(h), q.high, q.dec);
    fmtPrice(l, sizeof(l), q.low, q.dec);

    if (stale) {
        snprintf(ohl, sizeof(ohl), "DADO DE %lus ATRAS", (unsigned long)(ageMs / 1000));
        g.setTextColor(C_DN, C_BG);
    } else {
        snprintf(ohl, sizeof(ohl), "A%s H%s L%s", o, h, l);
        g.setTextColor(C_DIM, C_BG);
        if (g.textWidth(ohl) > 236 - dw - 8) snprintf(ohl, sizeof(ohl), "H%s L%s", h, l);
    }
    g.drawString(ohl, 3, 125);

    drawAlertFrame(g, q.alert != 0);
}

// ===============================================================
//  Tela 2: grafico em tela cheia
// ===============================================================
inline void drawChart(M5Canvas &g, const Snapshot &s, int idx, const DayHist &day,
                      bool dayMode, bool connOk, int bat, int som)
{
    const Quote &q = s.q[idx];

    g.fillScreen(C_BG);
    drawStrip(g, s, idx, 104);

    g.setFont(&fonts::Font0);
    const char *mode = dayMode ? "SESSAO" : "10 MIN";
    g.setTextColor(C_ACC, C_BG);
    g.drawString(mode, 174 - g.textWidth(mode), 1);
    drawSoundIcon(g, 178, 1, som);
    drawWifiBars(g, 192, 1, (int)WiFi.RSSI(), WiFi.status() == WL_CONNECTED);
    drawBatteryIcon(g, 206, 1, bat, C_BG);

    if (dayMode) {
        if (day.loading) {
            g.setFont(&fonts::Font2);
            g.setTextColor(C_DIM, C_BG);
            const char *m = "buscando a sessao...";
            g.drawString(m, (240 - g.textWidth(m)) / 2, 60);
        } else {
            drawSparkArr(g, day.pts, day.n, day.dec, q.prev, 2, 14, 236, 96, true, 3600, 30);
        }
    } else {
        drawSparkArr(g, q.spark, q.sparkN, q.dec, q.prev, 2, 14, 236, 96, true, 60, q.sint);
    }

    g.setFont(&fonts::Font2);
    char price[24];
    fmtPrice(price, sizeof(price), q.last, q.dec);
    g.setTextColor(connOk ? colorFor(q.chg) : C_DIM, C_BG);
    g.drawString(price, 3, 116);

    g.setFont(&fonts::Font0);
    char info[44];
    if (dayMode) snprintf(info, sizeof(info), "%d pts da sessao   D = 10min", day.n);
    else         snprintf(info, sizeof(info), "%d min   D = sessao", (q.sparkN * q.sint) / 60);
    g.setTextColor(C_DIM, C_BG);
    g.drawString(info, 236 - g.textWidth(info), 122);

    drawAlertFrame(g, q.alert != 0);
}

// ===============================================================
//  Tela 3: grade com todos os contratos
// ===============================================================
inline void drawGrid(M5Canvas &g, const Snapshot &s, int active, bool connOk, int bat, int som)
{
    g.fillScreen(C_BG);

    g.setFont(&fonts::Font0);
    g.setTextColor(C_ACC, C_BG);
    char gt[28];
    if (s.n > 8) snprintf(gt, sizeof(gt), "CONTRATOS  8 de %d", s.n);
    else         snprintf(gt, sizeof(gt), "CONTRATOS");
    g.drawString(gt, 4, 1);
    drawSoundIcon(g, 178, 1, som);
    drawWifiBars(g, 192, 1, (int)WiFi.RSSI(), WiFi.status() == WL_CONNECTED);
    drawBatteryIcon(g, 206, 1, bat, C_BG);
    g.drawFastHLine(0, 11, 240, C_HDR);

    if (s.n == 0) {
        g.setFont(&fonts::Font2);
        g.setTextColor(C_DIM, C_BG);
        g.drawString("Nenhum contrato", 4, 55);
        return;
    }

    const int COLS = 2, ROWS = 4, CW = 119, CH = 30;
    for (int i = 0; i < s.n && i < COLS * ROWS; i++) {
        const Quote &q = s.q[i];
        int cx = (i % COLS) * (CW + 2);
        int cy = 13 + (i / COLS) * CH;

        if (i == active) g.drawRect(cx, cy, CW, CH - 1, C_ACC);
        if (q.alert && (millis() / 400) % 2 == 0) g.drawRect(cx, cy, CW, CH - 1, C_ACC2);

        g.setFont(&fonts::Font0);
        g.setTextColor(i == active ? C_ACC : C_DIM, C_BG);
        g.drawString(q.root[0] ? q.root : q.sym, cx + 4, cy + 3);

        char pc[12];
        snprintf(pc, sizeof(pc), "%+.2f%%", q.pct);
        g.setTextColor(connOk ? colorFor(q.pct) : C_DIM, C_BG);
        g.drawString(pc, cx + CW - 4 - g.textWidth(pc), cy + 3);

        char px[20];
        fmtPrice(px, sizeof(px), q.last, q.dec);
        g.setFont(&fonts::Font2);
        g.setTextColor(connOk ? colorFor(q.chg) : C_DIM, C_BG);
        g.drawString(px, cx + 4, cy + 13);

        if (q.pos != 0) {
            char p[10];
            snprintf(p, sizeof(p), "%+d", q.pos);
            g.setFont(&fonts::Font0);
            g.setTextColor(q.pos > 0 ? C_UP : C_DN, C_BG);
            g.drawString(p, cx + CW - 4 - g.textWidth(p), cy + 18);
        }
    }
}

// ===============================================================
//  Tela 4: contas
// ===============================================================
inline void drawAccounts(M5Canvas &g, const Snapshot &s, int scroll, int bat)
{
    g.fillScreen(C_BG);

    g.setFont(&fonts::Font0);
    g.setTextSize(1);
    char title[32];
    snprintf(title, sizeof(title), "CONTAS  %s", s.acct[0] ? s.acct : "-");
    g.setTextColor(C_ACC, C_BG);
    g.drawString(title, 4, 1);
    drawBatteryIcon(g, 206, 1, bat, C_BG);
    g.drawFastHLine(0, 11, 240, C_HDR);

    if (s.accN == 0) {
        g.setFont(&fonts::Font2);
        g.setTextColor(C_DIM, C_BG);
        g.drawString("Nenhuma conta recebida", 4, 50);
        g.setFont(&fonts::Font0);
        g.drawString("Contas de simulacao sao ignoradas.", 4, 76);
        return;
    }

    g.setTextColor(C_DIM, C_BG);
    g.drawString("CONTA", 8, 15);
    g.drawString("REALIZ", 130 - g.textWidth("REALIZ"), 15);
    g.drawString("ABERTO", 184 - g.textWidth("ABERTO"), 15);
    g.drawString("TOTAL", 236 - g.textWidth("TOTAL"), 15);
    g.drawFastHLine(4, 24, 232, C_HDR);

    const int ROWS = 6;
    int shown = 0;
    for (int i = scroll; i < s.accN && shown < ROWS; i++, shown++) {
        const AccountInfo &a = s.acc[i];
        int y = 28 + shown * 14;

        g.fillCircle(7, y + 3, 2, a.conn ? C_UP : C_DIM);

        char nm[18];
        snprintf(nm, sizeof(nm), "%s", a.name);
        if (strlen(a.name) > 14) { nm[13] = 0; strcat(nm, "."); }
        g.setTextColor(a.conn ? C_TEXT : C_DIM, C_BG);
        g.drawString(nm, 14, y);

        char v[16];
        fmtMoneyShort(v, sizeof(v), a.realized);
        g.setTextColor(C_DIM, C_BG);
        g.drawString(v, 130 - g.textWidth(v), y);

        fmtMoneyShort(v, sizeof(v), a.unrealized);
        g.setTextColor(colorFor(a.unrealized), C_BG);
        g.drawString(v, 184 - g.textWidth(v), y);

        fmtMoneyShort(v, sizeof(v), a.total);
        g.setTextColor(colorFor(a.total), C_BG);
        g.drawString(v, 236 - g.textWidth(v), y);
    }

    if (scroll > 0) {
        g.setTextColor(C_ACC, C_BG);
        g.drawString("^", 232, 26);
    }
    if (scroll + shown < s.accN) {
        g.setTextColor(C_ACC, C_BG);
        g.drawString("v", 232, 104);
    }

    g.drawFastHLine(4, 115, 232, C_HDR);
    g.setFont(&fonts::Font2);
    g.setTextColor(C_DIM, C_BG);
    g.drawString("SOMA", 6, 118);

    char tot[16];
    fmtMoney(tot, sizeof(tot), s.total);
    g.setTextColor(colorFor(s.total), C_BG);
    g.drawString(tot, 236 - g.textWidth(tot), 118);

    if (s.accN > ROWS) {
        g.setFont(&fonts::Font0);
        char sc[32];
        snprintf(sc, sizeof(sc), "%d-%d/%d", scroll + 1, scroll + shown, s.accN);
        int scw = g.textWidth(sc);

        // setas indicando que ha mais acima e abaixo
        bool temAcima  = (scroll > 0);
        bool temAbaixo = (scroll + shown < s.accN);

        int cx = 120 - (scw + 26) / 2;
        g.setTextColor(temAcima ? C_ACC : C_HDR, C_BG);
        g.drawString(";", cx, 121);
        if (temAcima) g.fillTriangle(cx + 6, 126, cx + 10, 121, cx + 14, 126, C_ACC);

        g.setTextColor(C_DIM, C_BG);
        g.drawString(sc, cx + 18, 121);

        int dx = cx + 18 + scw + 4;
        g.setTextColor(temAbaixo ? C_ACC : C_HDR, C_BG);
        g.drawString(".", dx, 121);
        if (temAbaixo) g.fillTriangle(dx + 6, 121, dx + 10, 126, dx + 14, 121, C_ACC);
    }
}

// ===============================================================
//  Tela 5: estrategias
// ===============================================================
inline void drawStrats(M5Canvas &g, const Snapshot &s, int bat)
{
    g.fillScreen(C_BG);

    g.setFont(&fonts::Font0);
    char title[28];
    snprintf(title, sizeof(title), "ESTRATEGIAS  %d", s.stN);
    g.setTextColor(C_ACC, C_BG);
    g.drawString(title, 4, 1);
    drawBatteryIcon(g, 206, 1, bat, C_BG);
    g.drawFastHLine(0, 11, 240, C_HDR);

    if (s.stN == 0) {
        g.setFont(&fonts::Font2);
        g.setTextColor(C_DIM, C_BG);
        g.drawString("Nenhuma rodando", 4, 50);
        g.setFont(&fonts::Font0);
        g.drawString("Nas contas do filtro atual.", 4, 76);
        return;
    }

    for (int i = 0; i < s.stN && i < 9; i++) {
        const StratInfo &st = s.st[i];
        int y = 16 + i * 12;

        bool ativa = (strcmp(st.state, "Active") == 0 ||
                      strcmp(st.state, "Realtime") == 0 ||
                      strcmp(st.state, "Historical") == 0);

        g.fillCircle(7, y + 3, 2, ativa ? C_UP : C_DIM);

        char nm[22];
        snprintf(nm, sizeof(nm), "%.20s", st.name);
        g.setTextColor(ativa ? C_TEXT : C_DIM, C_BG);
        g.drawString(nm, 14, y);

        g.setTextColor(C_DIM, C_BG);
        char ac[14];
        snprintf(ac, sizeof(ac), "%.10s", st.acct);
        g.drawString(ac, 150 - g.textWidth(ac), y);

        g.setTextColor(ativa ? C_ACC : C_DIM, C_BG);
        char sv[14];
        snprintf(sv, sizeof(sv), "%.9s", st.state);
        g.drawString(sv, 236 - g.textWidth(sv), y);
    }

    g.setTextColor(C_DIM, C_BG);
    g.drawString("X desliga todas", 4, 126);
}

// ===============================================================
//  Tela 6: ultimas execucoes
// ===============================================================
inline void drawExecs(M5Canvas &g, const Snapshot &s, int bat)
{
    g.fillScreen(C_BG);

    g.setFont(&fonts::Font0);
    char title[28];
    snprintf(title, sizeof(title), "EXECUCOES  %d", s.exN);
    g.setTextColor(C_ACC, C_BG);
    g.drawString(title, 4, 1);
    drawBatteryIcon(g, 206, 1, bat, C_BG);
    g.drawFastHLine(0, 11, 240, C_HDR);

    if (s.exN == 0) {
        g.setFont(&fonts::Font2);
        g.setTextColor(C_DIM, C_BG);
        g.drawString("Nenhuma execucao hoje", 4, 50);
        return;
    }

    for (int i = 0; i < s.exN && i < 10; i++) {
        const ExecInfo &e = s.ex[i];
        int y = 15 + i * 11;
        bool comp = (e.side == 'C');

        g.setTextColor(C_DIM, C_BG);
        g.drawString(e.t, 4, y);

        g.setTextColor(C_TEXT, C_BG);
        g.drawString(e.sym, 56, y);

        char sq[14];
        snprintf(sq, sizeof(sq), "%s %d", comp ? "COMP" : "VEND", e.qty);
        g.setTextColor(comp ? C_UP : C_DN, C_BG);
        g.drawString(sq, 108, y);

        char px[16];
        fmtPrice(px, sizeof(px), e.px, e.dec);
        g.setTextColor(C_TEXT, C_BG);
        g.drawString(px, 236 - g.textWidth(px), y);
    }
}

// ===============================================================
//  Tela 9: ajuda
// ===============================================================
#define HELP_PAGES 3

inline void drawHelp(M5Canvas &g, int hpage, int bat)
{
    static const char *titulos[HELP_PAGES] = { "TELAS", "TECLAS", "LENDO O PAINEL" };

    static const char *k0[] = { "W", "P", "O", "A", "E", "H", "G", "S", "?" };
    static const char *d0[] = {
        "grade com todos os contratos",
        "posicoes abertas",
        "ordens de trabalho",
        "contas, uma a uma",
        "estrategias rodando",
        "ultimas 10 execucoes",
        "grafico em tela cheia",
        "status e diagnostico",
        "esta ajuda"
    };

    static const char *k1[] = { "TAB", "D", "; .", "ENTER", "T", "M", "B", "C", "X" };
    static const char *d1[] = {
        "troca de contrato",
        "grafico: 10min / sessao",
        "rolam listas e paginas",
        "atualiza agora",
        "troca o tema visual",
        "som: mudo / posicao / tudo",
        "apaga a tela, segue rodando",
        "configura rede (K = token)",
        "ZERAR TUDO (ENTER confirma)"
    };

    static const char *k2[] = { "faixa", "bolinha", "seta", "cinza", "-----", "borda", "N ORD", "rodape", "2 graves" };
    static const char *d2[] = {
        "contratos, cor = dia",
        "elo com o PC e sinal",
        "direcao do ultimo negocio",
        "conexao caiu, dado velho",
        "fechamento de ontem",
        "piscando = alerta cruzado",
        "ordens neste contrato",
        "abertura, max, min e o dia",
        "som: perdeu o PC"
    };

    const char **kk = (hpage == 0) ? k0 : (hpage == 1 ? k1 : k2);
    const char **dd = (hpage == 0) ? d0 : (hpage == 1 ? d1 : d2);

    g.fillScreen(C_BG);

    g.setFont(&fonts::Font0);
    g.setTextSize(1);
    char title[36];
    snprintf(title, sizeof(title), "AJUDA  %d/%d  %s", hpage + 1, HELP_PAGES, titulos[hpage]);
    g.setTextColor(C_ACC, C_BG);
    g.drawString(title, 4, 1);
    drawBatteryIcon(g, 206, 1, bat, C_BG);
    g.drawFastHLine(0, 11, 240, C_HDR);

    for (int i = 0; i < 9; i++) {
        if (kk[i][0] == 0) continue;
        int y = 16 + i * 11;

        bool perigo = (hpage == 1 && kk[i][0] == 'X');

        g.setTextColor(perigo ? C_DN : C_ACC2, C_BG);
        g.drawString(kk[i], 6, y);

        g.setTextColor(perigo ? C_DN : C_TEXT, C_BG);
        g.drawString(dd[i], 58, y);
    }

    g.drawFastHLine(0, 116, 240, C_HDR);
    g.setTextColor(C_DIM, C_BG);
    g.drawString("; . mudam de pagina", 6, 121);

    const char *sair = "? volta ao painel";
    g.drawString(sair, 234 - g.textWidth(sair), 121);
}

// ===============================================================
//  Tela 8: posicoes abertas, de todos os contratos
// ===============================================================
inline void drawPositions(M5Canvas &g, const Snapshot &s, int bat)
{
    g.fillScreen(C_BG);

    g.setFont(&fonts::Font0);
    char title[32];
    snprintf(title, sizeof(title), "POSICOES ABERTAS  %d", s.poN);
    g.setTextColor(C_ACC, C_BG);
    g.drawString(title, 4, 1);
    drawBatteryIcon(g, 206, 1, bat, C_BG);
    g.drawFastHLine(0, 11, 240, C_HDR);

    if (s.poN == 0) {
        g.setFont(&fonts::Font2);
        g.setTextColor(C_DIM, C_BG);
        g.drawString("Nenhuma posicao aberta", 4, 40);
        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        char l[64];
        snprintf(l, sizeof(l), "Contas consideradas: %s (%d)", s.acct[0] ? s.acct : "-", s.nAcc);
        g.drawString(l, 4, 70);
        g.drawString("Contas de simulacao ficam de fora.", 4, 82);
        g.drawString("Para incluir, mude no painel web do PC.", 4, 94);
        return;
    }

    g.setTextColor(C_DIM, C_BG);
    g.drawString("CONTRATO", 4, 15);
    g.drawString("MEDIO", 150 - g.textWidth("MEDIO"), 15);
    g.drawString("ABERTO", 236 - g.textWidth("ABERTO"), 15);
    g.drawFastHLine(4, 24, 232, C_HDR);

    for (int i = 0; i < s.poN && i < 8; i++) {
        const PosInfo &p = s.po[i];
        int y = 28 + i * 12;
        bool comp = (p.qty > 0);

        char sq[20];
        snprintf(sq, sizeof(sq), "%s %+d", p.sym, p.qty);
        g.setTextColor(comp ? C_UP : C_DN, C_BG);
        g.drawString(sq, 4, y);

        char ac[14];
        snprintf(ac, sizeof(ac), "%.9s", p.acct);
        g.setTextColor(C_DIM, C_BG);
        g.drawString(ac, 90, y);

        char av[16];
        fmtPrice(av, sizeof(av), p.avg, p.dec);
        g.setTextColor(C_TEXT, C_BG);
        g.drawString(av, 172 - g.textWidth(av), y);

        char up[16];
        fmtMoney(up, sizeof(up), p.upl);
        g.setTextColor(colorFor(p.upl), C_BG);
        g.drawString(up, 236 - g.textWidth(up), y);
    }

    g.setTextColor(C_DIM, C_BG);
    g.drawString("O ordens   X zera tudo", 4, 126);
}

// ===============================================================
//  Tela 7: ordens de trabalho
// ===============================================================
inline void drawOrders(M5Canvas &g, const Snapshot &s, int bat)
{
    g.fillScreen(C_BG);

    g.setFont(&fonts::Font0);
    char title[28];
    snprintf(title, sizeof(title), "ORDENS ABERTAS  %d", s.odN);
    g.setTextColor(C_ACC, C_BG);
    g.drawString(title, 4, 1);
    drawBatteryIcon(g, 206, 1, bat, C_BG);
    g.drawFastHLine(0, 11, 240, C_HDR);

    if (s.odN == 0) {
        g.setFont(&fonts::Font2);
        g.setTextColor(C_DIM, C_BG);
        g.drawString("Nenhuma ordem de trabalho", 4, 50);
        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        char l[64];
        snprintf(l, sizeof(l), "Contas consideradas: %s (%d)", s.acct[0] ? s.acct : "-", s.nAcc);
        g.drawString(l, 4, 76);
        g.drawString("Contas de simulacao ficam de fora.", 4, 88);
        return;
    }

    for (int i = 0; i < s.odN && i < 10; i++) {
        const OrderInfo &o = s.od[i];
        int y = 15 + i * 11;

        bool compra = (o.act[0] == 'B');   // Buy, BuyToCover

        g.setTextColor(C_TEXT, C_BG);
        g.drawString(o.sym, 4, y);

        char sq[16];
        snprintf(sq, sizeof(sq), "%s %d", compra ? "COMP" : "VEND", o.qty);
        g.setTextColor(compra ? C_UP : C_DN, C_BG);
        g.drawString(sq, 54, y);

        char tp[16];
        snprintf(tp, sizeof(tp), "%.7s", o.typ);
        g.setTextColor(C_DIM, C_BG);
        g.drawString(tp, 110, y);

        char ac[12];
        snprintf(ac, sizeof(ac), "%.7s", o.acct);
        g.setTextColor(C_DIM, C_BG);
        g.drawString(ac, 158, y);

        char px[16];
        fmtPrice(px, sizeof(px), o.px, o.dec);
        g.setTextColor(C_TEXT, C_BG);
        g.drawString(px, 236 - g.textWidth(px), y);
    }

    g.setTextColor(C_DIM, C_BG);
    g.drawString("X cancela todas e zera posicoes", 4, 126);
}

// ===============================================================
//  Tela de emergencia
// ===============================================================
inline void drawPanic(M5Canvas &g, int stage, bool ok, const char *msg, int secondsLeft,
                      bool armed = true)
{
    uint16_t frame = (stage == 2) ? (ok ? C_UP : C_DN) : C_DN;

    g.fillScreen(C_BG);
    g.drawRect(0, 0, 240, 135, frame);
    g.drawRect(1, 1, 238, 133, frame);
    g.setFont(&fonts::Font4);

    if (stage == 0) {
        g.setTextColor(C_DN, C_BG);
        const char *t = "ZERAR TUDO?";
        g.drawString(t, (240 - g.textWidth(t)) / 2, 16);

        g.setFont(&fonts::Font0);
        g.setTextColor(C_TEXT, C_BG);
        const char *l1 = "Encerra TODAS as posicoes a mercado,";
        const char *l2 = "cancela ordens e desliga estrategias.";
        g.drawString(l1, (240 - g.textWidth(l1)) / 2, 50);
        g.drawString(l2, (240 - g.textWidth(l2)) / 2, 62);

        g.setFont(&fonts::Font2);
        g.setTextColor(armed ? C_DN : C_DIM, C_BG);
        const char *k = armed ? "ENTER confirma" : "SEM TOKEN - tecla K";
        g.drawString(k, (240 - g.textWidth(k)) / 2, 82);

        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        char c[44];
        snprintf(c, sizeof(c), "qualquer outra tecla cancela  (%ds)", secondsLeft);
        g.drawString(c, (240 - g.textWidth(c)) / 2, 108);
    }
    else if (stage == 1) {
        g.setTextColor(C_ACC, C_BG);
        const char *t = "ENVIANDO...";
        g.drawString(t, (240 - g.textWidth(t)) / 2, 55);
    }
    else {
        g.setTextColor(ok ? C_UP : C_DN, C_BG);
        const char *t = ok ? "EXECUTADO" : "FALHOU";
        g.drawString(t, (240 - g.textWidth(t)) / 2, 30);

        g.setFont(&fonts::Font0);
        g.setTextColor(C_TEXT, C_BG);
        g.drawString(msg, (240 - g.textWidth(msg)) / 2, 66);

        g.setTextColor(C_DIM, C_BG);
        const char *h = "Confira no NinjaTrader antes de confiar.";
        g.drawString(h, (240 - g.textWidth(h)) / 2, 90);
        const char *k = "qualquer tecla para voltar";
        g.drawString(k, (240 - g.textWidth(k)) / 2, 110);
    }
}

// ===============================================================
//  Tela de status
// ===============================================================
inline const char *somNome(int m) { return (m <= 0) ? "mudo" : (m == 1 ? "posicao" : "tudo"); }

inline void drawStatus(M5Canvas &g, const Settings &st, const Snapshot &s, bool wifi, int bat)
{
    g.fillScreen(C_BG);
    g.setFont(&fonts::Font0);
    g.setTextColor(C_ACC2, C_BG);
    g.drawString("CARDPUTER://trader", 4, 1);
    drawSoundIcon(g, 178, 1, st.sound);
    drawWifiBars(g, 192, 1, (int)WiFi.RSSI(), wifi);
    drawBatteryIcon(g, 206, 1, bat, C_BG);
    g.drawFastHLine(0, 11, 240, C_HDR);

    g.setFont(&fonts::Font2);
    g.setTextColor(wifi ? C_UP : C_DN, C_BG);
    g.drawString(wifi ? "Wi-Fi conectado" : "Sem Wi-Fi", 4, 16);

    g.setFont(&fonts::Font0);
    g.setTextColor(C_DIM, C_BG);

    char line[72];
    snprintf(line, sizeof(line), "Rede: %s", st.ssid);                         g.drawString(line, 4, 40);
    snprintf(line, sizeof(line), "IP: %s", wifi ? WiFi.localIP().toString().c_str() : "-");
    g.drawString(line, 4, 52);
    snprintf(line, sizeof(line), "Bridge: %s:%d", st.host, st.port);           g.drawString(line, 4, 64);
    snprintf(line, sizeof(line), "Config: %s   Tema: %s   Som: %s",
             st.configured ? "aparelho" : (st.fromSd ? "SD" : "compilada"), TH.name,
             somNome(st.sound));
    g.drawString(line, 4, 76);
    snprintf(line, sizeof(line), "Conta: %s (%d)   Token: %s",
             s.acct[0] ? s.acct : "-", s.nAcc, st.token[0] ? "armado" : "off");
    g.drawString(line, 4, 88);

    g.setTextColor(s.ok ? C_UP : C_DN, C_BG);
    snprintf(line, sizeof(line), "> %s", s.err);
    g.drawString(line, 4, 104);
    if ((millis() / 500) % 2)
        g.fillRect(4 + g.textWidth(line) + 2, 104, 5, 8, s.ok ? C_UP : C_DN);

    g.setTextColor(C_DIM, C_BG);
    g.drawString("?  ajuda com todos os comandos", 4, 122);
}

// ===============================================================
//  Abertura
// ===============================================================
inline void drawSplash(M5GFX &d)
{
    d.fillScreen(TH.bg);
    d.drawRect(6, 6, 228, 123, TH.hdr);
    d.drawRect(8, 8, 224, 119, TH.accent);

    d.setFont(&fonts::Font4);
    d.setTextColor(TH.text, TH.bg);
    const char *t = "CardputerTrader";
    d.drawString(t, (240 - d.textWidth(t)) / 2, 34);

    d.setFont(&fonts::Font0);
    d.setTextColor(TH.accent, TH.bg);
    const char *s2 = "NinjaTrader ao vivo";
    d.drawString(s2, (240 - d.textWidth(s2)) / 2, 68);

    d.setTextColor(TH.dim, TH.bg);
    const char *c = "por Heluiz";
    d.drawString(c, (240 - d.textWidth(c)) / 2, 88);
}
