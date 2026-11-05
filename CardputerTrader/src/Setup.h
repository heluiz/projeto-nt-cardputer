#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <Preferences.h>
#include "Config.h"
#include "Data.h"
#include "Net.h"
#include "Ui.h"

// ===================================================================
//  Assistente de configuracao no proprio Cardputer.
//  Nada precisa ser definido antes de compilar.
//
//  Prioridade das configuracoes:
//    1. Memoria interna (o que voce configurou no aparelho)
//    2. /trader.txt no cartao SD
//    3. Valores do Config.h
// ===================================================================

static Preferences prefs;

// -------------------------------------------------------------------
//  Memoria interna
// -------------------------------------------------------------------
inline bool loadSettingsFromNvs(Settings &s)
{
    if (!prefs.begin("cardtrader", true)) return false;
    bool done = prefs.getBool("done", false);
    if (done) {
        prefs.getString("ssid", s.ssid, sizeof(s.ssid));
        prefs.getString("pass", s.pass, sizeof(s.pass));
        prefs.getString("host", s.host, sizeof(s.host));
        prefs.getString("token", s.token, sizeof(s.token));
        s.port = prefs.getInt("port", CFG_PORT);
        s.theme = prefs.getInt("theme", 0);
        s.sound = prefs.getInt("sound", 1);
        s.configured = true;
    }
    prefs.end();
    return done;
}

inline void saveSettingsToNvs(const Settings &s)
{
    if (!prefs.begin("cardtrader", false)) return;
    prefs.putString("ssid", s.ssid);
    prefs.putString("pass", s.pass);
    prefs.putString("host", s.host);
    prefs.putString("token", s.token);
    prefs.putInt("port", s.port);
    prefs.putInt("theme", s.theme);
    prefs.putInt("sound", s.sound);
    prefs.putBool("done", true);
    prefs.end();
}

inline void saveThemeToNvs(int theme)
{
    if (!prefs.begin("cardtrader", false)) return;
    prefs.putInt("theme", theme);
    prefs.end();
}

inline void clearSettingsNvs()
{
    if (!prefs.begin("cardtrader", false)) return;
    prefs.clear();
    prefs.end();
}

// -------------------------------------------------------------------
//  Moldura comum das telas do assistente
// -------------------------------------------------------------------
inline void setupFrame(M5Canvas &g, const char *title, const char *hint)
{
    g.fillScreen(C_BG);
    g.fillRect(0, 0, 240, 13, C_HDR);
    g.setFont(&fonts::Font0);
    g.setTextColor(C_ACC, C_HDR);
    g.drawString(title, 4, 3);

    g.setTextColor(C_DIM, C_BG);
    g.drawString(hint, 4, 124);
}

// -------------------------------------------------------------------
//  Campo de texto usando o teclado
//  Retorna true se confirmou com ENTER, false se cancelou com ESC.
// -------------------------------------------------------------------
inline bool textInput(M5Canvas &g, const char *title, const char *label,
                      char *buf, size_t maxLen, bool mask)
{
    static char txt[80];
    snprintf(txt, sizeof(txt), "%s", buf);
    size_t len = strlen(txt);
    if (maxLen > sizeof(txt)) maxLen = sizeof(txt);

    static char shown[84];
    uint32_t blink = millis();
    bool cursorOn = true;

    while (true) {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

            if (st.enter) { snprintf(buf, maxLen, "%s", txt); return true; }
            if (st.esc) return false;

            if ((st.del || st.backspace) && len > 0) { len--; txt[len] = 0; }

            for (size_t i = 0; i < st.word.size(); i++) {
                if (len + 1 < maxLen) { txt[len++] = st.word[i]; txt[len] = 0; }
            }
            cursorOn = true;
            blink = millis();
        }

        if (millis() - blink > 450) { cursorOn = !cursorOn; blink = millis(); }

        setupFrame(g, title, "ENTER confirma   FN+` cancela");

        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        g.drawString(label, 4, 26);

        size_t k = 0;
        for (; k < len && k < sizeof(shown) - 2; k++) shown[k] = mask ? '*' : txt[k];
        shown[k] = cursorOn ? '_' : ' ';
        shown[k + 1] = 0;

        g.drawRoundRect(3, 40, 234, 26, 4, C_HDR);
        g.setFont(&fonts::Font2);
        g.setTextColor(C_TEXT, C_BG);

        const char *view = shown;
        while (g.textWidth(view) > 222 && *view) view++;
        g.drawString(view, 8, 45);

        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        char cnt[24];
        snprintf(cnt, sizeof(cnt), "%d caracteres", (int)len);
        g.drawString(cnt, 4, 74);

        g.pushSprite(0, 0);
        delay(10);
    }
}

inline void saveSoundToNvs(int mode)
{
    if (!prefs.begin("cardtrader", false)) return;
    prefs.putInt("sound", mode);
    prefs.end();
}

inline void saveTokenToNvs(const char *token)
{
    if (!prefs.begin("cardtrader", false)) return;
    prefs.putString("token", token);
    prefs.end();
}

// Edita so o token, sem refazer a configuracao de rede.
inline bool editToken(M5Canvas &g, Settings &s)
{
    setupFrame(g, "Token", "ENTER confirma   FN+` cancela");
    g.setFont(&fonts::Font0);
    g.setTextColor(C_DIM, C_BG);
    g.drawString("Precisa ser igual ao token do painel web", 4, 22);
    g.drawString("da bridge, no PC. So letras e numeros.", 4, 34);
    g.drawString("Em branco desarma o comando de zerar.", 4, 46);
    g.pushSprite(0, 0);
    delay(1600);

    char tmp[40];
    snprintf(tmp, sizeof(tmp), "%s", s.token);

    if (!textInput(g, "Token", "Token de emergencia", tmp, sizeof(tmp), false))
        return false;

    snprintf(s.token, sizeof(s.token), "%s", tmp);
    saveTokenToNvs(s.token);

    setupFrame(g, "Token", "qualquer tecla para voltar");
    g.setFont(&fonts::Font2);
    g.setTextColor(s.token[0] ? C_UP : C_DIM, C_BG);
    g.drawString(s.token[0] ? "Token armado" : "Comando desarmado", 4, 45);
    g.pushSprite(0, 0);

    while (!(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())) {
        M5Cardputer.update();
        delay(20);
    }
    return true;
}

// -------------------------------------------------------------------
//  Lista de redes Wi-Fi
// -------------------------------------------------------------------
inline bool pickNetwork(M5Canvas &g, Settings &s)
{
    int sel = 0, top = 0, n = 0;
    bool scanning = true;

    while (true) {
        if (scanning) {
            setupFrame(g, "Configuracao 1/3", "procurando redes...");
            g.setFont(&fonts::Font2);
            g.setTextColor(C_TEXT, C_BG);
            g.drawString("Procurando redes Wi-Fi", 4, 55);
            g.pushSprite(0, 0);

            WiFi.mode(WIFI_STA);
            WiFi.disconnect();
            delay(80);
            n = WiFi.scanNetworks();
            if (n < 0) n = 0;
            sel = 0; top = 0;
            scanning = false;
        }

        setupFrame(g, "Configuracao 1/3", "ENTER escolhe  TAB rescan  M manual");

        if (n == 0) {
            g.setFont(&fonts::Font2);
            g.setTextColor(C_DN, C_BG);
            g.drawString("Nenhuma rede encontrada", 4, 50);
        } else {
            g.setFont(&fonts::Font0);
            for (int i = 0; i < 7 && (top + i) < n; i++) {
                int idx = top + i;
                int y = 18 + i * 15;
                bool cur = (idx == sel);

                if (cur) g.fillRect(2, y - 2, 236, 14, C_HDR);

                g.setTextColor(cur ? C_ACC : C_TEXT, cur ? C_HDR : C_BG);
                String nm = WiFi.SSID(idx);
                if (nm.length() > 26) nm = nm.substring(0, 26);
                g.drawString(nm.c_str(), 7, y + 2);

                int rssi = WiFi.RSSI(idx);
                char sig[16];
                snprintf(sig, sizeof(sig), "%d dBm", rssi);
                g.setTextColor(rssi > -67 ? C_UP : (rssi > -80 ? C_DIM : C_DN), cur ? C_HDR : C_BG);
                g.drawString(sig, 235 - g.textWidth(sig), y + 2);
            }
        }

        g.pushSprite(0, 0);

        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

            if (st.tab) { scanning = true; continue; }
            if (st.enter && n > 0) {
                snprintf(s.ssid, sizeof(s.ssid), "%s", WiFi.SSID(sel).c_str());
                return true;
            }

            bool up = st.up, down = st.down;
            for (size_t i = 0; i < st.word.size(); i++) {
                char c = st.word[i];
                if (c == ';') up = true;
                else if (c == '.') down = true;
                else if (c == 'm' || c == 'M') {
                    s.ssid[0] = 0;
                    if (textInput(g, "Configuracao 1/3", "Nome da rede (SSID)",
                                  s.ssid, sizeof(s.ssid), false)) return true;
                    scanning = true;
                }
            }

            if (up && sel > 0) { sel--; if (sel < top) top = sel; }
            if (down && sel < n - 1) { sel++; if (sel >= top + 7) top = sel - 6; }
        }
        delay(10);
    }
}

// -------------------------------------------------------------------
//  Conecta e mostra progresso
// -------------------------------------------------------------------
inline bool connectWithFeedback(M5Canvas &g, const Settings &s, uint32_t timeoutMs = 20000)
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.ssid, s.pass);

    uint32_t start = millis();
    int dots = 0;

    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) return false;

        setupFrame(g, "Configuracao 2/3", "aguarde");
        g.setFont(&fonts::Font2);
        g.setTextColor(C_TEXT, C_BG);
        g.drawString("Conectando", 4, 40);

        g.setFont(&fonts::Font0);
        g.setTextColor(C_ACC, C_BG);
        g.drawString(s.ssid, 4, 66);

        int w = (dots % 40) * 6;
        g.drawFastHLine(4, 90, 232, C_HDR);
        g.drawFastHLine(4, 90, w > 232 ? 232 : w, C_ACC);

        g.pushSprite(0, 0);
        dots++;
        delay(250);

        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            if (M5Cardputer.Keyboard.keysState().esc) return false;
        }
    }
    return true;
}

// -------------------------------------------------------------------
//  Assistente completo
// -------------------------------------------------------------------
inline bool runSetupWizard(M5Canvas &g, Settings &s)
{
    while (true) {
        // --- 1. rede ---
        if (!pickNetwork(g, s)) return false;

        // --- 2. senha ---
        s.pass[0] = 0;
        if (!textInput(g, "Configuracao 2/3", "Senha da rede", s.pass, sizeof(s.pass), true))
            continue;   // volta para a lista de redes

        if (!connectWithFeedback(g, s)) {
            setupFrame(g, "Configuracao 2/3", "qualquer tecla para tentar de novo");
            g.setFont(&fonts::Font2);
            g.setTextColor(C_DN, C_BG);
            g.drawString("Nao conectou", 4, 40);
            g.setFont(&fonts::Font0);
            g.setTextColor(C_DIM, C_BG);
            g.drawString("Senha errada, sinal fraco, ou rede 5GHz.", 4, 66);
            g.drawString("O ESP32 so enxerga redes de 2.4GHz.", 4, 78);
            g.pushSprite(0, 0);

            while (!(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())) {
                M5Cardputer.update();
                delay(20);
            }
            continue;
        }

        // --- 3. servidor ---
        setupFrame(g, "Configuracao 3/3", "procurando o NinjaTrader...");
        g.setFont(&fonts::Font2);
        g.setTextColor(C_TEXT, C_BG);
        g.drawString("Procurando o PC", 4, 40);
        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        g.drawString("O NinjaTrader precisa estar aberto.", 4, 68);
        g.pushSprite(0, 0);

        bool found = discoverHost(s);

        if (!found) {
            setupFrame(g, "Configuracao 3/3", "ENTER confirma");
            g.setFont(&fonts::Font0);
            g.setTextColor(C_DN, C_BG);
            g.drawString("Nao achei sozinho. Digite o IP do PC.", 4, 22);
            g.pushSprite(0, 0);
            delay(1400);

            if (!textInput(g, "Configuracao 3/3", "IP do PC (ipconfig)", s.host, sizeof(s.host), false))
                continue;

            char portTxt[10];
            snprintf(portTxt, sizeof(portTxt), "%d", s.port);
            if (textInput(g, "Configuracao 3/3", "Porta", portTxt, sizeof(portTxt), false)) {
                int p = atoi(portTxt);
                if (p > 0 && p < 65536) s.port = p;
            }
        }

        // --- 4. token (opcional, exigido para o botao de panico) ---
        setupFrame(g, "Configuracao 4/4", "ENTER confirma   FN+` pula");
        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        g.drawString("O token protege o comando de zerar tudo.", 4, 22);
        g.drawString("Deixe em branco para desativar o recurso.", 4, 34);
        g.drawString("Use o mesmo valor do arquivo .cfg do PC.", 4, 46);
        g.pushSprite(0, 0);
        delay(1800);

        textInput(g, "Configuracao 4/4", "Token (opcional)", s.token, sizeof(s.token), false);

        // --- confirmacao ---
        s.configured = true;
        saveSettingsToNvs(s);

        setupFrame(g, "Pronto", "qualquer tecla para comecar");
        g.setFont(&fonts::Font2);
        g.setTextColor(C_UP, C_BG);
        g.drawString(found ? "PC encontrado sozinho" : "Configuracao salva", 4, 24);

        g.setFont(&fonts::Font0);
        g.setTextColor(C_DIM, C_BG);
        char l[64];
        snprintf(l, sizeof(l), "Rede: %s", s.ssid);            g.drawString(l, 4, 54);
        snprintf(l, sizeof(l), "IP do Cardputer: %s", WiFi.localIP().toString().c_str());
        g.drawString(l, 4, 68);
        snprintf(l, sizeof(l), "Bridge: %s:%d", s.host, s.port); g.drawString(l, 4, 82);
        g.drawString("Para reconfigurar depois, tecla C.", 4, 100);
        g.pushSprite(0, 0);

        while (!(M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())) {
            M5Cardputer.update();
            delay(20);
        }
        return true;
    }
}
