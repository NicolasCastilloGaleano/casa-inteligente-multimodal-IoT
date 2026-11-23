"""Local API, serial bridge and simulator for the multimodal smart-home prototype."""
from __future__ import annotations

import argparse
import csv
import io
import json
import re
import sqlite3
import threading
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path
from threading import Lock
from typing import Any

from flask import Flask, Response, jsonify, request, send_from_directory

ROOT = Path(__file__).resolve().parent
DB_PATH = ROOT / "instance" / "casa.sqlite3"
app = Flask(__name__, static_folder="web", static_url_path="")
db_lock = Lock()
_db_ready = False

DEFAULT_SENSORS: dict[str, Any] = {
    "temperature_c": 24.0,
    "setpoint_c": 25.0,
    "light_lux": 120.0,
    "gas_pct": 5.0,
    "brightness_pct": 80.0,
    "door_open": False,
    "contact": False,
}
SENSOR_LIMITS = {"temperature_c": (-10, 60), "setpoint_c": (18, 32), "light_lux": (0, 1000), "gas_pct": (0, 100),
                 "brightness_pct": (0, 100)}
ACTUATOR_DEFAULTS: dict[str, Any] = {"light": 0, "fan": False, "curtain": "closed", "curtain_pct": 0,
                                     "window": "closed", "latch": "locked", "siren": False, "bell": False}
# Actuadores que se pueden mandar desde la web, con sus valores válidos.
COMMANDS = {"light": "0-100", "fan": "bool", "curtain": {"open", "closed"}, "window": {"open", "closed"},
            "latch": {"locked", "unlocked"}, "siren": "bool"}
# Origen de cada entrada en la maqueta física.
SENSOR_SOURCES = {"temperature_c": "KY-028 · real", "setpoint_c": "potenciómetro (consigna)",
                  "light_lux": "potenciómetro (simula luz)", "gas_pct": "potenciómetro (simula gas)",
                  "brightness_pct": "potenciómetro", "door_open": "reed switch · real",
                  "contact": "KY-036 · real"}
UID_RE = re.compile(r"^[0-9A-F]{4,20}$")
PORT_RE = re.compile(r"^((rfc2217|socket)://[\w.\-]+:\d{1,5}|COM\d{1,3}|/dev/tty[\w.\-]+)$")
TELEMETRY_FIELDS = ["timestamp", "mode", "temperature_c", "setpoint_c", "light_lux", "gas_pct", "brightness_pct",
                    "door_open", "contact", "light", "fan", "curtain", "curtain_pct", "window", "latch", "siren",
                    "gas_alarm", "access_active", "armed", "intrusion"]
TELEMETRY_TYPES = {"mode": "TEXT", "temperature_c": "REAL", "setpoint_c": "REAL", "light_lux": "REAL", "gas_pct": "REAL",
                   "brightness_pct": "REAL", "door_open": "INTEGER", "light": "INTEGER", "fan": "INTEGER",
                   "curtain": "TEXT", "curtain_pct": "INTEGER", "window": "TEXT", "latch": "TEXT", "siren": "INTEGER",
                   "gas_alarm": "INTEGER", "access_active": "INTEGER", "contact": "INTEGER", "armed": "INTEGER",
                   "intrusion": "INTEGER"}

EVENT_TEXT = {
    "access_granted": ("access", "Acceso concedido", "ok"),
    "access_denied": ("access", "Acceso denegado", "alerta"),
    "access_closed": ("access", "Pestillo cerrado tras el acceso", "ok"),
    "door_opened": ("device", "Puerta abierta", "ok"),
    "door_closed": ("device", "Puerta cerrada", "ok"),
    "door_forced": ("alarm", "Puerta forzada: se abrió con el pestillo cerrado", "alerta"),
    "door_left_open": ("alarm", "La puerta quedó abierta después del acceso", "alerta"),
    "gas_alarm_on": ("alarm", "ALARMA DE GAS activada", "alerta"),
    "gas_alarm_off": ("alarm", "Alarma de gas desactivada", "ok"),
    "doorbell": ("device", "Timbre: alguien está en la puerta", "aviso"),
    "contact_detected": ("device", "Contacto en marco metálico (seguridad desactivada)", "ok"),
    "intrusion": ("alarm", "INTRUSIÓN detectada", "alerta"),
    "security_arming": ("security", "Activando modo seguridad", "ok"),
    "security_armed": ("security", "Modo seguridad ACTIVADO", "ok"),
    "security_disarmed": ("security", "Modo seguridad desactivado", "ok"),
    "security_arm_failed": ("security", "No se pudo activar la seguridad", "alerta"),
    "siren_silenced": ("alarm", "Sirena silenciada", "ok"),
    "temp_sensor_error": ("device", "Sensor de temperatura sin lectura", "alerta"),
    "temp_sensor_ok": ("device", "Sensor de temperatura en línea", "ok"),
    "auto_window_open": ("control", "Regla automática: ventana abierta", "auto"),
    "auto_window_closed": ("control", "Regla automática: ventana cerrada", "auto"),
    "auto_fan_on": ("control", "Regla automática: ventilador encendido", "auto"),
    "auto_fan_off": ("control", "Regla automática: ventilador apagado", "auto"),
    "auto_light_on": ("control", "Regla automática: luz encendida", "auto"),
    "auto_light_off": ("control", "Regla automática: luz apagada", "auto"),
    "auto_curtain_open": ("control", "Regla automática: cortina abierta", "auto"),
    "auto_curtain_closed": ("control", "Regla automática: cortina cerrada", "auto"),
    "mode_auto": ("control", "Modo automático", "ok"),
    "mode_manual": ("control", "Modo manual", "ok"),
    "rfid_learning": ("access", "Esperando tarjeta para autorizar", "ok"),
    "rfid_learned": ("access", "Tarjeta autorizada desde el lector", "ok"),
    "rfid_full": ("access", "Memoria de tarjetas llena", "alerta"),
    "rfid_online": ("device", "Lector RFID en línea", "ok"),
    "rfid_offline": ("device", "Lector RFID sin respuesta", "alerta"),
}


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


# ---------------------------------------------------------------- database
def connect_db() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH, timeout=5)
    conn.row_factory = sqlite3.Row
    return conn


def init_db() -> None:
    global _db_ready
    if _db_ready:
        return
    with db_lock, connect_db() as conn:
        conn.execute("""CREATE TABLE IF NOT EXISTS events (
            id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL, category TEXT NOT NULL,
            modality TEXT, content TEXT NOT NULL, status TEXT NOT NULL, details TEXT NOT NULL DEFAULT '{}')""")
        conn.execute("""CREATE TABLE IF NOT EXISTS state (
            id INTEGER PRIMARY KEY CHECK (id = 1), sensors TEXT NOT NULL, actuators TEXT NOT NULL, updated_at TEXT NOT NULL)""")
        conn.execute("""CREATE TABLE IF NOT EXISTS telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp TEXT NOT NULL, raw TEXT NOT NULL DEFAULT '{}')""")
        existing = {row["name"] for row in conn.execute("PRAGMA table_info(telemetry)")}
        for column, kind in TELEMETRY_TYPES.items():
            if column not in existing:
                conn.execute(f"ALTER TABLE telemetry ADD COLUMN {column} {kind}")
        conn.execute("CREATE INDEX IF NOT EXISTS telemetry_ts ON telemetry(timestamp)")
        conn.execute("""CREATE TABLE IF NOT EXISTS rfid_users (
            uid TEXT PRIMARY KEY, name TEXT NOT NULL, created_at TEXT NOT NULL)""")
        if conn.execute("SELECT 1 FROM state WHERE id=1").fetchone() is None:
            conn.execute("INSERT INTO state VALUES (1, ?, ?, ?)",
                         (json.dumps(DEFAULT_SENSORS), json.dumps(ACTUATOR_DEFAULTS), now_iso()))
    _db_ready = True


def read_state() -> tuple[dict[str, Any], dict[str, Any], str]:
    init_db()
    with db_lock, connect_db() as conn:
        row = conn.execute("SELECT * FROM state WHERE id=1").fetchone()
    stored_s, stored_a = json.loads(row["sensors"]), json.loads(row["actuators"])
    # Ignora claves de versiones anteriores (p. ej. "pressure" o "haptic").
    sensors = {**DEFAULT_SENSORS, **{k: v for k, v in stored_s.items() if k in DEFAULT_SENSORS}}
    actuators = {**ACTUATOR_DEFAULTS, **{k: v for k, v in stored_a.items() if k in ACTUATOR_DEFAULTS}}
    return sensors, actuators, row["updated_at"]


def update_state(sensors: dict[str, Any] | None = None, actuators: dict[str, Any] | None = None) -> None:
    current_s, current_a, _ = read_state()
    current_s.update({k: v for k, v in (sensors or {}).items() if k in DEFAULT_SENSORS})
    current_a.update({k: v for k, v in (actuators or {}).items() if k in ACTUATOR_DEFAULTS})
    with db_lock, connect_db() as conn:
        conn.execute("UPDATE state SET sensors=?, actuators=?, updated_at=? WHERE id=1",
                     (json.dumps(current_s), json.dumps(current_a), now_iso()))


def add_event(category: str, content: str, status: str = "ok", modality: str | None = None,
              details: dict[str, Any] | None = None) -> None:
    init_db()
    with db_lock, connect_db() as conn:
        conn.execute("INSERT INTO events(timestamp,category,modality,content,status,details) VALUES(?,?,?,?,?,?)",
                     (now_iso(), category, modality, content[:500], status, json.dumps(details or {}, ensure_ascii=False)))


def rfid_users() -> list[dict[str, Any]]:
    init_db()
    with db_lock, connect_db() as conn:
        rows = conn.execute("SELECT * FROM rfid_users ORDER BY created_at").fetchall()
    return [dict(r) for r in rows]


def upsert_rfid_user(uid: str, name: str) -> None:
    init_db()
    with db_lock, connect_db() as conn:
        conn.execute("INSERT INTO rfid_users(uid,name,created_at) VALUES(?,?,?) "
                     "ON CONFLICT(uid) DO UPDATE SET name=excluded.name", (uid, name, now_iso()))


# ---------------------------------------------------------------- live (in-memory) device info
class LiveInfo:
    def __init__(self) -> None:
        self.lock = Lock()
        self.control: dict[str, Any] = {"mode": "auto", "alarms": {"gas": False, "door_left_open": False},
                                        "security": {"armed": False, "arming_s": 0, "intrusion": False, "cause": "",
                                                     "siren_muted": False},
                                        "access": {"active": False, "remaining_s": 0}, "manual": {},
                                        "rfid_ok": None, "authorized_cards": None, "learning": False}
        self.braille = {"seq": 0, "mask": 0}
        self.notice = {"seq": 0, "kind": None, "text": None}
        self.rfid_reads: list[dict[str, Any]] = []

    def push_notice(self, kind: str, text: str) -> None:
        with self.lock:
            self.notice = {"seq": self.notice["seq"] + 1, "kind": kind, "text": text}

    def add_rfid_read(self, uid: str, authorized: bool, simulated: bool) -> None:
        with self.lock:
            self.rfid_reads.insert(0, {"uid": uid, "authorized": authorized, "simulated": simulated, "timestamp": now_iso()})
            del self.rfid_reads[10:]

    def push_braille(self, mask: int) -> None:
        with self.lock:
            self.braille = {"seq": self.braille["seq"] + 1, "mask": mask}

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {"control": json.loads(json.dumps(self.control)), "braille": dict(self.braille),
                    "notice": dict(self.notice), "rfid_reads": list(self.rfid_reads)}


live = LiveInfo()


# ---------------------------------------------------------------- serial bridge (ESP32 por USB o Wokwi RFC2217)
class SerialBridge:
    def __init__(self) -> None:
        self._lock = Lock()
        self._stop = threading.Event()
        self._serial = None
        self._thread: threading.Thread | None = None
        self._status: dict[str, Any] = {"connected": False, "state": "disconnected", "url": None, "error": None,
                                        "last_seen": None, "firmware": None}
        self._last_seen_mono = 0.0

    def status(self) -> dict[str, Any]:
        with self._lock:
            status = self._status.copy()
            status["receiving"] = status["connected"] and time.monotonic() - self._last_seen_mono < 5
            return status

    def receiving(self) -> bool:
        return self.status()["receiving"]

    def connect(self, url: str) -> None:
        self.disconnect()
        with self._lock:
            self._stop = threading.Event()
            self._status = {"connected": False, "state": "connecting", "url": url, "error": None,
                            "last_seen": None, "firmware": None}
            self._thread = threading.Thread(target=self._reader, args=(url, self._stop), daemon=True)
            self._thread.start()

    def disconnect(self) -> None:
        self._stop.set()
        with self._lock:
            device, self._serial = self._serial, None
            self._status.update({"connected": False, "state": "disconnected", "error": None})
        if device is not None:
            try:
                device.close()
            except Exception:
                pass

    def send_line(self, line: str) -> bool:
        with self._lock:
            port = self._serial
            if port is None or not self._status.get("connected"):
                return False
            try:
                port.write(f"{line}\n".encode("ascii"))
                return True
            except Exception as exc:
                self._status.update({"connected": False, "state": "error", "error": str(exc)[:240]})
                return False

    def send(self, device: str, value: Any) -> bool:
        if device == "light":
            command_value = str(int(value))
        elif device in {"fan", "siren"}:
            command_value = "on" if value is True else "off"
        else:
            command_value = str(value)
        return self.send_line(f"{device}:{command_value}")

    def sync_cards(self) -> None:
        """La base de datos es la fuente de verdad de las tarjetas autorizadas."""
        self.send_line("auth:clear")
        for user in rfid_users():
            time.sleep(0.03)
            self.send_line(f"auth:add:{user['uid']}")

    def _open(self, url: str):
        import serial
        if url.startswith(("rfc2217://", "socket://")):  # Wokwi o ESP32 por Wi-Fi
            return serial.serial_for_url(url, baudrate=115200, timeout=1)
        # Puerto USB o Bluetooth: DTR/RTS desactivados para no reiniciar el ESP32 ni dejarlo en modo de carga.
        device = serial.serial_for_url(url, baudrate=115200, timeout=1, do_not_open=True)
        device.dtr = False
        device.rts = False
        device.open()
        return device

    def _reader(self, url: str, stop: threading.Event) -> None:
        try:
            import serial  # noqa: F401
        except ImportError:
            with self._lock:
                self._status.update({"state": "error", "error": "Falta pyserial. Ejecuta pip install -r requirements.txt."})
            return
        while not stop.is_set():
            device = None
            try:
                device = self._open(url)
                with self._lock:
                    if stop.is_set():
                        device.close()
                        break
                    self._serial = device
                    self._status.update({"connected": True, "state": "connected", "error": None})
                add_event("device", f"Conectado a {url}")
                self.sync_cards()
                self.send_line("status")
                buffer = b""
                while not stop.is_set():
                    chunk = device.readline()
                    if not chunk:
                        continue
                    buffer += chunk
                    if not buffer.endswith(b"\n"):
                        continue
                    line, buffer = buffer.decode("utf-8", errors="ignore").strip(), b""
                    if line:
                        try:
                            self._handle_line(line)
                        except Exception as exc:  # una línea mala no debe tumbar el puente
                            print(f"[puente] línea ignorada ({exc}): {line[:120]}")
            except Exception as exc:
                with self._lock:
                    if self._stop is stop:
                        self._status.update({"connected": False, "state": "connecting", "error": str(exc)[:240]})
            finally:
                with self._lock:
                    if self._serial is device:
                        self._serial = None
                        self._status["connected"] = False
                if device is not None:
                    try:
                        device.close()
                    except Exception:
                        pass
            stop.wait(2)
        with self._lock:
            if self._stop is stop:  # no pisar el estado de una conexión nueva
                self._status.update({"connected": False, "state": "disconnected", "error": None})

    def _handle_line(self, line: str) -> None:
        try:
            payload = json.loads(line)
        except (json.JSONDecodeError, TypeError):
            return  # mensajes de arranque del ESP32 u otro texto libre
        if not isinstance(payload, dict):
            return
        kind = payload.get("type")
        with self._lock:
            self._last_seen_mono = time.monotonic()
            self._status["last_seen"] = now_iso()
        if kind == "telemetry":
            self._handle_telemetry(payload)
        elif kind == "event":
            name = str(payload.get("event", ""))
            category, text, status = EVENT_TEXT.get(name, ("device", name, "ok"))
            detail = str(payload.get("detail", ""))
            add_event(category, f"{text} ({detail})" if detail else text, status=status,
                      details={"event": name, "detail": detail, "source": "esp32"})
            if name in {"doorbell", "gas_alarm_on", "door_forced", "access_granted", "access_denied", "intrusion",
                        "security_armed", "security_disarmed", "security_arm_failed"}:
                live.push_notice(name, text)
        elif kind == "braille" and isinstance(payload.get("mask"), int):
            mask = payload["mask"] & 0x3F
            live.push_braille(mask)
            dots = [d for d in range(1, 7) if mask & (1 << (d - 1))]
            add_event("braille", "Puntos: " + ",".join(map(str, dots)), modality="braille",
                      details={"dots": dots, "source": "teclado físico"})
        elif kind == "rfid":
            uid = str(payload.get("uid", "")).upper()
            if UID_RE.match(uid):
                live.add_rfid_read(uid, bool(payload.get("authorized")), bool(payload.get("simulated")))
        elif kind == "ack" and not payload.get("ok", True):
            add_event("command", f"Comando rechazado por el ESP32: {payload.get('cmd')}", status="rechazado",
                      details={"note": payload.get("note")})
        elif kind == "ack" and "alarma" in str(payload.get("note", "")):
            add_event("command", f"Comando en espera por alarma: {payload.get('cmd')}", status="bloqueado")
        elif kind == "auth_list" and isinstance(payload.get("uids"), list):
            known = {u["uid"] for u in rfid_users()}
            for uid in payload["uids"]:
                uid = str(uid).upper()
                if UID_RE.match(uid) and uid not in known:
                    upsert_rfid_user(uid, "Aprendida en el lector")
            with live.lock:
                live.control["authorized_cards"] = len(payload["uids"])
        elif kind == "ready":
            with self._lock:
                self._status["firmware"] = payload.get("firmware")
            ip = f", IP {payload['ip']}" if payload.get("ip") else ""
            add_event("device", f"ESP32 iniciado ({payload.get('firmware')}, canales {payload.get('channels', 'usb')}{ip}), "
                                f"RFID {'OK' if payload.get('rfid_ok') else 'sin respuesta'}")
            threading.Thread(target=self.sync_cards, daemon=True).start()

    def _handle_telemetry(self, payload: dict[str, Any]) -> None:
        sensors_in = payload.get("sensors") or {}
        sensors: dict[str, Any] = {}
        for key in ("temperature_c", "setpoint_c", "light_lux", "gas_pct", "brightness_pct"):
            value = sensors_in.get(key)
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                sensors[key] = float(value)
            elif key == "temperature_c" and value is None:
                sensors[key] = None  # sensor desconectado
        for key in ("door_open", "contact"):
            if isinstance(sensors_in.get(key), bool):
                sensors[key] = sensors_in[key]
        actuators_in = payload.get("actuators") or {}
        actuators: dict[str, Any] = {}
        for key in ("light", "curtain_pct"):
            if isinstance(actuators_in.get(key), int) and not isinstance(actuators_in.get(key), bool):
                actuators[key] = max(0, min(100, actuators_in[key]))
        for key in ("fan", "siren", "bell"):
            if isinstance(actuators_in.get(key), bool):
                actuators[key] = actuators_in[key]
        for key, valid in (("curtain", {"open", "closed"}), ("window", {"open", "closed"}), ("latch", {"locked", "unlocked"})):
            if actuators_in.get(key) in valid:
                actuators[key] = actuators_in[key]
        update_state(sensors=sensors, actuators=actuators)
        with live.lock:
            for key in ("mode", "dip_auto", "alarms", "access", "security", "manual", "rfid_ok", "authorized_cards",
                        "learning", "channels", "ip"):
                if key in payload:
                    live.control[key] = payload[key]
            live.control["curtain_moving"] = bool(actuators_in.get("curtain_moving"))
        s, a, _ = read_state()
        alarms = payload.get("alarms") or {}
        access = payload.get("access") or {}
        row = {"timestamp": now_iso(), "mode": payload.get("mode"), **{k: s.get(k) for k in DEFAULT_SENSORS},
               "light": a["light"], "fan": int(bool(a["fan"])), "curtain": a["curtain"], "curtain_pct": a["curtain_pct"],
               "window": a["window"], "latch": a["latch"], "siren": int(bool(a["siren"])),
               "gas_alarm": int(bool(alarms.get("gas"))), "access_active": int(bool(access.get("active"))),
               "armed": int(bool((payload.get("security") or {}).get("armed"))),
               "intrusion": int(bool((payload.get("security") or {}).get("intrusion")))}
        row["door_open"] = int(bool(row["door_open"]))
        row["contact"] = int(bool(row["contact"]))
        columns = list(row) + ["raw"]
        with db_lock, connect_db() as conn:
            conn.execute(f"INSERT INTO telemetry({','.join(columns)}) VALUES({','.join('?' * len(columns))})",
                         [*row.values(), json.dumps(payload.get("raw") or {})])


bridge = SerialBridge()


# ---------------------------------------------------------------- routes
def full_state() -> dict[str, Any]:
    sensors, actuators, updated_at = read_state()
    snap = live.snapshot()
    hardware = bridge.receiving()
    return {"sensors": sensors, "actuators": actuators, "updated_at": updated_at,
            "mode": "hardware" if hardware else "simulation", "sources": SENSOR_SOURCES if hardware else None,
            "control": snap["control"], "braille": snap["braille"], "notice": snap["notice"],
            "rfid_reads": snap["rfid_reads"],
            "bridge": bridge.status()}


@app.get("/")
def index():
    return send_from_directory(app.static_folder, "index.html")


@app.get("/api/state")
def api_state():
    return jsonify(full_state())


@app.get("/api/ports")
def api_ports():
    ports = []
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            kind = "bluetooth" if "bluetooth" in p.description.lower() or p.device.startswith("BTHENUM") else "usb"
            ports.append({"device": p.device, "description": p.description, "kind": kind})
    except ImportError:
        pass
    ports.append({"device": "socket://casa-multimodal.local:3333", "description": "ESP32 por Wi-Fi (o socket://IP:3333)", "kind": "wifi"})
    ports.append({"device": "rfc2217://127.0.0.1:4000", "description": "Simulador Wokwi (VS Code)", "kind": "wokwi"})
    return jsonify(ports)


@app.get("/api/bridge")
def api_bridge_status():
    return jsonify(bridge.status())


@app.post("/api/bridge/connect")
def api_bridge_connect():
    payload = request.get_json(silent=True) or {}
    url = str(payload.get("url", "rfc2217://127.0.0.1:4000")).strip()
    if not PORT_RE.match(url):
        return jsonify(error="Usa un puerto COM (USB o Bluetooth), socket://IP:3333 (Wi-Fi) o rfc2217://127.0.0.1:4000 (Wokwi)"), 400
    bridge.connect(url)
    return jsonify(bridge.status()), 202


@app.post("/api/bridge/disconnect")
def api_bridge_disconnect():
    bridge.disconnect()
    return jsonify(bridge.status())


@app.post("/api/sensors")
def api_sensors():
    if bridge.receiving():
        return jsonify(error="Las entradas vienen del ESP32: usa los potenciómetros y sensores de la maqueta."), 409
    payload = request.get_json(silent=True) or {}
    sensors, _, _ = read_state()
    for key, (low, high) in SENSOR_LIMITS.items():
        if key in payload:
            try:
                value = float(payload[key])
            except (TypeError, ValueError):
                return jsonify(error=f"{key} debe ser numérico"), 400
            if not low <= value <= high:
                return jsonify(error=f"{key} fuera de rango ({low}–{high})"), 400
            sensors[key] = value
    for key in ("door_open", "contact"):
        if key in payload:
            if not isinstance(payload[key], bool):
                return jsonify(error=f"{key} debe ser booleano"), 400
            sensors[key] = payload[key]
    update_state(sensors=sensors)
    with live.lock:  # simulación del modo seguridad
        security = live.control["security"]
        cause = "puerta" if sensors["door_open"] else "contacto en marco metálico" if sensors["contact"] else ""
        trigger = security["armed"] and cause and not security["intrusion"]
        if trigger:
            security.update({"intrusion": True, "cause": cause, "siren_muted": False})
    if trigger:
        update_state(actuators={"siren": True})
        add_event("alarm", f"INTRUSIÓN detectada ({cause}, simulación)", status="alerta")
        live.push_notice("intrusion", f"INTRUSIÓN detectada ({cause})")
    add_event("sensor", "Entradas simuladas actualizadas", details=sensors)
    return jsonify(full_state())


@app.post("/api/commands")
def api_command():
    payload = request.get_json(silent=True) or {}
    name, value = payload.get("device"), payload.get("value")
    if name == "bell":  # tocar el timbre desde la web
        if not bridge.send_line("bell:ring"):
            live.push_notice("doorbell", "Timbre (simulación)")
        add_event("device", "Timbre tocado desde la web", status="aviso", modality=payload.get("modality"))
        return jsonify(full_state())
    if name not in COMMANDS:
        return jsonify(error="Actuador no reconocido"), 400
    rule = COMMANDS[name]
    if rule == "0-100":
        if not isinstance(value, (int, float)) or isinstance(value, bool) or not 0 <= value <= 100:
            return jsonify(error=f"{name} requiere un nivel entre 0 y 100"), 400
        value = int(value)
    elif rule == "bool":
        if not isinstance(value, bool):
            return jsonify(error=f"{name} requiere true o false"), 400
    elif value not in rule:
        return jsonify(error=f"{name} requiere {' o '.join(sorted(rule))}"), 400
    forwarded = bridge.send(name, value)
    if not forwarded:
        changes = {name: value}
        if name == "curtain":
            changes["curtain_pct"] = 100 if value == "open" else 0
        update_state(actuators=changes)  # sin ESP32: se actualiza la simulación
    add_event("actuation", f"{name}: {value}", modality=payload.get("modality"),
              details={"device": name, "value": value, "forwarded": forwarded})
    return jsonify(full_state())


@app.post("/api/security")
def api_security():
    armed = (request.get_json(silent=True) or {}).get("armed")
    if not isinstance(armed, bool):
        return jsonify(error="armed debe ser true o false"), 400
    if bridge.send_line(f"security:{'arm' if armed else 'disarm'}"):
        return jsonify(full_state())
    sensors, _, _ = read_state()
    if armed and sensors["door_open"]:
        return jsonify(error="Cierra la puerta para activar la seguridad"), 409
    with live.lock:
        live.control["security"] = {"armed": armed, "arming_s": 0, "intrusion": False, "cause": "", "siren_muted": False}
    if not armed:
        update_state(actuators={"siren": False})
    add_event("security", f"Modo seguridad {'ACTIVADO' if armed else 'desactivado'} (simulación)")
    live.push_notice("security_armed" if armed else "security_disarmed", f"Modo seguridad {'activado' if armed else 'desactivado'}")
    return jsonify(full_state())


@app.post("/api/alarm/silence")
def api_alarm_silence():
    if not bridge.send_line("alarm:silence"):
        update_state(actuators={"siren": False})
        with live.lock:
            live.control["security"]["siren_muted"] = live.control["security"].get("intrusion", False)
        add_event("alarm", "Sirena silenciada (simulación)")
    return jsonify(full_state())


@app.post("/api/mode")
def api_mode():
    mode = (request.get_json(silent=True) or {}).get("mode")
    if mode not in {"auto", "manual"}:
        return jsonify(error="mode debe ser auto o manual"), 400
    if not bridge.send_line(f"mode:{mode}"):
        with live.lock:
            live.control["mode"] = mode
        add_event("control", f"Modo {'automático' if mode == 'auto' else 'manual'} (simulación)")
    return jsonify(full_state())


@app.post("/api/messages")
def api_message():
    payload = request.get_json(silent=True) or {}
    content, modality = payload.get("content"), payload.get("modality")
    if not isinstance(content, str) or not content.strip():
        return jsonify(error="content no puede estar vacío"), 400
    if modality not in {"text", "voice", "braille", "sign"}:
        return jsonify(error="modalidad no reconocida"), 400
    add_event("message", content.strip(), modality=modality,
              details={"confidence": payload.get("confidence"), "intent": payload.get("intent")})
    return jsonify({"ok": True, "timestamp": now_iso(), "message": content.strip()}), 201


@app.post("/api/braille")
def api_braille():
    dots = (request.get_json(silent=True) or {}).get("dots")
    if not isinstance(dots, list) or any(dot not in range(1, 7) for dot in dots):
        return jsonify(error="dots debe ser una lista de puntos entre 1 y 6"), 400
    normalized = sorted(set(dots))
    add_event("braille", "Puntos: " + ",".join(map(str, normalized)), modality="braille", details={"dots": normalized})
    return jsonify({"ok": True, "dots": normalized, "timestamp": now_iso()}), 201


# ---- RFID
@app.get("/api/rfid/users")
def api_rfid_users():
    return jsonify(rfid_users())


@app.post("/api/rfid/users")
def api_rfid_add():
    payload = request.get_json(silent=True) or {}
    uid = str(payload.get("uid", "")).strip().upper()
    name = str(payload.get("name", "")).strip()[:60] or "Sin nombre"
    if not UID_RE.match(uid):
        return jsonify(error="UID inválido: usa de 4 a 20 caracteres hexadecimales"), 400
    upsert_rfid_user(uid, name)
    bridge.send_line(f"auth:add:{uid}")
    add_event("access", f"Tarjeta {uid} autorizada ({name})")
    return jsonify(rfid_users()), 201


@app.delete("/api/rfid/users/<uid>")
def api_rfid_delete(uid: str):
    uid = uid.upper()
    if not UID_RE.match(uid):
        return jsonify(error="UID inválido"), 400
    with db_lock, connect_db() as conn:
        conn.execute("DELETE FROM rfid_users WHERE uid=?", (uid,))
    bridge.send_line(f"auth:del:{uid}")
    add_event("access", f"Tarjeta {uid} eliminada")
    return jsonify(rfid_users())


@app.post("/api/rfid/learn")
def api_rfid_learn():
    if not bridge.send_line("auth:learn"):
        return jsonify(error="Conecta el ESP32 para autorizar desde el lector"), 409
    return jsonify({"ok": True}), 202


@app.post("/api/rfid/simulate")
def api_rfid_simulate():
    uid = str((request.get_json(silent=True) or {}).get("uid", "")).strip().upper()
    if not UID_RE.match(uid):
        return jsonify(error="UID inválido"), 400
    if bridge.send_line(f"rfid:{uid}"):
        return jsonify({"ok": True, "forwarded": True}), 202
    authorized = uid in {u["uid"] for u in rfid_users()}
    live.add_rfid_read(uid, authorized, True)
    if authorized:  # simulación: el pestillo se abre 5 s
        update_state(actuators={"latch": "unlocked"})
        threading.Timer(5, lambda: update_state(actuators={"latch": "locked"})).start()
    add_event("access", f"{'Acceso concedido' if authorized else 'Acceso denegado'} ({uid}, simulación)",
              status="ok" if authorized else "alerta")
    return jsonify({"ok": True, "forwarded": False, "authorized": authorized})


# ---- history and data export
@app.get("/api/events")
def api_events():
    try:
        limit = max(1, min(200, int(request.args.get("limit", "100"))))
    except ValueError:
        return jsonify(error="limit debe ser entero"), 400
    init_db()
    with db_lock, connect_db() as conn:
        rows = conn.execute("SELECT * FROM events ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
    return jsonify([{"id": r["id"], "timestamp": r["timestamp"], "category": r["category"], "modality": r["modality"],
                     "content": r["content"], "status": r["status"], "details": json.loads(r["details"])} for r in rows])


def telemetry_rows(minutes: int) -> list[sqlite3.Row]:
    init_db()
    since = (datetime.now(timezone.utc) - timedelta(minutes=minutes)).isoformat(timespec="milliseconds")
    with db_lock, connect_db() as conn:
        return conn.execute(f"SELECT {','.join(TELEMETRY_FIELDS)} FROM telemetry WHERE timestamp >= ? ORDER BY id",
                            (since,)).fetchall()


def parse_minutes() -> int:
    try:
        return max(1, min(60 * 24 * 30, int(request.args.get("minutes", "10"))))
    except ValueError:
        return 10


@app.get("/api/telemetry")
def api_telemetry():
    return jsonify([dict(r) for r in telemetry_rows(parse_minutes())])


@app.get("/api/telemetry.csv")
def api_telemetry_csv():
    out = io.StringIO()
    writer = csv.writer(out)
    writer.writerow(TELEMETRY_FIELDS)
    writer.writerows([list(r) for r in telemetry_rows(parse_minutes())])
    return Response(out.getvalue(), mimetype="text/csv",
                    headers={"Content-Disposition": "attachment; filename=telemetria.csv"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--serial", help="Conectar al iniciar, p. ej. COM3 o rfc2217://127.0.0.1:4000")
    args = parser.parse_args()
    init_db()
    if args.serial:
        bridge.connect(args.serial)
    app.run(host=args.host, port=args.port, debug=False, threaded=True)
