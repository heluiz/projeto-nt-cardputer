# CardputerTrader

Painel de cotações e posições do **NinjaTrader 8** num **M5Cardputer ADV**, na palma da mão.
*A NinjaTrader 8 market data and position panel running on an M5Cardputer ADV.*

![plataforma](https://img.shields.io/badge/ESP32--S3-M5Cardputer%20ADV-22D3EE)
![plataforma](https://img.shields.io/badge/NinjaTrader-8-F0369B)
![licença](https://img.shields.io/badge/licen%C3%A7a-MIT-blue)

---

## Como funciona

```
NinjaTrader 8 (PC)                       M5Cardputer ADV
CardputerBridge (AddOn C#)  ──HTTP──>    CardputerTrader (firmware)
porta 8088                               consulta 1x/s pelo Wi-Fi
```

O AddOn roda dentro do NinjaTrader, assina o market data dos contratos que você escolher, lê posições, ordens, execuções e PnL das contas, e serve tudo em JSON na rede local. O Cardputer consulta e desenha.

Dados de futuros em tempo real não existem de graça — a arquitetura aproveita o feed que você já paga, em vez de depender de API pública atrasada.

## O que ele mostra

- **Preço ao vivo** com variação do dia, seta do último negócio, bid e ask
- **Gráfico** dos últimos 10 minutos ou da sessão inteira, com grade de tempo
- **Posições abertas** de qualquer contrato, com preço médio e resultado
- **Ordens de trabalho** por conta
- **Contas individuais**, com realizado, aberto e total
- **Estratégias rodando**, com estado
- **Últimas 10 execuções**
- **Alertas de preço** — a borda pisca ao cruzar o nível
- **Botão de pânico** — zera posições, cancela ordens e desliga estratégias

Contas de simulação e desconectadas ficam fora do cálculo por padrão.

## Três temas

| | |
|---|---|
| **NEON** | ciano e magenta, cantos em colchete |
| **ÂMBAR** | CRT antigo, scanlines, sete segmentos |
| **MATRIX** | fósforo verde |

Tecla `T` alterna, escolha salva na memória do aparelho.

## Instalação rápida

**No PC:** copie os dois `.cs` de `NinjaTraderBridge/` para `Documentos\NinjaTrader 8\bin\Custom\AddOns\` e aperte **F5** no NinjaScript Editor. Configure em `http://localhost:8088/`.

**No Cardputer:** compile `CardputerTrader/` com PlatformIO e grave. Na primeira vez ele abre um assistente que procura a rede Wi-Fi, pede a senha e **descobre o PC sozinho** por broadcast — você não precisa saber o IP.

Passo a passo completo em [docs/INSTALACAO.md](docs/INSTALACAO.md).

## Documentação

- [Instalação e solução de problemas](docs/INSTALACAO.md)
- [Guia rápido de teclas](docs/GUIA-RAPIDO.md) — ou aperte `?` no próprio aparelho
- [Como compilar no VS Code](docs/COMPILAR-NO-VSCODE.md)
- [Análise crítica: riscos e limitações](docs/ANALISE-CRITICA.md)

## Hardware

[M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv) — ESP32-S3, LCD 240×135, teclado de 56 teclas, bateria de 1750 mAh. Autonomia de 10 a 13 horas com Wi-Fi ligado.

## Aviso

Este projeto **lê e envia ordens de encerramento** para uma plataforma de negociação real. O comando de emergência executa a mercado, ao preço disponível. Confira sempre o resultado no NinjaTrader antes de considerar uma posição encerrada.

Sem garantia de nenhum tipo — leia a [licença](LICENSE). Você é responsável pelas suas operações.

## Segurança na rede

O servidor responde a qualquer máquina da rede local. Defina um token no painel web para exigir autenticação, e **nunca exponha essa porta à internet**.
