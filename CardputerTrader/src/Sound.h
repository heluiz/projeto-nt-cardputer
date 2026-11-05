#pragma once
#include <M5Cardputer.h>
#include "Data.h"

// ===================================================================
//  Alertas sonoros
//
//  Modos (tecla M alterna):
//    0  mudo
//    1  so posicao   (entrada, aumento, reducao, encerramento)
//    2  posicao + alertas de preco
//
//  Os sons sao curtos de proposito: informam sem irritar em mesa.
// ===================================================================

#define SND_NONE     0
#define SND_LONG     1   // abriu comprado
#define SND_SHORT    2   // abriu vendido
#define SND_FLAT     3   // zerou
#define SND_ADD      4   // aumentou
#define SND_REDUCE   5   // reduziu
#define SND_ALERT    6   // preco cruzou o nivel
#define SND_ERROR    7   // perdeu a bridge
#define SND_BACK     8   // bridge voltou

#define SND_VOLUME   180

inline const char *soundModeName(int mode)
{
    if (mode <= 0) return "mudo";
    if (mode == 1) return "posicao";
    return "tudo";
}

inline void soundBegin(int mode)
{
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(mode > 0 ? SND_VOLUME : 0);
}

inline void soundSetMode(int mode)
{
    M5Cardputer.Speaker.setVolume(mode > 0 ? SND_VOLUME : 0);
}

// Toca o evento. Nao bloqueia: as notas entram na fila do alto-falante.
inline void soundPlay(int event, int mode)
{
    if (mode <= 0 || event == SND_NONE) return;
    if (event == SND_ALERT && mode < 2) return;

    switch (event) {
        case SND_LONG:                       // sobe: entrou comprado
            M5Cardputer.Speaker.tone(1046, 70);
            delay(75);
            M5Cardputer.Speaker.tone(1568, 110);
            break;

        case SND_SHORT:                      // desce: entrou vendido
            M5Cardputer.Speaker.tone(784, 70);
            delay(75);
            M5Cardputer.Speaker.tone(523, 110);
            break;

        case SND_FLAT:                       // grave e longo: acabou
            M5Cardputer.Speaker.tone(440, 220);
            break;

        case SND_ADD:                        // clique agudo
            M5Cardputer.Speaker.tone(1318, 60);
            break;

        case SND_REDUCE:                     // clique medio
            M5Cardputer.Speaker.tone(880, 60);
            break;

        case SND_ALERT:                      // tres agudos
            M5Cardputer.Speaker.tone(1760, 55);
            delay(70);
            M5Cardputer.Speaker.tone(1760, 55);
            delay(70);
            M5Cardputer.Speaker.tone(1760, 55);
            break;

        case SND_ERROR:                      // dois graves: perdeu o PC
            M5Cardputer.Speaker.tone(330, 90);
            delay(110);
            M5Cardputer.Speaker.tone(262, 160);
            break;

        case SND_BACK:                       // dois medios subindo: voltou
            M5Cardputer.Speaker.tone(659, 60);
            delay(70);
            M5Cardputer.Speaker.tone(880, 80);
            break;
    }
}

// -------------------------------------------------------------------
//  Deteccao de mudanca de posicao entre duas leituras
// -------------------------------------------------------------------
struct PosSeen {
    char sym[10] = "";
    int  qty = 0;
};

class PositionWatcher {
public:
    // Compara o novo retrato com o anterior e devolve o evento mais
    // relevante. Entrada tem prioridade sobre encerramento, que tem
    // prioridade sobre ajustes de tamanho.
    int diff(const Snapshot &s)
    {
        if (!primed) {           // primeira leitura so memoriza
            store(s);
            primed = true;
            return SND_NONE;
        }

        int evento = SND_NONE;

        for (int i = 0; i < s.poN; i++) {
            const PosInfo &p = s.po[i];
            int antes = qtyOf(p.sym);

            if (antes == 0 && p.qty != 0) {
                evento = (p.qty > 0) ? SND_LONG : SND_SHORT;
                break;                                   // entrada manda
            }
            if (antes != 0 && ((antes > 0) != (p.qty > 0))) {
                evento = (p.qty > 0) ? SND_LONG : SND_SHORT;   // inverteu
                break;
            }
            if (evento == SND_NONE) {
                if (abs(p.qty) > abs(antes)) evento = SND_ADD;
                else if (abs(p.qty) < abs(antes)) evento = SND_REDUCE;
            }
        }

        if (evento == SND_NONE || evento == SND_ADD || evento == SND_REDUCE) {
            for (int i = 0; i < prevN; i++) {
                if (prev[i].qty == 0) continue;
                bool aindaExiste = false;
                for (int j = 0; j < s.poN; j++) {
                    if (strcmp(prev[i].sym, s.po[j].sym) == 0 && s.po[j].qty != 0) {
                        aindaExiste = true;
                        break;
                    }
                }
                if (!aindaExiste) { evento = SND_FLAT; break; }
            }
        }

        store(s);
        return evento;
    }

    // Alerta de preco: dispara so na borda, nao a cada leitura
    bool alertEdge(const Snapshot &s)
    {
        int hits = 0;
        for (int i = 0; i < s.n; i++) if (s.q[i].alert) hits++;
        bool edge = (hits > lastAlerts);
        lastAlerts = hits;
        return edge;
    }

    void reset() { primed = false; prevN = 0; lastAlerts = 0; }

private:
    PosSeen prev[MAX_POS];
    int  prevN = 0;
    bool primed = false;
    int  lastAlerts = 0;

    int qtyOf(const char *sym) const
    {
        for (int i = 0; i < prevN; i++)
            if (strcmp(prev[i].sym, sym) == 0) return prev[i].qty;
        return 0;
    }

    void store(const Snapshot &s)
    {
        prevN = 0;
        for (int i = 0; i < s.poN && prevN < MAX_POS; i++) {
            snprintf(prev[prevN].sym, sizeof(prev[prevN].sym), "%s", s.po[i].sym);
            prev[prevN].qty = s.po[i].qty;
            prevN++;
        }
    }
};
