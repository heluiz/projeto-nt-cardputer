#pragma once
#include <Arduino.h>
#include "Config.h"

// ---------------------------------------------------------------
//  Estruturas de dados vindas do NinjaTrader
// ---------------------------------------------------------------

struct Quote {
    char  sym[20]  = "";
    char  root[10] = "";
    int   dec      = 2;
    float last = 0, bid = 0, ask = 0;
    float open = 0, high = 0, low = 0, prev = 0;
    float chg = 0, pct = 0;
    int   pos  = 0;          // negativo = vendido
    float avg  = 0, upl = 0;
    int   nord = 0;          // ordens de trabalho neste contrato
    int   tick = 0;          // +1 subiu, -1 caiu
    int   alert = 0;         // 1 = nivel cruzado
    float alvl = 0;
    int   sint = 5;          // segundos entre pontos do grafico
    float spark[MAX_SPARK];
    int   sparkN = 0;
};

struct AccountInfo {
    char  name[24] = "";
    bool  conn = false;
    float realized = 0, unrealized = 0, total = 0;
};

struct StratInfo {
    char name[22]  = "";
    char acct[20]  = "";
    char state[14] = "";
};

struct PosInfo {
    char  sym[10]  = "";
    char  acct[20] = "";
    int   qty      = 0;
    float avg      = 0;
    float upl      = 0;
};

struct OrderInfo {
    char  sym[10]  = "";
    char  acct[20] = "";
    char  act[12] = "";     // Buy, Sell, BuyToCover, SellShort
    char  typ[12] = "";     // Limit, StopMarket, ...
    char  st[12]  = "";     // Working, Accepted
    int   qty     = 0;
    float px      = 0;
};

struct ExecInfo {
    char  t[10]   = "";
    char  sym[10] = "";
    char  side    = '?';
    int   qty     = 0;
    float px      = 0;
};

struct Snapshot {
    bool     ok = false, accOk = false;
    char     acct[24] = "";
    int      nAcc = 0;
    float    realized = 0, unrealized = 0, total = 0;

    Quote       q[MAX_QUOTES];   int n     = 0;
    AccountInfo acc[MAX_ACCOUNTS]; int accN = 0;
    StratInfo   st[MAX_STRATS];  int stN   = 0;
    ExecInfo    ex[MAX_EXECS];   int exN   = 0;
    OrderInfo   od[MAX_ORDERS];  int odN   = 0;
    PosInfo     po[MAX_POS];     int poN   = 0;

    uint32_t lastOkMs = 0;
    char     err[64] = "aguardando";
};

// historico da sessao, buscado sob demanda
struct DayHist {
    char  sym[20] = "";
    int   dec = 2;
    float pts[MAX_DAY];
    int   n = 0;
    bool  loading = false;
};

struct Settings {
    char ssid[40] = CFG_WIFI_SSID;
    char pass[64] = CFG_WIFI_PASS;
    char host[40] = CFG_HOST;
    int  port     = CFG_PORT;
    char token[40] = "";
    int  theme    = 0;
    bool fromSd   = false;
    bool configured = false;
};

// ---------------------------------------------------------------
inline void fmtPrice(char *out, size_t n, float v, int dec)
{
    if (dec < 0) dec = 0;
    if (dec > 6) dec = 6;
    snprintf(out, n, "%.*f", dec, v);
}

inline void fmtSigned(char *out, size_t n, float v, int dec)
{
    if (dec < 0) dec = 0;
    if (dec > 6) dec = 6;
    snprintf(out, n, "%+.*f", dec, v);
}

inline void fmtMoney(char *out, size_t n, float v)
{
    snprintf(out, n, "%+.2f", v);
}

// abrevia valores grandes: 12345 -> 12.3k
inline void fmtMoneyShort(char *out, size_t n, float v)
{
    float a = fabsf(v);
    if (a >= 10000.0f) snprintf(out, n, "%+.1fk", v / 1000.0f);
    else               snprintf(out, n, "%+.0f", v);
}
