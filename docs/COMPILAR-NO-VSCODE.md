# Compilar o CardputerTrader no VS Code

Tempo: ~20 min na primeira vez (o grosso é download). Depois, ~30 segundos por build.

---

## 1. Instalar a extensão PlatformIO

1. Abra o VS Code
2. `Ctrl+Shift+X` (ícone de quatro quadrados na barra lateral)
3. Busque **PlatformIO IDE** e instale o primeiro resultado (autor: PlatformIO)
4. Aparece "Installing PlatformIO Core (this may take a few minutes)" — deixe rodar, baixa ~200 MB
5. Quando pedir, clique **Reload Window**

Você vai saber que deu certo quando aparecer o ícone de **formiga** (alien) na barra lateral e uma fileira de ícones na barra azul de baixo: 🏠 ✓ → 🔌 🗑

---

## 2. Abrir a pasta certa

`Ctrl+K` depois `Ctrl+O`, e selecione a pasta **`CardputerTrader`**.

> Tem que ser a pasta que contém o `platformio.ini` direto dentro dela. Se você abrir a pasta de cima (a que tem `CardputerTrader` e `NinjaTraderBridge` lado a lado), o PlatformIO não reconhece o projeto.

Se perguntar sobre confiar nos autores: **Yes, I trust the authors**.

Espere o canto inferior direito parar de processar (o PIO está indexando).

---

## 3. Configurar Wi-Fi e IP antes de compilar

Abra `src/Config.h` e ajuste as quatro linhas:

```cpp
#define CFG_WIFI_SSID   "NomeDaSuaRede"
#define CFG_WIFI_PASS   "SuaSenha"
#define CFG_HOST        "192.168.0.15"   // IP do PC com o NinjaTrader
#define CFG_PORT        8088
```

Para descobrir o IP do PC: tecle `Win+R`, digite `cmd`, Enter, e rode `ipconfig`. Use o **Endereço IPv4** do adaptador da sua rede (Wi-Fi ou Ethernet).

> Atalho útil: se preferir não recompilar toda vez que o IP mudar, deixe o `Config.h` como está e crie um `trader.txt` na raiz do cartão SD (modelo em `trader.txt.exemplo`). O arquivo do SD tem prioridade.

---

## 4. Compilar

Clique no **✓** na barra azul de baixo (é o Build). Ou `Ctrl+Alt+B`.

Um terminal abre embaixo. **Não feche.** Na primeira vez ele baixa o toolchain do ESP32 (~500 MB) — pode levar 10-15 min. Você vai ver:

```
Processing cardputer-adv (platform: espressif32@6.7.0; board: esp32-s3-devkitc-1)
Platform Manager: Installing espressif32 @ 6.7.0
Tool Manager: Installing platformio/toolchain-xtensa-esp32s3
Library Manager: Installing M5Cardputer
```

No fim:

```
RAM:   [==        ]  17.2% (used 56000 bytes from 327680 bytes)
Flash: [=====     ]  48.1% (used 1580000 bytes from 3276800 bytes)
========================= [SUCCESS] Took 142.31 seconds =========================
```

---

## 5. Pegar o `.bin`

O arquivo fica em:

```
CardputerTrader\.pio\build\cardputer-adv\firmware.bin
```

> A pasta `.pio` é oculta. No Explorer: **Exibir → Mostrar → Itens ocultos**.

Renomeie para `CardputerTrader.bin`, copie para a raiz do cartão SD e abra pelo launcher do Cardputer.

---

## 6. Alternativa: gravar direto pelo USB (mais rápido)

Em vez do cartão SD, dá para gravar pelo cabo:

1. Coloque a chave lateral do Cardputer em **OFF**
2. Segure o botão **G0** (o botãozinho ao lado da porta USB)
3. Ligue o cabo USB no PC, **ainda segurando o G0**
4. Solte o G0 — ele entrou em modo de gravação
5. No VS Code, clique na **→** (seta) na barra de baixo — é o Upload

Depois da gravação, desconecte, ponha a chave em **ON** e ele inicia sozinho.

---

## Erros comuns

| Erro no terminal | Causa e solução |
|---|---|
| `Error: Unknown board ID` | Você abriu a pasta errada, ou o `platformio.ini` não foi lido. Confira o passo 2. |
| `HTTPClientError` / `Connection timed out` | Antivírus ou firewall bloqueando o Python do PlatformIO. Autorize e rode o Build de novo. |
| `fatal error: M5Cardputer.h: No such file` | O download da lib falhou. Rode `pio pkg install` no terminal do PIO, ou apague a pasta `.pio` e compile de novo. |
| `A fatal error occurred: Failed to connect to ESP32-S3` (no Upload) | Não entrou em modo de gravação. Refaça o passo 6 segurando o G0 desde antes de plugar. |
| Compila mas a tela fica preta | Chave lateral em OFF, ou bateria descarregada. |

Se aparecer qualquer texto vermelho que não esteja nessa tabela, copie o bloco inteiro e me mande.

---

## Antes de tudo funcionar: o lado do NinjaTrader

O firmware só mostra dados se o AddOn estiver rodando. Falta ajustar **o nome da sua conta** no `CardputerBridge.cs`:

```cpp
private const string ACCOUNT_NAME = "Sim101";
```

Onde encontrar o nome exato: no **Control Center** do NinjaTrader, aba **Accounts**. Em conta de mesa/prop firm normalmente é o número da conta (algo como `APEX-12345-01` ou `PA-1234567`).

> Sugestão: compile e teste primeiro com `Sim101`. Se o painel acender com os preços, o caminho todo está funcionando e aí você troca só o nome da conta e recompila (F5). Isolar um problema por vez economiza tempo.
