# API de Satélites — Guia para o Front-end

Documento de referência sobre a chave `satelites` retornada pela API do DarkSky Station. Cobre formato, origem de cada campo (real / calculado / estimado), comportamento e casos de borda.

---

## Onde encontrar os dados

Os satélites vêm **dentro do `/score`**, na chave `satelites` — um fetch só traz tudo (score + telemetria + satélites).

```
GET https://darksky-fiap.duckdns.org/score
```

Resposta (recortada):

```json
{
  "skyScore": 6.2,
  "cidade": "São Paulo",
  "lat": -23.5505,
  "lon": -46.6333,
  "...": "demais campos do score",
  "satelites": [
    {
      "id": "STARLINK-2503",
      "constellation": "Starlink",
      "altitude": 476,
      "velocity": 7.6,
      "azimuth": 52.2,
      "elevation": 24.8,
      "passesIn": 22,
      "magnitude": 2.5,
      "danger": false
    },
    {
      "id": "ONEWEB-0312",
      "constellation": "OneWeb",
      "altitude": 1200,
      "velocity": 7.1,
      "azimuth": 90.4,
      "elevation": 45.2,
      "passesIn": 16,
      "magnitude": 4.0,
      "danger": false
    }
  ]
}
```

---

## Constelações incluídas

O array traz **duas constelações de internet reais**, consultadas por categoria dedicada no N2YO:

- **Starlink** (categoria 52) — ~10.000 satélites em LEO, maioria do array
- **OneWeb** (categoria 53) — ~650 satélites em órbita mais alta, aparecem em menor número

**Kuiper e NOAA não aparecem** — o N2YO não tem categoria própria pra Kuiper (constelação muito recente) e NOAA não é constelação de internet. O contrato já reconhece os nomes deles caso surjam no futuro, mas hoje não há dado. Qualquer satélite não reconhecido vem com `constellation: "Outro"`.

---

## Campos do objeto satélite

| Campo | Tipo | Origem | Observação |
|---|---|---|---|
| `id` | string | **Real** (N2YO) | Nome do satélite, ex: `"STARLINK-2503"`, `"ONEWEB-0312"` |
| `constellation` | string | **Real** (inferido do nome) | `"Starlink"`, `"OneWeb"` ou `"Outro"` — case exato |
| `altitude` | number | **Real** (N2YO `satalt`) | Altitude em km |
| `azimuth` | number | **Calculado** | 0–360°, 0 = Norte. Derivado da posição real do satélite |
| `elevation` | number | **Calculado** | 0–90°, ângulo acima do horizonte. Derivado da posição real |
| `velocity` | number | **Estimado** | km/s. Calculado pela física orbital a partir da altitude |
| `passesIn` | number | **Estimado** | Minutos até passagem. Aproximado pela elevação |
| `magnitude` | number | **Estimado** | Fixo por constelação (não medido) |
| `danger` | boolean | **Derivado** | `true` se `passesIn <= 10` |

---

## O que é real vs. estimado (importante)

A API gratuita do N2YO (endpoint `above`) fornece para cada satélite apenas: nome, latitude/longitude do ponto subsatélite e altitude. Os demais campos foram derivados ou estimados a partir disso. Detalhamento honesto:

### Reais (vêm direto da N2YO)
- **`id`** — nome oficial do satélite no catálogo NORAD.
- **`altitude`** — altitude orbital real naquele instante. Starlink fica em ~500 km, OneWeb em ~1.200 km — dá pra ver a diferença no campo.
- **`constellation`** — inferida do nome (`"STARLINK-..."` -> `"Starlink"`, `"ONEWEB-..."` -> `"OneWeb"`). A consulta usa as categorias 52 (Starlink) e 53 (OneWeb), então hoje vêm essas duas. Nomes fora do padrão recebem `"Outro"`.

### Calculados (matematicamente derivados de dados reais)
- **`azimuth` e `elevation`** — a N2YO dá a posição do satélite (lat/lng/alt). O backend converte isso em coordenadas horizontais (azimute e elevação) relativas ao observador, usando trigonometria esférica. São **posições reais no céu** — não são aleatórias. Um satélite com `elevation: 67` está realmente alto no céu; com `elevation: 5` está perto do horizonte.

### Estimados (aproximações documentadas)
- **`velocity`** — não vem da N2YO. Calculada pela fórmula da velocidade orbital `v = sqrt(GM / (R + altitude))`, com `GM = 398600 km³/s²` e `R = 6371 km`. Starlink (mais baixo) dá ~7.6 km/s; OneWeb (mais alto) dá ~7.1 km/s. É estimativa, mas fisicamente correta — e varia com a altitude real de cada satélite.
- **`passesIn`** — não vem da N2YO (exigiria outro endpoint, 1 request por satélite). Estimado pela elevação: satélite com elevação alta está próximo do zênite e tende a passar mais rápido. Escala usada: elevação 0° -> ~30 min; elevação 90° -> ~2 min. É uma aproximação para dar sensação de urgência relativa, não um horário exato de passagem.
- **`magnitude`** — não vem da N2YO. Valor fixo por constelação: Starlink +2.5, OneWeb +4.0, NOAA +3.5, Kuiper +3.0, Outro +3.0.
- **`danger`** — derivado de `passesIn <= 10`. Como `passesIn` é estimado, `danger` herda essa natureza aproximada.

---

## Comportamento

### Quantidade de satélites varia
O número de satélites no array **muda ao longo do dia** conforme as constelações passam sobre a coordenada. Pode ser **0 em alguns momentos e ~80 em outros**. Já foi observado de 7 a 72 sobre São Paulo (ex: 64 Starlink + 8 OneWeb). O canvas precisa aguentar essa faixa sem quebrar o layout.

### Atualização
- O array é recalculado **a cada ~10 minutos** (ciclo do backend) e **imediatamente quando a localização muda** (POST `/location`).
- Entre atualizações, o array fica estático. Se o canvas faz animação contínua, ela é interpolação do front — o backend não envia posições novas a cada segundo.

### Localização
O array reflete a coordenada atual do sistema. Quando o usuário troca de cidade (POST `/location` com `lat`/`lon`/`city`/`city_key`), os satélites são recalculados para a nova posição em até ~1 segundo. Cidades diferentes mostram satélites diferentes.

### Raio de cobertura
A consulta usa raio de 70° ao redor do zênite — ou seja, praticamente todo o hemisfério visível. Por isso aparecem satélites com elevação baixa (perto do horizonte) também, não só os no topo do céu.

### Custo de requisição
Cada ciclo faz 2 requests ao N2YO (um por constelação). Bem abaixo do limite gratuito de 1.000/hora.

---

## Casos de borda para tratar no front

| Situação | O que acontece | Sugestão no front |
|---|---|---|
| `satelites: []` (array vazio) | Nenhum satélite sobre a coordenada naquele instante | Mostrar "céu limpo de satélites agora", não quebrar |
| Muitos satélites (~80) | Array grande | Garantir que o canvas comporta sem travar |
| `elevation` próximo de 0 | Satélite no horizonte | Decidir se plota ou filtra (ex: só `elevation > 10`) |
| `azimuth` 0 e 360 | São o mesmo ponto (Norte) | Tratar como contínuo, não saltar |
| `constellation: "Outro"` | Satélite fora de Starlink/OneWeb | Ter uma cor neutra de fallback |
| API fora do ar | `/score` não responde | Fallback / estado de carregamento |

---

## Mapa de cores por constelação (do front)

O `constellation` vem com case exato para casar com o mapa de cores. Hoje vêm `"Starlink"` e `"OneWeb"`; o contrato suporta os demais e um fallback:

```js
const CORES = {
  "Starlink": "...",
  "OneWeb":   "...",
  "NOAA":     "...",   // não aparece hoje, reservado
  "Kuiper":   "...",   // não aparece hoje, reservado
  "Outro":    "..."    // fallback para satélites não reconhecidos
};
```

Recomendado ter sempre a cor `"Outro"` definida, pra nenhum ponto ficar sem cor.

---

## Resumo de uma linha

`GET /score` -> chave `satelites` -> array de objetos (Starlink + OneWeb) com posição **real** no céu (azimuth/elevation calculados de dados reais da N2YO), velocidade fisicamente estimada pela altitude, e magnitude/passesIn aproximados. Quantidade varia de 0 a ~80. Atualiza a cada 10 min ou na troca de cidade.
