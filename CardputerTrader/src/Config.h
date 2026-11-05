#pragma once

// ===================================================================
//  Valores padrao. Podem ser sobrescritos pelo assistente no aparelho
//  (tecla C) ou por um /trader.txt no cartao SD.
//  Ordem de prioridade: memoria interna -> cartao SD -> este arquivo.
// ===================================================================

#define CFG_WIFI_SSID   "MinhaRede"
#define CFG_WIFI_PASS   "MinhaSenha"
#define CFG_HOST        "192.168.0.15"
#define CFG_PORT        8088

#define CFG_POLL_MS         1000    // consulta normal
#define CFG_POLL_SLOW_MS    4000    // com a tela apagada
#define CFG_POLL_MAX_MS     30000   // teto do backoff quando a bridge nao responde
#define CFG_HTTP_TIMEOUT_MS 2500

// Quanto tempo sem resposta antes de avisar que perdeu o PC,
// e de quanto em quanto tempo repetir o aviso enquanto durar.
#define LINK_LOST_MS    30000
#define LINK_REBEEP_MS  300000

#define MAX_QUOTES      10
#define MAX_ACCOUNTS    24
#define MAX_SPARK       120
#define MAX_STRATS      16
#define MAX_EXECS       10
#define MAX_ORDERS      20
#define MAX_POS         20
#define MAX_DAY         160

#define CFG_SD_FILE     "/trader.txt"
