# CardputerTrader — painel NQ/ES no M5Cardputer ADV

Dois componentes: um **AddOn no NinjaTrader** (PC) que expõe os dados na rede local, e o **firmware do Cardputer** que os exibe.

```
NinjaTrader 8 (PC)                    Cardputer ADV
 CardputerBridge.cs  ──HTTP/JSON──>   CardputerTrader
 porta 8088                            polling 1x/s
```

---

## Parte 1 — Instalar o AddOn no NinjaTrader

1. Copie `NinjaTraderBridge/CardputerBridge.cs` para:
   `Documentos\NinjaTrader 8\bin\Custom\AddOns\`
2. No NinjaTrader: **New → NinjaScript Editor → F5** para compilar. Não precisa editar nada antes.
3. Abra **http://localhost:8088/** no navegador do PC — é o painel de configuração:
   - **Conta**: dropdown com todas as suas contas. O padrão soma todas.
   - **Contratos**: escreva as raízes separadas por vírgula (`NQ,ES`). O vencimento vigente é escolhido sozinho e rola 8 dias antes do vencimento.
   - A tabela embaixo mostra o que está realmente assinado, com preço ao vivo. Se estiver vazia, o problema é aí.
4. Na aba **Output** (New → Output) deve aparecer:
   ```
   [CardputerBridge] Assinado: NQ -> NQ 09-26
   [CardputerBridge] No ar. Configuracao: http://localhost:8088/
   ```
5. Teste no navegador do próprio PC: `http://localhost:8088/quote` — tem que devolver JSON com preços.

### Liberar a porta no Firewall do Windows

Sem isso o Cardputer não alcança o PC. PowerShell **como administrador**:

```powershell
New-NetFirewallRule -DisplayName "CardputerBridge" -Direction Inbound -Protocol TCP -LocalPort 8088 -Action Allow -Profile Private
New-NetFirewallRule -DisplayName "CardputerBridge Discovery" -Direction Inbound -Protocol UDP -LocalPort 8089 -Action Allow -Profile Private
```

A segunda regra é a que permite o Cardputer achar o PC sozinho. Sem ela, o assistente cai na digitação manual do IP.

### Descobrir o IP do PC

Só é necessário se a descoberta automática falhar:

```
ipconfig
```
Anote o "Endereço IPv4" do adaptador da sua rede (algo como `192.168.0.15`).

---

## Parte 2 — Firmware do Cardputer

### Configuração — feita no próprio aparelho

Não precisa definir nada antes de compilar. Na primeira vez que ligar, o Cardputer abre um assistente de 3 passos:

1. **Rede** — lista as redes Wi-Fi encontradas, com intensidade de sinal. Setas `;` e `.` navegam, `ENTER` escolhe, `TAB` procura de novo, `M` digita o nome na mão.
2. **Senha** — digitada no teclado, mascarada. `ENTER` confirma, `FN+\`` volta.
3. **Servidor** — ele manda um broadcast na rede e o NinjaTrader responde. Se achar, pronto. Se não achar, você digita o IP.

Tudo fica salvo na memória interna. Para reconfigurar depois, tecla `C` a qualquer momento.

**Alternativas**, se preferir: um `trader.txt` na raiz do cartão SD (modelo em `CardputerTrader/trader.txt.exemplo`) ou os valores em `src/Config.h`. A ordem de prioridade é: memória interna → cartão SD → `Config.h`.

### Compilar (VS Code + PlatformIO)

1. Abra a pasta `CardputerTrader` no VS Code
2. Clique no **✓** da barra inferior (Build)
3. O binário sai em `.pio\build\cardputer-adv\firmware.bin`
4. Renomeie para `CardputerTrader.bin`, copie para a raiz do SD e abra pelo launcher

Alternativa sem instalar nada: suba a pasta num repo do GitHub — o workflow em `.github/workflows/build.yml` compila sozinho e o `.bin` sai em **Actions → Artifacts**.

---

## Teclas

| Tecla | Ação |
|---|---|
| `W` | grade com todos os contratos |
| `G` | gráfico em tela cheia |
| `D` | no gráfico, alterna 10 min ↔ sessão inteira |
| `P` | posições abertas, de todas as contas e contratos |
| `O` | ordens de trabalho |
| `A` | contas, uma a uma — `;` sobe e `.` desce |
| `E` | estratégias rodando |
| `H` | últimas 10 execuções |
| `S` | status e diagnóstico |
| `TAB` | troca de contrato |
| `T` | troca o tema (neon → âmbar → matrix) |
| `C` | refaz a configuração de rede |
| `K` | define o token de emergência |
| `B` | liga/desliga a tela |
| `X` | **zerar tudo** — confirma com `ENTER` |
| `ENTER` | atualiza agora |

Qualquer tecla volta ao painel se você já estiver na tela dela — não precisa decorar caminho de volta.

## Contas de simulação

Por padrão são **ignoradas**: contas cujo nome começa com `Sim`, `Playback`, `Replay` ou `Backtest` não entram em nenhum cálculo, nem aparecem na tela de contas. Isso evita que o resultado de simulação se misture ao dinheiro real.

Para mudar, use o painel web: o campo *Contas de simulação* alterna entre ignorar e incluir, e o campo ao lado aceita nomes exatos de outras contas que você queira fora.

## Posições e ordens não aparecem?

A causa mais comum é o filtro de contas. Abra o painel web: a seção **Contas consideradas** lista cada conta com quantas posições e ordens ela tem, e logo abaixo mostra quais contas estão sendo ignoradas.

Duas exclusões valem por padrão: **contas de simulação** (`Sim`, `Playback`, `Replay`, `Backtest`) e **contas desconectadas**, que mostram saldo congelado. Cada uma tem seu dropdown no painel web, e a lista de ignoradas mostra o motivo de cada exclusão.

A tela `P` do Cardputer mostra posições de **qualquer** contrato, mesmo os que não estão na sua lista de monitorados — então uma posição em MES aparece ali mesmo que você só acompanhe NQ.

## Alertas de preço

No painel web, campo *Alertas de preço*, formato `NQ>23200,ES<5800`. Quando o preço cruza o nível, a borda da tela do Cardputer pisca e o contrato ganha uma marca na faixa superior. Sem som, como combinado.

## O que aparece no painel

- Preço grande, colorido por direção do dia
- Variação absoluta e percentual (base: fechamento anterior)
- Mini gráfico dos últimos ~10 min, com linha tracejada no fechamento anterior
- Posição aberta (COMP/VEND, quantidade, preço médio) e PnL não realizado
- Bid e ask, seta indicando a direção do último negócio
- Abertura / máxima / mínima do dia e resultado total das contas
- Faixa superior com todos os contratos, coloridos pela variação do dia

Quando a conexão cai, os números ficam cinza e o rodapé passa a mostrar há quantos segundos o dado está parado — número velho não se disfarça de número novo.

Telas separadas mostram cada conta individualmente, as estratégias rodando com seu estado, e as 10 últimas execuções com horário, contrato, lado, quantidade e preço.

O gráfico tem grade de tempo a cada minuto e, na tecla `D`, mostra a sessão inteira em vez dos últimos 10 minutos. Esse histórico é gravado em `Documentos\NinjaTrader 8\CardputerHistory\` e sobrevive a reinício do NinjaTrader.

## Comando de emergência

A tecla `X` encerra **todas as posições a mercado**, cancela ordens de trabalho e desliga estratégias, nas contas do filtro atual.

Três travas protegem contra acionamento acidental:

1. **Token obrigatório.** Sem token configurado nos dois lados, o comando é recusado.

   **No PC:** painel web da bridge → campo *Token dos comandos de emergência* → **Salvar**. Fica gravado em `Documentos\NinjaTrader 8\CardputerBridge.cfg`.

   **No Cardputer:** tecla `K`, digite o mesmo valor, `ENTER`.

   Use **só letras e números**, sem espaços nem acentos — o valor viaja na URL. Algo como `zerar2026` já resolve. Não é senha de banco: ele só impede acionamento acidental e curioso na rede local.

   Para conferir, a tela de status (`S`) mostra `Token: armado` ou `Token: off`.

   > Com token definido, ele passa a ser exigido também na leitura das cotações — por isso os dois lados precisam bater. O painel web aberto **do próprio PC** (`localhost`) nunca pede token, justamente para você não se trancar para fora. De outra máquina, use `http://IP-DO-PC:8088/?t=SEU_TOKEN`.
2. **Confirmação em duas teclas.** `X` só arma; `ENTER` executa. Qualquer outra tecla cancela.
3. **Expira em 8 segundos** se você não confirmar.

> Ordem a mercado executa ao preço disponível, que pode ser bem pior que o da tela em momento de estresse. E o resultado mostrado no Cardputer é o que a bridge relatou — **sempre confira no Control Center** antes de considerar encerrado.

Sobre estratégias: desligar funciona. **Religar não é oferecido** — a estratégia precisaria ser reinstanciada com os parâmetros originais, e errar isso silenciosamente seria pior que não ter o recurso. Reative pelo NinjaTrader.

## Tela de inicialização

Ao ligar, ele mostra cada etapa da partida com `[ OK ]`, `[FALHA]` ou `[ ?? ]`: configuração, Wi-Fi, endereço IP, bridge, contratos, contas e token. Quando algo não sobe, a linha em vermelho diz exatamente onde parou, em vez de deixar você adivinhando na tela vazia.

## Economia de bateria

- 1 minuto sem tecla → tela escurece
- 5 minutos → tela apaga e a consulta cai para 1 a cada 4 segundos
- Qualquer tecla acorda, sem executar nada (a primeira tecla só desperta)
- Abaixo de 15%, o indicador de bateria fica vermelho

## Se não funcionar

| Sintoma na tela `S` | Causa provável |
|---|---|
| `sem wifi` | SSID/senha errados, ou rede 5GHz (o ESP32 só pega 2.4GHz) |
| `HTTP -1` | firewall bloqueando, IP do PC errado, ou NinjaTrader fechado |
| `token nao confere` | o token do Cardputer (`K`) é diferente do salvo no painel web |
| `HTTP 200` mas preços zerados | contrato com código do mês vencido, ou sem feed de dados conectado |
| `DIA` cinza | nenhuma conta selecionada — confira o dropdown no painel web |

## Limites internos

O firmware guarda no máximo 24 contas, 10 contratos, 20 posições, 20 ordens, 16 estratégias e as 10 últimas execuções. Acima disso os itens excedentes não aparecem na tela — mas a **soma do dia continua correta**, porque ela é calculada no PC sobre todas as contas, não sobre a lista enviada.

Se precisar de mais, os valores estão em `src/Config.h`. Cuidado: com 7 contratos e 24 contas o JSON já fica em torno de 12 KB por consulta, e dobrar isso começa a pesar no ESP32.

## Limitações conhecidas

- Só funciona com o NinjaTrader **aberto e conectado** ao feed. Não há fallback para API pública.
- O histórico do gráfico vive na memória do PC — reiniciar o NinjaTrader zera o sparkline.
- Sem alertas sonoros, conforme combinado.
- A leitura de cotações e PnL continua sem autenticação: quem estiver na sua rede consegue ver. O token protege apenas os comandos de emergência. Para proteger a leitura também, use `token=` — ele passa a ser exigido em todas as rotas.
- Nunca exponha essa porta à internet.
- Análise completa de riscos e limitações em `ANALISE-CRITICA.md`.
