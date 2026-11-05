#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>
#include "Config.h"
#include "Data.h"

// Pinos do microSD no Cardputer ADV (doc oficial M5Stack)
#define SD_SCK   40
#define SD_MISO  39
#define SD_MOSI  14
#define SD_CS    12

#define DISCOVERY_PORT   8089
#define DISCOVERY_LOCAL  8090

// ---------------------------------------------------------------
//  /trader.txt no cartao SD
// ---------------------------------------------------------------
inline bool loadSettingsFromSd(Settings &s)
{
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 25000000)) return false;

    File f = SD.open(CFG_SD_FILE, FILE_READ);
    if (!f) { SD.end(); return false; }

    bool got = false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line.startsWith("#")) continue;

        int eq = line.indexOf('=');
        if (eq <= 0) continue;

        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        key.trim(); val.trim();
        key.toLowerCase();

        if      (key == "ssid")  { strncpy(s.ssid, val.c_str(), sizeof(s.ssid) - 1); got = true; }
        else if (key == "pass")  { strncpy(s.pass, val.c_str(), sizeof(s.pass) - 1); got = true; }
        else if (key == "host")  { strncpy(s.host, val.c_str(), sizeof(s.host) - 1); got = true; }
        else if (key == "token") { strncpy(s.token, val.c_str(), sizeof(s.token) - 1); got = true; }
        else if (key == "port")  { int p = val.toInt(); if (p > 0 && p < 65536) { s.port = p; got = true; } }
    }
    f.close();
    SD.end();

    s.fromSd = got;
    return got;
}

// ---------------------------------------------------------------
//  Wi-Fi
// ---------------------------------------------------------------
inline void wifiStart(const Settings &s)
{
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(s.ssid, s.pass);
}

inline bool wifiOk() { return WiFi.status() == WL_CONNECTED; }

// ---------------------------------------------------------------
//  Descobre o PC na rede por broadcast UDP
// ---------------------------------------------------------------
inline bool discoverHost(Settings &s, uint32_t timeoutMs = 2500)
{
    if (!wifiOk()) return false;

    WiFiUDP udp;
    if (!udp.begin(DISCOVERY_LOCAL)) return false;

    IPAddress bcast = WiFi.localIP();
    bcast[3] = 255;

    for (int attempt = 0; attempt < 3; attempt++) {
        udp.beginPacket(bcast, DISCOVERY_PORT);
        udp.print("CARDPUTER?");
        udp.endPacket();

        uint32_t start = millis();
        while (millis() - start < timeoutMs / 3) {
            int sz = udp.parsePacket();
            if (sz > 0) {
                char buf[64] = {0};
                int len = udp.read(buf, sizeof(buf) - 1);
                if (len > 0) buf[len] = 0;

                if (strncmp(buf, "CARDPUTER:", 10) == 0) {
                    IPAddress from = udp.remoteIP();
                    snprintf(s.host, sizeof(s.host), "%s", from.toString().c_str());
                    int p = atoi(buf + 10);
                    if (p > 0 && p < 65536) s.port = p;
                    udp.stop();
                    return true;
                }
            }
            delay(20);
        }
    }
    udp.stop();
    return false;
}

// ---------------------------------------------------------------
//  Comando para a bridge (ex: /panic)
// ---------------------------------------------------------------
inline bool sendCommand(const Settings &s, const char *route, char *outMsg, size_t outLen)
{
    if (!wifiOk()) { snprintf(outMsg, outLen, "sem wifi"); return false; }
    if (s.token[0] == 0) { snprintf(outMsg, outLen, "sem token configurado"); return false; }

    char url[180];
    snprintf(url, sizeof(url), "http://%s:%d%s?t=%s", s.host, s.port, route, s.token);

    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(5000);
    http.setReuse(false);
    if (!http.begin(url)) { snprintf(outMsg, outLen, "url invalida"); return false; }

    int code = http.GET();
    String body = (code > 0) ? http.getString() : String("");
    http.end();

    if (code != 200) {
        snprintf(outMsg, outLen, "HTTP %d %s", code, body.substring(0, 24).c_str());
        return false;
    }
    snprintf(outMsg, outLen, "%s", body.substring(0, 40).c_str());
    return true;
}

// ---------------------------------------------------------------
//  Historico da sessao de um contrato
// ---------------------------------------------------------------
inline bool fetchHistory(const Settings &s, const char *root, DayHist &h)
{
    if (!wifiOk()) return false;

    char url[200];
    if (s.token[0])
        snprintf(url, sizeof(url), "http://%s:%d/hist?sym=%s&t=%s", s.host, s.port, root, s.token);
    else
        snprintf(url, sizeof(url), "http://%s:%d/hist?sym=%s", s.host, s.port, root);

    HTTPClient http;
    http.setConnectTimeout(CFG_HTTP_TIMEOUT_MS);
    http.setTimeout(4000);
    http.setReuse(false);
    if (!http.begin(url)) return false;

    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, http.getStream());
    http.end();
    if (e || !doc["ok"].as<bool>()) return false;

    snprintf(h.sym, sizeof(h.sym), "%s", doc["sym"] | root);
    h.dec = doc["dec"] | 2;
    h.n = 0;
    for (JsonVariant v : doc["pts"].as<JsonArray>()) {
        if (h.n >= MAX_DAY) break;
        h.pts[h.n++] = v.as<float>();
    }
    return h.n > 1;
}

// ---------------------------------------------------------------
//  Consulta principal
// ---------------------------------------------------------------
inline bool fetchSnapshot(const Settings &s, Snapshot &snap)
{
    if (!wifiOk()) { snprintf(snap.err, sizeof(snap.err), "sem wifi"); return false; }

    char url[180];
    if (s.token[0])
        snprintf(url, sizeof(url), "http://%s:%d/quote?t=%s", s.host, s.port, s.token);
    else
        snprintf(url, sizeof(url), "http://%s:%d/quote", s.host, s.port);

    HTTPClient http;
    http.setConnectTimeout(CFG_HTTP_TIMEOUT_MS);
    http.setTimeout(CFG_HTTP_TIMEOUT_MS);
    http.setReuse(false);

    if (!http.begin(url)) { snprintf(snap.err, sizeof(snap.err), "url invalida"); return false; }

    int code = http.GET();
    if (code != 200) {
        if (code == 401) snprintf(snap.err, sizeof(snap.err), "token nao confere (K)");
        else             snprintf(snap.err, sizeof(snap.err), "HTTP %d", code);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, http.getStream());
    http.end();

    if (e) { snprintf(snap.err, sizeof(snap.err), "json: %s", e.c_str()); return false; }

    if (!doc["ok"].as<bool>()) {
        const char *m = doc["msg"] | "bridge com erro";
        snprintf(snap.err, sizeof(snap.err), "%s", m);
        return false;
    }

    snap.ok         = true;
    snap.accOk      = doc["accOk"] | false;
    snap.realized   = doc["realized"] | 0.0f;
    snap.unrealized = doc["unrealized"] | 0.0f;
    snap.total      = doc["total"] | 0.0f;
    snap.nAcc       = doc["nAcc"] | 0;
    snprintf(snap.acct, sizeof(snap.acct), "%s", doc["mode"] | "");

    // ---- contas ----
    snap.accN = 0;
    for (JsonObject a : doc["accounts"].as<JsonArray>()) {
        if (snap.accN >= MAX_ACCOUNTS) break;
        AccountInfo &ai = snap.acc[snap.accN];
        snprintf(ai.name, sizeof(ai.name), "%s", a["name"] | "?");
        ai.conn       = a["conn"] | false;
        ai.realized   = a["realized"] | 0.0f;
        ai.unrealized = a["unrealized"] | 0.0f;
        ai.total      = a["total"] | 0.0f;
        snap.accN++;
    }

    // ---- cotacoes ----
    int i = 0;
    for (JsonObject o : doc["quotes"].as<JsonArray>()) {
        if (i >= MAX_QUOTES) break;
        Quote &q = snap.q[i];

        snprintf(q.sym,  sizeof(q.sym),  "%s", o["sym"]  | "?");
        snprintf(q.root, sizeof(q.root), "%s", o["root"] | "?");
        q.dec   = o["dec"]   | 2;
        q.last  = o["last"]  | 0.0f;
        q.bid   = o["bid"]   | 0.0f;
        q.ask   = o["ask"]   | 0.0f;
        q.open  = o["open"]  | 0.0f;
        q.high  = o["high"]  | 0.0f;
        q.low   = o["low"]   | 0.0f;
        q.prev  = o["prev"]  | 0.0f;
        q.chg   = o["chg"]   | 0.0f;
        q.pct   = o["pct"]   | 0.0f;
        q.pos   = o["pos"]   | 0;
        q.avg   = o["avg"]   | 0.0f;
        q.upl   = o["upl"]   | 0.0f;
        q.nord  = o["nord"]  | 0;
        q.tick  = o["tick"]  | 0;
        q.alert = o["alert"] | 0;
        q.alvl  = o["alvl"]  | 0.0f;
        q.sint  = o["sint"]  | 5;

        q.sparkN = 0;
        for (JsonVariant v : o["spark"].as<JsonArray>()) {
            if (q.sparkN >= MAX_SPARK) break;
            q.spark[q.sparkN++] = v.as<float>();
        }
        i++;
    }
    snap.n = i;

    // ---- estrategias ----
    snap.stN = 0;
    for (JsonObject t : doc["strats"].as<JsonArray>()) {
        if (snap.stN >= MAX_STRATS) break;
        StratInfo &si = snap.st[snap.stN];
        snprintf(si.name,  sizeof(si.name),  "%s", t["name"]  | "?");
        snprintf(si.acct,  sizeof(si.acct),  "%s", t["acct"]  | "");
        snprintf(si.state, sizeof(si.state), "%s", t["state"] | "");
        snap.stN++;
    }

    // ---- posicoes abertas (todos os contratos) ----
    snap.poN = 0;
    for (JsonObject d : doc["positions"].as<JsonArray>()) {
        if (snap.poN >= MAX_POS) break;
        PosInfo &pi = snap.po[snap.poN];
        snprintf(pi.sym,  sizeof(pi.sym),  "%s", d["sym"]  | "?");
        snprintf(pi.acct, sizeof(pi.acct), "%s", d["acct"] | "");
        pi.dec = d["dec"] | 2;
        pi.qty = d["qty"] | 0;
        pi.avg = d["avg"] | 0.0f;
        pi.upl = d["upl"] | 0.0f;
        snap.poN++;
    }

    // ---- ordens abertas ----
    snap.odN = 0;
    for (JsonObject d : doc["orders"].as<JsonArray>()) {
        if (snap.odN >= MAX_ORDERS) break;
        OrderInfo &oi = snap.od[snap.odN];
        snprintf(oi.sym,  sizeof(oi.sym),  "%s", d["sym"]  | "?");
        snprintf(oi.acct, sizeof(oi.acct), "%s", d["acct"] | "");
        snprintf(oi.act, sizeof(oi.act), "%s", d["act"] | "");
        snprintf(oi.typ, sizeof(oi.typ), "%s", d["typ"] | "");
        snprintf(oi.st,  sizeof(oi.st),  "%s", d["st"]  | "");
        oi.dec = d["dec"] | 2;
        oi.qty = d["qty"] | 0;
        oi.px  = d["px"]  | 0.0f;
        snap.odN++;
    }

    // ---- execucoes ----
    snap.exN = 0;
    for (JsonObject x : doc["execs"].as<JsonArray>()) {
        if (snap.exN >= MAX_EXECS) break;
        ExecInfo &ei = snap.ex[snap.exN];
        snprintf(ei.t,   sizeof(ei.t),   "%s", x["t"]   | "");
        snprintf(ei.sym, sizeof(ei.sym), "%s", x["sym"] | "");
        const char *sd = x["side"] | "?";
        ei.side = sd[0];
        ei.dec = x["dec"] | 2;
        ei.qty = x["qty"] | 0;
        ei.px  = x["px"]  | 0.0f;
        snap.exN++;
    }

    snap.lastOkMs = millis();
    snprintf(snap.err, sizeof(snap.err), "ok");
    return true;
}
