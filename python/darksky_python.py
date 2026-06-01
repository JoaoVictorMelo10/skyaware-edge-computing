# ============================================================
#  DarkSky — Script Python na VM
#  Flask API (porta 5000) + FIWARE + CelesTrak + Open-Meteo
# ============================================================

import requests
import paho.mqtt.client as mqtt
import time
import threading
from datetime import datetime
from flask import Flask, jsonify, request
from flask_cors import CORS

# ============================================================
# CONFIGURAÇÕES
# ============================================================
ORION_HOST  = "http://localhost:1026"
ENTITY_ID   = "urn:ngsi-ld:DarkSkyStation:001"
SERVICE     = "darksky"
SERVICEPATH = "/"

MQTT_BROKER = "localhost"
MQTT_PORT   = 1883
DEVICE_ID   = "darksky-esp32-001"
TOPIC_CMD   = f"/darksky2026/{DEVICE_ID}/cmd"

HEADERS_GET = {
    "fiware-service": SERVICE,
    "fiware-servicepath": SERVICEPATH
}
HEADERS_POST = {
    "fiware-service": SERVICE,
    "fiware-servicepath": SERVICEPATH,
    "Content-Type": "application/json"
}

# Localização padrão
DEFAULT_LAT  = -23.5505
DEFAULT_LON  = -46.6333
DEFAULT_CITY = "São Paulo"

# Bortle por cidade (0.0 = sem poluição, 1.0 = máximo)
BORTLE_MAP = {
    "sao_paulo":      0.75,
    "rio_de_janeiro": 0.70,
    "belo_horizonte": 0.65,
    "brasilia":       0.55,
    "curitiba":       0.60,
    "porto_alegre":   0.60,
    "salvador":       0.60,
    "campinas":       0.65,
    "interior":       0.30,
    "custom":         0.50,
}

# ============================================================
# Estado global
# ============================================================
state = {
    "lat": DEFAULT_LAT, "lon": DEFAULT_LON,
    "city": DEFAULT_CITY, "polv_luminosa": 0.75,
    "sky_score": 0.0, "f_orbital": 0.70,
    "f_local": 0.0, "base": 0.0,
    "m_atm": 0.0, "m_lum": 0.0,
    "cobertura_nuvens": 0.20,
    "temp": 0.0, "hum": 0.0,
    "pressure": 0.0, "ldr_raw": 0,
    "last_update": None,
    "status": "aguardando",
    "corte_ativo": False, "corte_motivo": "",
    # Simulação
    "sim_active": False,
    "sim_cloud": None, "sim_polv": None, "sim_orbital": None
}

# ============================================================
# Flask
# ============================================================
app = Flask(__name__)
CORS(app)

@app.route('/score', methods=['GET'])
def get_score():
    return jsonify({
        "skyScore":         state["sky_score"],
        "fatorOrbital":     round(state["f_orbital"], 3),
        "fatorLocal":       round(state["f_local"], 3),
        "base":             round(state["base"], 3),
        "multiplicadorAtm": round(state["m_atm"], 3),
        "multiplicadorLum": round(state["m_lum"], 3),
        "coberturaNuvens":  round(state["cobertura_nuvens"] * 100, 1),
        "polvLuminosa":     round(state["polv_luminosa"] * 100, 1),
        "temperatura":      state["temp"],
        "umidade":          state["hum"],
        "pressao":          state["pressure"],
        "ldrRaw":           state["ldr_raw"],
        "cidade":           state["city"],
        "lat":              state["lat"],
        "lon":              state["lon"],
        "status":           state["status"],
        "corteAtivo":       state["corte_ativo"],
        "corteMotivo":      state["corte_motivo"],
        "lastUpdate":       state["last_update"],
        "simActive":        state["sim_active"]
    })

@app.route('/location', methods=['POST'])
def set_location():
    data = request.get_json()
    if not data:
        return jsonify({"error": "Body JSON invalido"}), 400
    lat      = data.get("lat")
    lon      = data.get("lon")
    city     = data.get("city", "custom")
    city_key = data.get("city_key", "custom")
    if lat is None or lon is None:
        return jsonify({"error": "lat e lon obrigatorios"}), 400
    state["lat"]          = float(lat)
    state["lon"]          = float(lon)
    state["city"]         = city
    state["polv_luminosa"] = BORTLE_MAP.get(city_key, 0.50)
    # Atualiza nuvens para nova localização imediatamente
    if not state["sim_active"]:
        state["cobertura_nuvens"] = get_cloud_cover(state["lat"], state["lon"])
    print(f"[LOCATION] {city} ({lat},{lon}) Bortle={state['polv_luminosa']}")
    return jsonify({"ok": True})

@app.route('/forecast', methods=['GET'])
def get_forecast():
    lat = request.args.get("lat", state["lat"])
    lon = request.args.get("lon", state["lon"])
    try:
        url = (f"https://api.open-meteo.com/v1/forecast"
               f"?latitude={lat}&longitude={lon}"
               f"&hourly=cloud_cover&forecast_days=1"
               f"&timezone=America/Sao_Paulo")
        r = requests.get(url, timeout=10)
        if r.status_code == 200:
            data = r.json()
            hours  = data["hourly"]["time"][:12]
            clouds = data["hourly"]["cloud_cover"][:12]
            forecast = []
            for h, c in zip(hours, clouds):
                m_atm = 1.0 - (c / 100.0)
                m_lum = 1.0 - (state["polv_luminosa"] * 0.9)
                proj  = round(state["base"] * m_atm * m_lum * 10, 1) if state["base"] > 0 else 0
                forecast.append({"hora": h[-5:], "nuvens": c, "scoreProjetado": proj})
            return jsonify({"forecast": forecast, "city": state["city"]})
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/simulate', methods=['POST'])
def simulate():
    data = request.get_json()
    if not data:
        return jsonify({"error": "Body JSON invalido"}), 400
    state["sim_active"] = True
    if "coberturaNuvens" in data:
        v = float(data["coberturaNuvens"]) / 100.0
        state["sim_cloud"]       = v
        state["cobertura_nuvens"] = v
        print(f"[SIM] nuvens={data['coberturaNuvens']}%")
    if "polvLuminosa" in data:
        v = float(data["polvLuminosa"]) / 100.0
        state["sim_polv"]        = v
        state["polv_luminosa"]    = v
        print(f"[SIM] polv={data['polvLuminosa']}%")
    if "fatorOrbital" in data:
        v = float(data["fatorOrbital"])
        state["sim_orbital"]  = v
        state["f_orbital"]    = v
        print(f"[SIM] orbital={v}")
    return jsonify({"ok": True,
                    "coberturaNuvens": round(state["cobertura_nuvens"]*100,1),
                    "polvLuminosa":    round(state["polv_luminosa"]*100,1),
                    "fatorOrbital":    round(state["f_orbital"],3)})

@app.route('/simulate/reset', methods=['POST'])
def simulate_reset():
    state["sim_active"]  = False
    state["sim_cloud"]   = None
    state["sim_polv"]    = None
    state["sim_orbital"] = None
    state["polv_luminosa"]    = BORTLE_MAP.get("sao_paulo", 0.75)
    state["cobertura_nuvens"] = get_cloud_cover(state["lat"], state["lon"])
    print("[SIM] Reset — dados reais")
    return jsonify({"ok": True})

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "ok", "service": "DarkSky Python API"})

# ============================================================
# MQTT
# ============================================================
client = mqtt.Client()
client.on_connect = lambda c, u, f, rc: print(f"[MQTT] rc={rc}")
client.on_publish = lambda c, u, mid: None

def connect_mqtt():
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.loop_start()
    except Exception as e:
        print(f"[MQTT] Erro: {e}")

def enviar_comando(comando):
    client.publish(TOPIC_CMD, f"{DEVICE_ID}@{comando}|")
    print(f"  [CMD] {comando}")

# ============================================================
# APIs externas
# ============================================================
def get_orbital_quality():
    try:
        r = requests.get(
            "https://celestrak.org/SOCRATES/query.php?CATNR=25544&LIMIT=10&FORMAT=json",
            timeout=10)
        if r.status_code == 200:
            try:
                data = r.json()
                n = len(data) if isinstance(data, list) else 5
                f = max(0.3, min(1.0, 1.0 - (n / 10.0) * 0.7))
                print(f"  [CelesTrak] n={n} → f_orbital={f:.3f}")
                return f
            except: pass
        r2 = requests.get(
            "https://celestrak.org/SOCRATES/query.php?CATNR=25544&FORMAT=TLE",
            timeout=10)
        if r2.status_code == 200:
            lines = [l for l in r2.text.strip().split('\n') if l.strip()]
            n = max(1, len(lines) // 3)
            f = max(0.3, min(1.0, 1.0 - (n / 10.0) * 0.7))
            print(f"  [CelesTrak] n={n} → f_orbital={f:.3f}")
            return f
    except Exception as e:
        print(f"  [CelesTrak] Erro: {e}")
    return 0.70

def get_cloud_cover(lat, lon):
    try:
        url = (f"https://api.open-meteo.com/v1/forecast"
               f"?latitude={lat}&longitude={lon}"
               f"&current=cloud_cover&timezone=America/Sao_Paulo")
        r = requests.get(url, timeout=10)
        if r.status_code == 200:
            c = r.json()["current"]["cloud_cover"] / 100.0
            print(f"  [Open-Meteo] nuvens={c*100:.0f}%")
            return c
    except Exception as e:
        print(f"  [Open-Meteo] Erro: {e}")
    return 0.20

# ============================================================
# FÓRMULA HÍBRIDA CORRIGIDA
# Score = B × M_atm × M_lum × 10
# Correções:
#   - Valores zero tratados corretamente (sem fallback "or 60")
#   - M_lum recalibrado: polv=0.75 → M_lum=0.625 (não mais 0.325)
#     Fórmula: M_lum = 1.0 - (polv * 0.5)
#     Deserto (polv=0)  → M_lum = 1.0
#     Interior (polv=0.3) → M_lum = 0.85
#     SP (polv=0.75)    → M_lum = 0.625
#     Centro SP (polv=0.9) → M_lum = 0.55
#   - Score 10/10 possível em condições de deserto
# ============================================================
def calcular_sky_score(temp, hum, pressure, ldr_raw,
                        f_orbital, cobertura_nuvens, polv_luminosa):
    # Portões de corte
    if cobertura_nuvens >= 0.85:
        return 0.0, 0.0, 0.0, 0.0, 0.0, True, f"Céu {cobertura_nuvens*100:.0f}% nublado"
    if polv_luminosa >= 0.90:
        return 1.0, 0.0, 1.0, 0.55, 0.0, True, "Poluição luminosa extrema"

    # Fator local — CORRIGIDO: trata zero corretamente
    hum_score      = max(0.0, min(1.0, 1.0 - (hum / 100.0)))
    pressure_score = max(0.0, min(1.0, (pressure - 980.0) / 50.0))
    darkness_score = max(0.0, min(1.0, (4095 - ldr_raw) / 4095.0))
    f_local = (hum_score + pressure_score + darkness_score) / 3.0

    # Base ponderada
    base = (f_orbital * 0.7) + (f_local * 0.3)

    # Multiplicadores — RECALIBRADO
    m_atm = 1.0 - cobertura_nuvens
    m_lum = 1.0 - (polv_luminosa * 0.5)  # antes era 0.9, agora 0.5

    score = round(base * m_atm * m_lum * 10.0, 1)
    score = max(0.0, min(10.0, score))
    return score, f_local, base, m_atm, m_lum, False, ""

def get_entity_data():
    try:
        r = requests.get(
            f"{ORION_HOST}/v2/entities/{ENTITY_ID}",
            headers=HEADERS_GET, timeout=5)
        if r.status_code == 200:
            return r.json()
    except Exception as e:
        print(f"  [ORION] Falha: {e}")
    return None

def atualizar_orion(sky_score, f_orbital, f_local,
                     cobertura_nuvens, polv_luminosa, m_atm, m_lum):
    try:
        body = {
            "skyScore":          {"type": "Number", "value": sky_score},
            "fatorLocal":        {"type": "Number", "value": round(f_local, 3)},
            "fatorOrbital":      {"type": "Number", "value": round(f_orbital, 3)},
            "multiplicadorAtm":  {"type": "Number", "value": round(m_atm, 3)},
            "multiplicadorLum":  {"type": "Number", "value": round(m_lum, 3)},
            "coberturaNuvens":   {"type": "Number", "value": round(cobertura_nuvens*100,1)},
            "polvLuminosa":      {"type": "Number", "value": round(polv_luminosa*100,1)}
        }
        r = requests.patch(
            f"{ORION_HOST}/v2/entities/{ENTITY_ID}/attrs",
            headers=HEADERS_POST, json=body, timeout=5)
        if r.status_code in [200, 204]:
            print(f"  [ORION] skyScore={sky_score}")
        else:
            print(f"  [ORION] Erro {r.status_code}")
    except Exception as e:
        print(f"  [ORION] Falha: {e}")

# ============================================================
# Loop principal
# ============================================================
def main_loop():
    print("[LOOP] Iniciando...")
    connect_mqtt()
    time.sleep(2)

    cache_interval   = 600
    last_api_fetch   = 0
    f_orbital_cached = 0.70

    while True:
        try:
            now_ts = time.time()
            now    = datetime.now().strftime("%H:%M:%S")
            print(f"\n[{now}] Ciclo...")

            # Atualiza APIs externas a cada 10 min (só se não estiver em simulação)
            if now_ts - last_api_fetch >= cache_interval:
                f_orbital_cached = get_orbital_quality()
                if not state["sim_active"]:
                    state["cobertura_nuvens"] = get_cloud_cover(
                        state["lat"], state["lon"])
                last_api_fetch = now_ts

            # Em simulação, usa valores simulados
            f_orbital_use = state["f_orbital"] if state["sim_active"] else f_orbital_cached
            if not state["sim_active"]:
                state["f_orbital"] = f_orbital_cached

            data = get_entity_data()
            if data:
                # CORRIGIDO: trata None e zero separadamente
                def safe_float(key, default):
                    val = data.get(key, {}).get("value")
                    return float(val) if val is not None else default

                temp     = safe_float("temperature", 22.0)
                hum      = safe_float("humidity",    60.0)
                pressure = safe_float("pressure",    1013.0)
                ldr_raw  = int(safe_float("ldrRaw",  2048))

                score, f_local, base, m_atm, m_lum, corte, motivo = calcular_sky_score(
                    temp, hum, pressure, ldr_raw,
                    f_orbital_use,
                    state["cobertura_nuvens"],
                    state["polv_luminosa"]
                )

                state.update({
                    "sky_score": score, "f_local": f_local, "base": base,
                    "m_atm": m_atm, "m_lum": m_lum,
                    "temp": temp, "hum": hum,
                    "pressure": pressure, "ldr_raw": ldr_raw,
                    "last_update": now,
                    "corte_ativo": corte, "corte_motivo": motivo
                })

                print(f"  f_orbital: {f_orbital_use:.3f}{' [SIM]' if state['sim_active'] else ' [CelesTrak]'}")
                print(f"  f_local:   {f_local:.3f}  [ESP32]")
                print(f"  M_atm:     {m_atm:.3f}  [{state['cobertura_nuvens']*100:.0f}% nuvens]")
                print(f"  M_lum:     {m_lum:.3f}  [polv={state['polv_luminosa']*100:.0f}%]")
                print(f"  >>> Score: {score}{' [SIMULAÇÃO]' if state['sim_active'] else ''}")

                if score >= 7.0:
                    state["status"] = "ideal"
                    enviar_comando("green")
                elif score >= 4.0:
                    state["status"] = "moderado"
                    enviar_comando("off")
                else:
                    state["status"] = "ruim"
                    enviar_comando("red")

                atualizar_orion(score, f_orbital_use, f_local,
                                state["cobertura_nuvens"], state["polv_luminosa"],
                                m_atm, m_lum)
            else:
                print("  Sem dados do Orion...")

        except Exception as e:
            print(f"[ERRO] {e}")

        time.sleep(10)

# ============================================================
if __name__ == "__main__":
    print("=" * 55)
    print("  DarkSky Python — Flask + FIWARE + CelesTrak + Open-Meteo")
    print("=" * 55)
    t = threading.Thread(target=main_loop, daemon=True)
    t.start()
    app.run(host="0.0.0.0", port=5000, debug=False)
