# Revisão geral — o que foi encontrado e corrigido

Revisão feita lendo o código, não de memória. Três defeitos reais, duas melhorias de robustez, e uma lista do que continua em aberto.

---

## Corrigido

### 1. Risco de não compilar na leitura de posições
O código chamava `Instrument.MarketData.Last.Price` como plano B para calcular o resultado de contratos fora da lista monitorada. Essa cadeia de propriedades é a menos documentada de tudo que uso do NinjaScript — e um erro ali não daria um aviso, quebraria a compilação inteira.

Removi o plano B. Agora o preço vem só dos contratos assinados; um contrato fora da lista aparece com resultado zerado em vez de número inventado.

### 2. A bridge varria tudo duas vezes por segundo
O JSON era remontado a cada 500 ms, e nele eu percorria posições, ordens, execuções e estratégias de **todas** as contas. Com 24 contas e um dia inteiro de execuções acumuladas, isso é trabalho pesado dentro do processo do NinjaTrader, 172 mil vezes por dia.

Pior: para o contador de ordens de cada contrato, eu varria a lista de ordens **uma vez por contrato**. Sete contratos, 24 contas, duas vezes por segundo.

Agora essa parte é remontada a cada 2 segundos e reaproveitada, e o contador de ordens usa um índice montado de uma vez só. As cotações continuam a 500 ms, que é o que precisa ser rápido.

### 3. O último contrato não aparecia na faixa superior
A faixa escrevia o nome de todos os contratos lado a lado. Com os seus sete, `BTC` ficava 9 pixels além do limite e sumia sem aviso.

Redesenhei: agora mostra o **nome completo do contrato ativo** e um marcador por contrato. Cabem dez sem cortar nenhum, e o marcador ainda mostra a cor da variação e pisca no alerta.

---

## Melhorias

### Casas decimais por instrumento
As telas de posições, ordens e execuções mostravam tudo com duas casas. Correto para índices, errado para câmbio, onde o `6E` precisa de cinco. Agora cada linha usa a precisão do próprio contrato.

### A grade avisa quando não cabe
Com mais de oito contratos, o título passa a mostrar `CONTRATOS 8 de 10` em vez de simplesmente esconder o resto.

---

## Verificações que passaram

- Balanceamento de chaves, parênteses e colchetes em todos os arquivos, ignorando textos e comentários
- Todo campo lido pelo firmware é enviado pela bridge
- Assinaturas das funções de desenho batem com as chamadas
- Nenhuma tecla duplicada
- Nenhuma função definida duas vezes ou usada antes de existir

---

## Continua em aberto

**Nada foi compilado.** Continua sendo o maior risco. As chamadas de que menos tenho certeza, em ordem: `Account.Strategies`, `Account.Executions`, `Instrument.Expiry`.

**Corridas de dados menores.** O teclado lê `snap.n` sem travar o mutex enquanto a tarefa de rede pode estar copiando. São inteiros — o pior caso é um quadro desenhado com número antigo, não travamento.

**O som bloqueia por até 185 ms.** As pausas entre as notas rodam no laço principal. Se você apertar uma tecla exatamente durante um alerta, ela pode se perder.

**A configuração é lida uma vez.** Editar o `.cfg` na mão exige reiniciar o NinjaTrader. Pelo painel web vale na hora.
