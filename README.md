# Sky Aware — DarkSky Station
### Edge Computing & Computer Systems · Global Solution 2026 · FIAP

> Estação de monitoramento hiperlocal do céu noturno. Coleta dados físicos do ambiente via ESP32, integra com dados orbitais processados na nuvem e calcula o **Sky Observation Score** — um índice de 0 a 10 que indica a qualidade do céu para observação astronômica.

---

## Equipe — 1ESPA

| Nome | RM |
|---|---|
| João Victor Melo Santos | 566640 |
| Murilo Jeronimo Ferreira Nunes | 560641 |
| Vinicius Kozonoe Guaglini | 567264 |
| Yan Lucas Gonçalves da Silva | 567046 |
| Bruno Santos Castilho | 566799 |

---

## Links

| Recurso | Link |
|---|---|
| Repositório GitHub | [skyaware-edge-computing](https://github.com/JoaoVictorMelo10/skyaware-edge-computing) |
| Simulação Wokwi | [Abrir projeto no Wokwi](https://wokwi.com/projects/465279238243903489) |

---

## O Problema

Desde 2019, constelações como Starlink (SpaceX), Kuiper (Amazon) e OneWeb colocaram mais de 6.000 satélites em órbita baixa (LEO). Cada satélite reflete luz solar e aparece como rastro nas fotografias astronômicas. O Vera C. Rubin Observatory estima que até **30% de suas imagens científicas** já são contaminadas por rastros de satélites.

O problema não é só orbital — condições físicas locais como umidade alta, pressão instável e poluição luminosa do entorno imediato também inviabilizam uma sessão de observação.

> **Nenhum satélite consegue medir o poste a 20 metros do seu telescópio. Nenhum servidor na nuvem sabe se há neblina no seu quintal agora. O ESP32 sabe.**

A **DarkSky Station** resolve isso: é o ponto do sistema que mede o ambiente físico hiperlocal e combina esse dado com a inteligência orbital processada na nuvem.

---

## Arquitetura do Sistema

<!-- INSERIR IMAGEM AQUI -->
<!-- ![Arquitetura DarkSky Station](docs/arquitetura.png) -->

> ⚠️ Diagrama de arquitetura disponível em `docs/arquitetura.png`

### Fluxo de Dados

```
CelesTrak (TLE) ──────────────────────────────┐
Open-Meteo API (nuvens) ──────────────────────┤
                                               ▼
                              Python / Flask (VM Azure)
                              ├── Calcula f_orbital via CelesTrak
                              ├── Obtém cobertura de nuvens via Open-Meteo
                              ├── Aplica BORTLE_MAP (poluição luminosa)
                              └── Busca dados brutos do ESP32 no Orion CB
                                               │
                              FIWARE Orion Context Broker
                              └── IoT Agent MQTT
                                               │
                    ┌──────────────────────────┘
                    ▼
           ESP32 — DarkSky Station
           ├── Lê DHT22 (temperatura, umidade)
           ├── Lê BMP180 (pressão atmosférica)
           ├── Lê LDR (luminosidade local)
           └── Publica dados RAW via MQTT → FIWARE
                    │
                    ▼
           Python calcula Sky Observation Score
           └── Envia comando de LED ao ESP32 via MQTT
                    │
           ┌────────┴────────┐
           ▼                 ▼
    ESP32 acende         React Dashboard
    LED verde/vermelho   consome Flask API
    + buzzer             via HTTPS
```

---

## Sky Observation Score — Fórmula Híbrida

O score é calculado inteiramente pelo Python na VM. O ESP32 fornece os dados físicos brutos e executa os comandos recebidos — não calcula o score localmente.

### Portões de corte absoluto

| Condição | Resultado | Motivo |
|---|---|---|
| Cobertura de nuvens ≥ 85% | Score = 0.0 | Observação inviável |
| Poluição luminosa ≥ 90% | Score = 1.0 | Mínimo histórico |

### Cálculo completo

```
FATOR LOCAL — sensores do ESP32 (0 a 1):
  hum_score      = 1.0 − (umidade / 100)
  pressure_score = (pressão − 980) / 50        [clamp 0–1]
  darkness_score = (4095 − ldr_raw) / 4095
  f_local        = (hum_score + pressure_score + darkness_score) / 3

FATOR ORBITAL — via CelesTrak (0.3 a 1.0):
  f_orbital = 1.0 − (n_satelites / 10.0) × 0.7

BASE PONDERADA:
  B = (f_orbital × 0.7) + (f_local × 0.3)

MULTIPLICADORES DE CORTE:
  M_atm = 1.0 − cobertura_nuvens        [Open-Meteo]
  M_lum = 1.0 − (polv_luminosa × 0.5)   [BORTLE_MAP]

SCORE FINAL (0 a 10):
  Score = B × M_atm × M_lum × 10
```

### Exemplos práticos

| Cenário | B | M_atm | M_lum | Score | Status |
|---|---|---|---|---|---|
| Céu 100% nublado | 0.75 | 0.00 | 0.625 | 0.0 | CORTE |
| Noite moderada SP (26% nuvens) | 0.75 | 0.74 | 0.625 | 3.5 | RUIM |
| Noite boa SP (10% nuvens) | 0.75 | 0.90 | 0.625 | 4.2 | MODERADO |
| Interior SP (5% nuvens) | 0.80 | 0.95 | 0.850 | 6.5 | MODERADO |
| Deserto ideal (0% nuvens) | 0.95 | 1.00 | 1.000 | 9.5 | IDEAL |

### Comportamento dos LEDs e Buzzer

| Score | LED Verde | LED Vermelho | Buzzer | Status no OLED |
|---|---|---|---|---|
| ≥ 7.0 | ACESO | Apagado | 1 beep curto | `CEU IDEAL` |
| 4.0 – 6.9 | Apagado | Apagado | Silêncio | `MODERADO` |
| < 4.0 | Apagado | ACESO | Bipes intermitentes | `CEU RUIM` |

---

## Hardware — Componentes

| Componente | Qtd | Função |
|---|---|---|
| ESP32 DevKit C V4 | 1 | Microcontrolador principal |
| DHT22 | 1 | Temperatura e umidade relativa |
| LDR (Photoresistor) | 1 | Luminosidade ambiente |
| BMP180 | 1 | Pressão atmosférica |
| OLED SSD1306 128×64 | 1 | Display de dados em tempo real |
| LED Verde | 1 | Indicador céu ideal |
| LED Vermelho | 1 | Indicador céu ruim |
| Resistor 220Ω | 2 | Proteção dos LEDs |
| Buzzer | 1 | Alerta sonoro |

### Pinagem

| Componente | Pino do componente | Pino ESP32 |
|---|---|---|
| DHT22 | VCC | 3V3 |
| DHT22 | GND | GND |
| DHT22 | DATA | GPIO 15 |
| LDR | VCC | 3V3 |
| LDR | GND | GND |
| LDR | AO | GPIO 34 |
| OLED SSD1306 | VCC | 3V3 |
| OLED SSD1306 | GND | GND |
| OLED SSD1306 | SDA | GPIO 21 |
| OLED SSD1306 | SCL | GPIO 22 |
| BMP180 | VCC | 3V3 |
| BMP180 | GND | GND |
| BMP180 | SDA | GPIO 21 (mesmo barramento I²C) |
| BMP180 | SCL | GPIO 22 (mesmo barramento I²C) |
| LED Verde | Anodo (A) | Resistor 220Ω → GPIO 18 |
| LED Verde | Catodo (C) | GND |
| LED Vermelho | Anodo (A) | Resistor 220Ω → GPIO 19 |
| LED Vermelho | Catodo (C) | GND |
| Buzzer | + | GPIO 27 |
| Buzzer | - | GND |

---

## FIWARE — Integração IoT

### Tópicos MQTT

| Tópico | Direção | Descrição |
|---|---|---|
| `/darksky2026/darksky-esp32-001/attrs` | ESP32 → FIWARE | ESP32 publica leituras dos sensores |
| `/darksky2026/darksky-esp32-001/cmd` | Python → ESP32 | Python envia comando green/red/off |

### Formato Ultralight 2.0 — Publish (ESP32 → FIWARE)

```
t|22.5|h|60.0|p|1013.2|l|2048
```

`t` = temperatura · `h` = umidade · `p` = pressão · `l` = LDR raw

### Formato do comando — Subscribe (Python → ESP32)

```
darksky-esp32-001@green|   → LED verde (céu ideal)
darksky-esp32-001@off|     → LEDs apagados (moderado)
darksky-esp32-001@red|     → LED vermelho + buzzer (céu ruim)
```

### Atributos da entidade FIWARE

| Atributo | Tipo | Origem | Descrição |
|---|---|---|---|
| `temperature` | Number | ESP32 | Temperatura em °C |
| `humidity` | Number | ESP32 | Umidade relativa em % |
| `pressure` | Number | ESP32 | Pressão atmosférica em hPa |
| `ldrRaw` | Number | ESP32 | Valor bruto do LDR (0–4095) |
| `skyScore` | Number | Python | Sky Observation Score (0–10) |
| `fatorLocal` | Number | Python | Fator local combinado (0–1) |
| `fatorOrbital` | Number | Python | Qualidade orbital (0–1) |
| `multiplicadorAtm` | Number | Python | M_atm = 1 − cobertura_nuvens |
| `multiplicadorLum` | Number | Python | M_lum = 1 − (polv × 0.5) |
| `coberturaNuvens` | Number | Python/Open-Meteo | Cobertura de nuvens em % |
| `polvLuminosa` | Number | Python/BORTLE_MAP | Poluição luminosa em % |
| `led` | command | Python | Comando enviado ao ESP32 |

### Comandos úteis para debug na VM

```bash
# Simular ESP32 publicando leitura
mosquitto_pub -h localhost -p 1883 \
  -t "/darksky2026/darksky-esp32-001/attrs" \
  -m "t|22.5|h|60.0|p|1013.2|l|2048"

# Escutar comandos enviados ao ESP32
mosquitto_sub -h localhost -p 1883 \
  -t "/darksky2026/darksky-esp32-001/cmd" -v
```

---

## Flask API

**Base URL:** `https://darksky-fiap.duckdns.org`

| Endpoint | Método | Função |
|---|---|---|
| `/health` | GET | Verifica se a API está online |
| `/score` | GET | Score atual com todos os fatores |
| `/location` | POST | Atualiza localização e recalcula condições |
| `/forecast` | GET | Previsão de nuvens e score projetado (12h) |
| `/simulate` | POST | Simula condições para demonstração |
| `/simulate/reset` | POST | Volta para dados reais das APIs |

### Exemplo de retorno do `/score`

```json
{
  "skyScore": 5.0,
  "fatorOrbital": 0.700,
  "fatorLocal": 0.686,
  "base": 0.696,
  "multiplicadorAtm": 0.800,
  "multiplicadorLum": 0.625,
  "coberturaNuvens": 20.0,
  "polvLuminosa": 75.0,
  "temperatura": 24.0,
  "umidade": 60.0,
  "pressao": 1013.2,
  "ldrRaw": 32,
  "cidade": "São Paulo",
  "status": "moderado",
  "corteAtivo": false
}
```

---

## Como Executar

### 1. Subir o FIWARE na VM

```bash
cd ~/darksky-fiware
docker-compose up -d
docker ps
```

### 2. Iniciar a API Python

```bash
sudo systemctl start darksky-python
sudo systemctl status darksky-python
```

### 3. Provisionar o dispositivo (Postman)

Importe `postman/darksky-fiware.postman_collection.json` e execute na ordem:

```
2.1 Criar Service Group → 2.2 Registrar Dispositivo → 3.3 Verificar Entidade
```

> Headers obrigatórios: `fiware-service: darksky` · `fiware-servicepath: /`
>
> Se precisar recomeçar do zero: execute a pasta `5. Limpeza` antes de reprovisionar.

### 4. Iniciar o Wokwi

Abrir o [projeto no Wokwi](https://wokwi.com/projects/465279238243903489) e clicar em Play. O ESP32 conecta automaticamente na rede `Wokwi-GUEST` e começa a publicar dados via MQTT.

### 5. Verificar funcionamento

```bash
curl https://darksky-fiap.duckdns.org/health
# → {"status":"ok","service":"DarkSky Python API"}

curl https://darksky-fiap.duckdns.org/score
# → JSON com skyScore, temperatura, umidade, etc.
```

---

## Estrutura do Repositório

```
skyaware-edge-computing/
├── README.md
├── hardware/
│   └── darksky_wokwi_fiware.ino      ← código do ESP32
├── python/
│   ├── darksky_python.py             ← script Python Flask
│   ├── requirements.txt              ← dependências
│   └── darksky-python.service        ← serviço systemd
├── postman/
│   └── darksky-fiware.postman_collection.json
├── site/
│   └── index.html                    ← dashboard com 3 perfis
└── docs/
    ├── arquitetura.png
    ├── sky_observation_score_proposta.pdf
    └── DarkSky_EdgeComputing_GS2026.pdf
```

---

## Stack Técnica

| Camada | Tecnologia |
|---|---|
| Dispositivo de borda | ESP32 (Wokwi) + DHT22 + LDR + BMP180 + OLED + LEDs + Buzzer |
| Protocolo de transmissão | MQTT — Ultralight 2.0 |
| Back-end IoT | FIWARE (Orion Context Broker + IoT Agent UL + Mosquitto + MongoDB) |
| Processamento | Python 3 + Flask |
| Segurança | Nginx + Let's Encrypt (HTTPS) + DuckDNS |
| Infraestrutura | VM Azure Ubuntu 22.04 + Docker Compose |
| Dashboard | HTML + CSS + JavaScript (GitHub Pages) |

---

## Contexto — Sky Aware

A **DarkSky Station** é o módulo de edge computing da plataforma **Sky Aware**. O Sky Aware é o sistema completo de proteção do céu noturno — integra dados orbitais (CelesTrak/TLE), dados atmosféricos (Open-Meteo), poluição luminosa (BORTLE_MAP/VIIRS NASA) e os dados físicos hiperlocais coletados por esta estação para calcular o Sky Observation Score e exibi-lo em um dashboard com três perfis de usuário: Astrônomo Amador, Astrofotógrafo e Operador de Observatório.

---

*FIAP · Engenharia de Software · 1ESPA · Global Solution 2026*
