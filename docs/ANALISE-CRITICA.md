# Análise crítica do projeto

Escrito depois de reler tudo que entreguei. Separei entre o que já corrigi na v2 e o que continua sendo risco.

---

## O que estava errado e foi corrigido

### 1. Contrato hardcoded — era a pior parte
`"NQ 09-26"` fixo no código significava que em setembro o painel simplesmente pararia de mostrar preço, sem erro claro, e você teria que lembrar de editar e recompilar. Isso é o tipo de defeito que só aparece quando você já esqueceu como o projeto funciona.

**Agora:** você informa só a raiz (`NQ`), e o AddOn testa os vencimentos em ordem até achar o vigente, rolando 8 dias antes do vencimento. Um timer confere de hora em hora e reassina sozinho quando vira. Se quiser travar um contrato específico, ainda dá — escreva `NQ 12-26` completo.

### 2. Uma única conta, hardcoded
Além de exigir recompilar, ignorava a realidade de quem opera em mais de uma conta ao mesmo tempo.

**Agora:** lê todas as contas que o NinjaTrader conhece. O padrão soma tudo (posições líquidas e PnL agregado), e o dropdown permite filtrar para uma conta específica. O preço médio no modo agregado é ponderado pela quantidade.

### 3. Qualquer mudança exigia recompilar
Trocar o IP, o símbolo ou a conta significava abrir o VS Code ou o NinjaScript Editor.

**Agora:** painel web em `http://localhost:8088/` com os dropdowns, salvando em `Documentos\NinjaTrader 8\CardputerBridge.cfg`. Vale na hora. Do lado do Cardputer, o `trader.txt` no SD já resolvia o Wi-Fi e o IP.

### 4. Zero visibilidade quando algo dava errado
Se o contrato não resolvesse, você veria uma tela em branco sem saber por quê.

**Agora:** o painel web mostra a tabela do que está realmente assinado, com preço e posição ao vivo. Se a linha não aparecer, o problema é ali, não no Cardputer.

---

## O que continua sendo risco

### 1. Nada disso foi compilado nem executado
É a limitação mais importante e vale dizer com todas as letras: escrevi contra a documentação do NinjaScript e da M5Stack, mas não rodei uma linha. Os pontos de maior incerteza no AddOn são três chamadas de API — `Instrument.MarketData.Update`, `BarsRequest`, e `Instrument.Expiry`. Se alguma tiver assinatura diferente na sua versão do NT8, o erro aparece no F5 e é rápido de corrigir. Me mande o texto do erro.

### 2. Qualquer um na sua rede vê seu PnL
O servidor responde para quem perguntar, sem autenticação por padrão. Numa rede doméstica o risco é baixo; num escritório, coworking ou Wi-Fi compartilhado, não é. Deixei suporte a token — basta pôr `token=algumacoisa` no arquivo `.cfg` e usar `?t=algumacoisa` nas URLs. **Não exponha essa porta à internet.**

### 3. Depende do NinjaTrader aberto
Você escolheu essa arquitetura de propósito, e ela é a certa para dados em tempo real. Mas o custo é que o Cardputer vira um enfeite quando o PC está desligado. Sem fallback para API pública, como combinamos.

### 4. O histórico do gráfico é volátil
Os 120 pontos do sparkline vivem na memória do NinjaTrader. Reiniciar o NT zera o gráfico e ele leva 10 minutos para reencher. Persistir em CSV resolveria, mas achei que não valia a complexidade para um gráfico de 10 minutos.

### 5. Polling burro
O Cardputer abre uma conexão HTTP nova a cada segundo. Funciona bem numa LAN, mas é desperdício de bateria e de handshake. WebSocket ou uma conexão persistente com long-polling seria mais elegante. Com 1750 mAh e Wi-Fi ligado direto, espere algo em torno de **10 a 13 horas** de autonomia — a tecla `B` desligando a tela ajuda pouco, porque quem come bateria é o rádio, não o LCD.

### 6. Semântica do PnL realizado
`RealizedProfitLoss` significa coisas ligeiramente diferentes entre conta simulada e conta de corretora — algumas resetam na virada da sessão, outras no fechamento do broker. Confira o número contra o Control Center no primeiro dia antes de confiar nele.

### 7. O firmware não trata bem "muitos símbolos"
Se você colocar oito contratos, o JSON passa de 10 KB e o parsing no ESP32 fica pesado. Na prática, dois ou três é o uso natural. Acima de quatro, eu reduziria o `SPARK_MAX`.

---

## Adicionado depois desta análise

- **Temas alternáveis** pela tecla `T`, salvos na memória
- **Tela de boot em terminal**, que virou a principal ferramenta de diagnóstico
- **Comando de emergência** com token, confirmação em duas teclas e expiração
- **Economia de bateria** com escurecimento progressivo e consulta mais lenta com a tela apagada
- **Tela de contas individuais** na tecla `A`

Riscos novos que isso introduziu:

- O comando de emergência é a primeira coisa no projeto que **movimenta dinheiro**. As três travas ajudam, mas o risco real é você apertar `X` e `ENTER` no bolso. Se isso preocupar, remova o token do Cardputer — a leitura continua funcionando e o comando fica desarmado.
- `Account.Strategies` e `SetState(State.Terminated)` são as chamadas de que menos tenho certeza no projeto inteiro. Se o F5 reclamar, é aí.
- Com a tela apagada o dado fica até 4 segundos velho. A bolinha de status vai piscar vermelha nesse modo — é esperado, não é falha.

## Resolvido na v3

- **Rede fora do loop principal.** A consulta agora roda numa tarefa no núcleo 0. O teclado responde na hora mesmo com o NinjaTrader fechado.
- **Backoff progressivo.** Falhando, o intervalo dobra até 30 s. Depois de 4 falhas seguidas ele refaz a descoberta por broadcast, resolvendo troca de IP pelo DHCP sozinho.
- **Dado velho se anuncia.** Números ficam cinza e o rodapé mostra a idade em segundos.
- **Requisições históricas** caíram de uma a cada 5 minutos para uma por hora.
- **O contrato selecionado é rastreado pelo nome**, não pelo índice.
- **`String` fora do campo de texto**, que agora usa buffer fixo.

## O que eu faria depois, em ordem de valor

1. **Ver funcionando.** Nada na lista abaixo importa antes disso.
2. **Alerta visual de nível de preço** — você dispensou alertas sonoros, mas uma borda piscando quando o preço cruza um valor definido no painel web seria útil e barato de implementar.
3. **Tela de contas** — uma quarta tela no Cardputer listando cada conta com seu PnL separado, já que o dado agora existe no JSON.
4. **Persistir o histórico** em CSV, permitindo um gráfico de dia inteiro em vez de 10 minutos.
5. **WebSocket** no lugar do polling, se a bateria incomodar.
