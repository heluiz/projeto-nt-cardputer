// ===================================================================
//  CardputerTrader v3
//  Painel de cotacoes para M5Cardputer ADV, alimentado pelo AddOn
//  CardputerBridge rodando no NinjaTrader 8.
//
//  Telas e teclas:
//    W      grade com todos os contratos
//    G      grafico em tela cheia   (D alterna 10 min / sessao)
//    A      contas, uma a uma       (; e . rolam)
//    P      posicoes abertas (todas as contas e contratos)
//    O      ordens de trabalho
//    E      estrategias rodando
//    H      ultimas 10 execucoes
//    S      status e diagnostico
//    ?      ajuda com todos os comandos
//    TAB    troca de contrato
//    T      troca o tema
//    C      refaz a configuracao de rede
//    K      define o token de emergencia
//    M      som: mudo / posicao / tudo
//    B      liga/desliga a tela
//    X      ZERAR TUDO (confirma com ENTER)
//    ENTER  atualiza agora
//
//  A rede roda no segundo nucleo: o teclado nunca trava esperando
//  resposta do PC.
//
//  por Heluiz
// ===================================================================

#include <M5Cardputer.h>
#include "Config.h"
#include "Theme.h"
#include "Data.h"
#include "Net.h"
#include "Ui.h"
#include "Boot.h"
#include "Setup.h"
#include "Sound.h"

// --- economia de bateria ---
#define IDLE_DIM_MS   60000UL
#define IDLE_OFF_MS   300000UL
#define BRIGHT_FULL   100
#define BRIGHT_DIM    20

// --- telas ---
#define PG_DASH   0
#define PG_CHART  1
#define PG_STATUS 2
#define PG_ACCTS  3
#define PG_GRID   4
#define PG_STRATS 5
#define PG_EXECS  6
#define PG_ORDERS 7
#define PG_POS    8
#define PG_HELP   9

static M5Canvas   canvas(&M5Cardputer.Display);
static Settings   settings;
static Snapshot   snap;        // lido pela interface
static Snapshot   snapStage;   // escrito pela tarefa de rede
static DayHist    dayHist;
static BootScreen boot;

static SemaphoreHandle_t dataMux = nullptr;
static TaskHandle_t      netTaskHandle = nullptr;

static int      symIdx    = 0;
static char     symWanted[20] = "";   // acompanha o contrato mesmo se a lista mudar
static int      page      = PG_STATUS;
static int      accScroll = 0;
static int      helpPage  = 0;
static PositionWatcher posWatch;
static volatile int soundQueue = SND_NONE;
static bool     linkLost   = false;
static uint32_t lastOkAt   = 0;
static uint32_t lastLostBeep = 0;
static bool     dayMode   = false;
static bool     screenOn  = true;
static bool     canvasOk  = false;

static uint32_t lastDraw  = 0;
static uint32_t lastKeyAt = 0;
static int      dimLevel  = 0;

static volatile bool reqNow     = false;
static volatile bool reqHist    = false;
static volatile bool netRunning = false;

// --- panico ---
static bool     panicMode = false;
static int      panicStage = 0;
static bool     panicOk = false;
static char     panicMsg[48] = "";
static uint32_t panicArmedAt = 0;
static volatile bool reqPanic = false;
#define PANIC_TIMEOUT_MS 8000UL

// -------------------------------------------------------------------
static void setupCanvas()
{
    canvas.setColorDepth(8);
    if (canvas.createSprite(240, 135)) { canvasOk = true; return; }
    canvas.setColorDepth(4);
    if (canvas.createSprite(240, 135)) { canvasOk = true; return; }
    canvasOk = false;
}

static void setBrightnessLevel(int level)
{
    dimLevel = level;
    if (!screenOn) { M5Cardputer.Display.setBrightness(0); return; }
    if (level == 0)      M5Cardputer.Display.setBrightness(BRIGHT_FULL);
    else if (level == 1) M5Cardputer.Display.setBrightness(BRIGHT_DIM);
    else                 M5Cardputer.Display.setBrightness(0);
}

// mantem o contrato escolhido mesmo se a lista mudar de ordem
static void syncSymbol()
{
    if (snap.n == 0) { symIdx = 0; return; }
    if (symWanted[0]) {
        for (int i = 0; i < snap.n; i++) {
            if (strcmp(snap.q[i].root, symWanted) == 0) { symIdx = i; return; }
        }
    }
    if (symIdx >= snap.n) symIdx = 0;
    snprintf(symWanted, sizeof(symWanted), "%s", snap.q[symIdx].root);
}

// -------------------------------------------------------------------
//  Tarefa de rede, no nucleo 0
// -------------------------------------------------------------------
static void netTask(void *arg)
{
    uint32_t interval = CFG_POLL_MS;
    int fails = 0;

    netRunning = true;
    for (;;) {
        if (reqPanic) {
            reqPanic = false;
            char msg[48];
            bool ok = sendCommand(settings, "/panic", msg, sizeof(msg));
            if (xSemaphoreTake(dataMux, portMAX_DELAY) == pdTRUE) {
                panicOk = ok;
                snprintf(panicMsg, sizeof(panicMsg), "%s", msg);
                panicStage = 2;
                xSemaphoreGive(dataMux);
            }
        }

        if (reqHist) {
            reqHist = false;
            char root[20];
            if (xSemaphoreTake(dataMux, portMAX_DELAY) == pdTRUE) {
                snprintf(root, sizeof(root), "%s",
                         (snap.n > 0) ? snap.q[symIdx].root : "");
                xSemaphoreGive(dataMux);
            }
            DayHist tmp;
            bool ok = (root[0] != 0) && fetchHistory(settings, root, tmp);
            if (xSemaphoreTake(dataMux, portMAX_DELAY) == pdTRUE) {
                if (ok) { memcpy(&dayHist, &tmp, sizeof(DayHist)); }
                dayHist.loading = false;
                xSemaphoreGive(dataMux);
            }
        }

        bool got = fetchSnapshot(settings, snapStage);
        if (!got) snapStage.ok = false;

        if (got) {
            int ev = posWatch.diff(snapStage);
            if (ev == SND_NONE && posWatch.alertEdge(snapStage)) ev = SND_ALERT;
            if (ev != SND_NONE) soundQueue = ev;
        }

        if (xSemaphoreTake(dataMux, portMAX_DELAY) == pdTRUE) {
            memcpy(&snap, &snapStage, sizeof(Snapshot));
            xSemaphoreGive(dataMux);
        }

        // backoff progressivo, para nao martelar a rede com a bridge fora
        // --- aviso sonoro de elo perdido e recuperado ---
        uint32_t agora = millis();
        if (got) {
            lastOkAt = agora;
            if (linkLost) {
                linkLost = false;
                soundQueue = SND_BACK;
            }
        } else {
            if (!linkLost && agora - lastOkAt > LINK_LOST_MS) {
                linkLost = true;
                lastLostBeep = agora;
                soundQueue = SND_ERROR;
            } else if (linkLost && agora - lastLostBeep > LINK_REBEEP_MS) {
                lastLostBeep = agora;
                soundQueue = SND_ERROR;
            }
        }

        if (got) {
            fails = 0;
            bool telaApagada = (dimLevel == 2 || !screenOn);

            // Com o som ligado, a tela apagada NAO afrouxa o ritmo: o alerta
            // de entrada precisa ser rapido, e quem gasta bateria e o radio
            // Wi-Fi ficar associado, nao a frequencia da consulta.
            if (telaApagada && settings.sound == 0) interval = CFG_POLL_SLOW_MS;
            else                                    interval = CFG_POLL_MS;
        } else {
            fails++;
            interval = CFG_POLL_MS << (fails > 5 ? 5 : fails);
            if (interval > CFG_POLL_MAX_MS) interval = CFG_POLL_MAX_MS;

            // depois de algumas falhas seguidas, procura o PC de novo:
            // o IP pode ter mudado pelo DHCP
            if (fails == 4 || fails == 12) {
                Settings probe = settings;
                if (discoverHost(probe, 1500)) {
                    snprintf(settings.host, sizeof(settings.host), "%s", probe.host);
                    settings.port = probe.port;
                    saveSettingsToNvs(settings);
                    fails = 0;
                    interval = CFG_POLL_MS;
                }
            }
        }

        // espera fatiada, para responder rapido a um pedido manual
        uint32_t waited = 0;
        while (waited < interval && !reqNow && !reqHist && !reqPanic) {
            vTaskDelay(pdMS_TO_TICKS(50));
            waited += 50;
        }
        reqNow = false;
    }
}

// -------------------------------------------------------------------
static void render()
{
    if (!canvasOk) return;
    if (!screenOn || dimLevel == 2) return;
    if (xSemaphoreTake(dataMux, pdMS_TO_TICKS(60)) != pdTRUE) return;

    int  bat    = (int)M5Cardputer.Power.getBatteryLevel();
    uint32_t age = millis() - snap.lastOkMs;
    bool connOk = snap.ok && (age < 5000);
    bool wifi   = wifiOk();

    if (panicMode) {
        int left = (int)((PANIC_TIMEOUT_MS - (millis() - panicArmedAt)) / 1000);
        if (left < 0) left = 0;
        drawPanic(canvas, panicStage, panicOk, panicMsg, left, settings.token[0] != 0);
    } else {
        syncSymbol();

        switch (page) {
            case PG_GRID:   drawGrid(canvas, snap, symIdx, connOk, bat, settings.sound); break;
            case PG_ACCTS:  drawAccounts(canvas, snap, accScroll, bat); break;
            case PG_STRATS: drawStrats(canvas, snap, bat); break;
            case PG_EXECS:  drawExecs(canvas, snap, bat); break;
            case PG_ORDERS: drawOrders(canvas, snap, bat); break;
            case PG_POS:    drawPositions(canvas, snap, bat); break;
            case PG_HELP:   drawHelp(canvas, helpPage, bat); break;
            case PG_STATUS: drawStatus(canvas, settings, snap, wifi, bat); break;
            case PG_CHART:
                if (snap.n == 0) drawStatus(canvas, settings, snap, wifi, bat);
                else drawChart(canvas, snap, symIdx, dayHist, dayMode, connOk, bat, settings.sound);
                break;
            default:
                if (snap.n == 0) drawStatus(canvas, settings, snap, wifi, bat);
                else drawDashboard(canvas, snap, symIdx, connOk, bat, age, settings.sound);
                break;
        }
    }

    xSemaphoreGive(dataMux);
    canvas.pushSprite(0, 0);
}

// transicao curta entre telas
static void wipe()
{
    if (!canvasOk || !screenOn || dimLevel != 0) return;
    for (int x = 0; x < 240; x += 24) {
        M5Cardputer.Display.fillRect(x, 0, 24, 135, TH.hdr);
        delay(3);
    }
}

static void goTo(int p)
{
    if (page == p) p = PG_DASH;
    wipe();
    page = p;
    render();
}

static void toggleScreen()
{
    screenOn = !screenOn;
    if (screenOn) { setBrightnessLevel(0); render(); }
    else {
        M5Cardputer.Display.fillScreen(TH.bg);
        M5Cardputer.Display.setBrightness(0);
    }
}

// -------------------------------------------------------------------
static void handleKeys()
{
    if (!M5Cardputer.Keyboard.isChange()) return;
    if (!M5Cardputer.Keyboard.isPressed()) return;

    Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
    lastKeyAt = millis();

    if (dimLevel != 0) { setBrightnessLevel(0); render(); return; }

    // --- confirmacao do panico ---
    if (panicMode) {
        if (panicStage == 2) { panicMode = false; render(); return; }
        if (panicStage == 1) return;
        if (st.enter) {
            if (settings.token[0] == 0) {
                panicOk = false;
                snprintf(panicMsg, sizeof(panicMsg), "aperte K e use o token do PC");
                panicStage = 2;
                render();
                return;
            }
            panicStage = 1;
            reqPanic = true;
            render();
            return;
        }
        panicMode = false;
        render();
        return;
    }

    if (st.tab) {
        if (snap.n > 0) {
            symIdx = (symIdx + 1) % snap.n;
            snprintf(symWanted, sizeof(symWanted), "%s", snap.q[symIdx].root);
            if (page == PG_CHART && dayMode) { dayHist.loading = true; reqHist = true; }
        }
        render();
        return;
    }

    if (st.enter) { reqNow = true; return; }

    if (page == PG_ACCTS && (st.up || st.down)) {
        if (st.down && accScroll + 6 < snap.accN) accScroll++;
        if (st.up && accScroll > 0) accScroll--;
        render();
        return;
    }

    if (page == PG_HELP && (st.up || st.down)) {
        if (st.down) helpPage = (helpPage + 1) % HELP_PAGES;
        if (st.up)   helpPage = (helpPage + HELP_PAGES - 1) % HELP_PAGES;
        render();
        return;
    }

    for (size_t i = 0; i < st.word.size(); i++) {
        char c = st.word[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';

        switch (c) {
            case 'g': goTo(PG_CHART); break;
            case 'w': goTo(PG_GRID); break;
            case 'a': accScroll = 0; goTo(PG_ACCTS); break;
            case 'e': goTo(PG_STRATS); break;
            case 'o': goTo(PG_ORDERS); break;
            case 'p': goTo(PG_POS); break;

            case '/':
            case '?':
                helpPage = 0;
                goTo(PG_HELP);
                break;
            case 'h': goTo(PG_EXECS); break;
            case 's': goTo(PG_STATUS); break;
            case 'b': toggleScreen(); break;

            case 'd':
                if (page == PG_CHART) {
                    dayMode = !dayMode;
                    if (dayMode) { dayHist.loading = true; reqHist = true; }
                    render();
                }
                break;

            case 't':
                settings.theme = nextTheme();
                saveThemeToNvs(settings.theme);
                render();
                break;

            case 'm':
                settings.sound = (settings.sound + 1) % 3;
                saveSoundToNvs(settings.sound);
                soundSetMode(settings.sound);
                if (settings.sound > 0) soundPlay(SND_ADD, settings.sound);
                render();
                break;

            case 'x':
                panicMode = true;
                panicStage = 0;
                panicArmedAt = millis();
                panicMsg[0] = 0;
                render();
                break;

            case 'k':
                editToken(canvas, settings);
                setBrightnessLevel(0);
                render();
                break;

            case 'c':
                if (runSetupWizard(canvas, settings)) {
                    reqNow = true;
                    page = PG_STATUS;
                }
                setBrightnessLevel(0);
                render();
                break;

            case '.':
                if (page == PG_ACCTS && accScroll + 6 < snap.accN) { accScroll++; render(); }
                else if (page == PG_HELP) { helpPage = (helpPage + 1) % HELP_PAGES; render(); }
                break;
            case ';':
                if (page == PG_ACCTS && accScroll > 0) { accScroll--; render(); }
                else if (page == PG_HELP) { helpPage = (helpPage + HELP_PAGES - 1) % HELP_PAGES; render(); }
                break;
        }
    }
}

// -------------------------------------------------------------------
void setup()
{
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(BRIGHT_FULL);
    M5Cardputer.Display.setTextWrap(false);

    dataMux = xSemaphoreCreateMutex();

    setupCanvas();
    if (!canvasOk) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        M5Cardputer.Display.setFont(&fonts::Font2);
        M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5Cardputer.Display.drawString("Sem memoria p/ buffer", 6, 40);
        while (true) delay(1000);
    }

    boot.begin(&canvas);
    char det[24];

    // ---- 1. configuracao ----
    boot.step("config", "...", 0);
    if (loadSettingsFromNvs(settings)) {
        applyTheme(settings.theme);
        boot.begin(&canvas);
        boot.step("config", "memoria interna", 1);
    } else if (loadSettingsFromSd(settings)) {
        boot.update("cartao SD", 1);
    } else {
        boot.update("nao configurado", 3);
        delay(900);
        runSetupWizard(canvas, settings);
        applyTheme(settings.theme);
        boot.begin(&canvas);
        boot.step("config", "assistente", 1);
    }

    // ---- 2. wi-fi ----
    boot.step("wifi", "conectando", 0);
    wifiStart(settings);
    uint32_t t0 = millis();
    while (!wifiOk() && millis() - t0 < 15000) delay(150);

    if (wifiOk()) {
        snprintf(det, sizeof(det), "%ddBm", (int)WiFi.RSSI());
        boot.update(det, 1);
        boot.step("endereco", WiFi.localIP().toString().c_str(), 1);
    } else {
        boot.update("sem conexao", 2);
        boot.step("endereco", "-", 2);
    }

    // ---- 3. bridge ----
    snprintf(det, sizeof(det), "%s:%d", settings.host, settings.port);
    boot.step("bridge", det, 0);

    bool got = false;
    for (int i = 0; i < 3 && wifiOk() && !got; i++) {
        got = fetchSnapshot(settings, snap);
        if (!got) delay(400);
    }

    // se nao respondeu, tenta achar o PC de novo antes de desistir
    if (!got && wifiOk()) {
        boot.update("procurando o PC", 3);
        if (discoverHost(settings, 2000)) {
            saveSettingsToNvs(settings);
            got = fetchSnapshot(settings, snap);
        }
    }
    boot.update(got ? settings.host : snap.err, got ? 1 : 2);

    // ---- 4. dados ----
    if (got) {
        snprintf(det, sizeof(det), "%d contratos", snap.n);
        boot.step("feeds", det, snap.n > 0 ? 1 : 3);
        snprintf(det, sizeof(det), "%d contas", snap.accN);
        boot.step("contas", det, snap.accN > 0 ? 1 : 3);
        snprintf(det, sizeof(det), "%d rodando", snap.stN);
        boot.step("estrategias", det, 1);
        snprintf(det, sizeof(det), "%d pos / %d ord", snap.poN, snap.odN);
        boot.step("carteira", det, 1);
    }

    soundBegin(settings.sound);
    boot.step("som", soundModeName(settings.sound), settings.sound > 0 ? 1 : 3);

    boot.step("token", settings.token[0] ? "armado" : "desativado",
              settings.token[0] ? 1 : 3);

    boot.finish(got ? "ready" : "modo diagnostico");
    delay(got ? 1300 : 2400);

    memcpy(&snapStage, &snap, sizeof(Snapshot));
    lastOkAt = millis();

    // ---- tarefa de rede no nucleo 0 ----
    xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, &netTaskHandle, 0);

    page = got ? PG_DASH : PG_STATUS;
    if (snap.n > 0) snprintf(symWanted, sizeof(symWanted), "%s", snap.q[0].root);
    lastKeyAt = millis();
    setBrightnessLevel(0);
    render();
}

// -------------------------------------------------------------------
void loop()
{
    M5Cardputer.update();
    handleKeys();

    if (soundQueue != SND_NONE) {
        int ev = soundQueue;
        soundQueue = SND_NONE;
        soundPlay(ev, settings.sound);
    }

    uint32_t now = millis();

    if (panicMode && panicStage == 0 && now - panicArmedAt > PANIC_TIMEOUT_MS) {
        panicMode = false;
        render();
    }

    if (!panicMode && screenOn) {
        uint32_t idle = now - lastKeyAt;
        if (idle > IDLE_OFF_MS && dimLevel != 2)      setBrightnessLevel(2);
        else if (idle > IDLE_DIM_MS && dimLevel == 0) setBrightnessLevel(1);
    }

    if (now - lastDraw >= 200) {
        lastDraw = now;
        render();
    }

    delay(5);
}
