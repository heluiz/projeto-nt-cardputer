# CardputerTrader — guia rápido

## Antes de ligar

O NinjaTrader precisa estar **aberto e conectado ao feed**. Sem ele, o Cardputer não tem de onde tirar preço.

## Ao ligar

A tela de boot mostra cada etapa. Tudo `[ OK ]` até `> ready` e o painel abre sozinho. Qualquer `[FALHA]` em vermelho diz onde parou — nesse caso ele abre direto no status em vez do painel.

## Telas

| Tecla | Tela |
|---|---|
| `W` | grade com todos os contratos de uma vez |
| `P` | posições abertas, de qualquer contrato |
| `O` | ordens de trabalho |
| `A` | contas, uma a uma |
| `E` | estratégias rodando |
| `H` | últimas 10 execuções |
| `G` | gráfico em tela cheia |
| `S` | status e diagnóstico |

Apertar a mesma tecla de novo volta ao painel. Não existe caminho de volta para decorar.

## Dentro das telas

| Tecla | Ação |
|---|---|
| `TAB` | troca de contrato |
| `D` | no gráfico, alterna 10 min ↔ sessão inteira |
| `;` e `.` | rolam listas longas (sobe e desce, sem precisar do FN) |
| `ENTER` | atualiza agora |

## Ajustes

| Tecla | Ação |
|---|---|
| `T` | troca o tema: neon → âmbar → matrix |
| `M` | som: mudo → posição → tudo |
| `B` | apaga a tela sem parar nada |
| `C` | refaz a configuração de rede |
| `K` | define o token de emergência |

## Emergência

`X` arma, `ENTER` executa. Encerra todas as posições a mercado, cancela ordens e desliga estratégias, nas contas do filtro atual.

Expira em 8 segundos e qualquer outra tecla cancela. Exige token igual nos dois lados — se aparecer `SEM TOKEN`, aperte `K` e digite o mesmo valor do painel web.

**Confira sempre no NinjaTrader** antes de considerar encerrado.

## Lendo o painel

- **Faixa de cima**: todos os contratos, coloridos pela variação do dia. O ativo fica destacado.
- **Bolinha e barras**: conexão com a bridge e sinal de Wi-Fi.
- **Preço grande**: colorido pela direção do dia. A seta ao lado mostra se o último negócio subiu ou desceu.
- **Números cinza**: a conexão caiu. O rodapé passa a mostrar há quantos segundos o dado está parado.
- **Linha tracejada no gráfico**: fechamento de ontem. Acima dela você está no positivo.
- **Borda piscando**: alerta de preço cruzado.
- **`3 ORD`**: existem ordens de trabalho naquele contrato.

## No PC

Painel de configuração em `localhost:8088`, ou **Control Center → Tools → Painel Cardputer**.

Contas simuladas e desconectadas ficam fora do cálculo por padrão; os dois dropdowns permitem incluir. Ali você também troca a conta, os contratos, os alertas e o token. Vale na hora, sem recompilar. A seção *Contas consideradas* mostra o que está entrando no cálculo — é o primeiro lugar a olhar quando algo não aparece.

## Alertas sonoros

| Som | Quando |
|---|---|
| dois tons subindo | abriu posição comprada |
| dois tons descendo | abriu posição vendida |
| um grave longo | zerou a posição |
| clique agudo | aumentou a posição |
| clique médio | reduziu a posição |
| três agudos rápidos | preço cruzou um alerta |
| dois graves | perdeu contato com o PC |
| dois médios subindo | contato restabelecido |

A tecla `M` alterna entre **mudo**, **posição** (só os eventos de posição) e **tudo** (inclui os alertas de preço). A escolha fica salva.

O aviso de contato perdido dispara depois de **30 segundos** sem resposta e se repete a cada 5 minutos enquanto durar — assim o silêncio deixa de ser ambíguo quando você está com a tela apagada.

O som toca mesmo com a tela apagada — dá para deixar o Cardputer no bolso ou na mesa e ouvir quando uma estratégia entrou.

## Economia de bateria

Escurece em 1 minuto parado, apaga em 5. Qualquer tecla acorda, e essa primeira tecla só desperta — não dispara ação nenhuma.

**Com a tela apagada ele continua monitorando**, e o som continua tocando. Se o som estiver ligado, a consulta se mantém em 1 segundo para o alerta não atrasar. Só no modo mudo ela cai para 4 segundos.

A tela apagada rende cerca de uma hora a mais de autonomia. O grosso do consumo é o rádio Wi-Fi, que fica ligado de qualquer forma — espere de 10 a 13 horas.
