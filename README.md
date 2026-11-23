# Casa inteligente multimodal

Prototipo de maqueta domótica con interfaz multimodal (texto, voz, braille simulado y cámara). Incluye:

- **Firmware ESP32** (`firmware/`): lee los sensores, aplica las reglas de control y mueve los actuadores.
- **Servicio Flask + SQLite** (`server.py`): comunicación con el ESP32 por **USB, Bluetooth o Wi-Fi** (o con Wokwi), API, historial y exportación de datos.
- **Aplicación web** (`web/`): comunicación multimodal, control de la casa, RFID e historial. Funciona en el computador y tiene una **vista celular** para simular el teléfono.
- **Guía de montaje con diagramas** (`docs/montaje.html`): ábrela en el navegador, funciona sin internet y se puede imprimir.
- **Construcción de la maqueta** (`docs/maqueta.html` + `maqueta/`): modelo 3D paramétrico en OpenSCAD, planos de corte 1:1 en SVG para madera, piezas STL para imprimir y paso a paso.

## Requisitos de la guía y cómo se cumplen

| Requisito | Implementación |
|---|---|
| ≥ 4 entradas | **Reales:** temperatura (KY-028), puerta (reed switch), contacto en marcos metálicos (KY-036), RFID, botón de timbre y DIP switch. **Simuladas con potenciómetro:** luz ambiente y gas. **Ajustes:** consigna del termostato y brillo |
| Procesamiento | Reglas en el ESP32 con histéresis: alarma de gas, termostato de dos etapas (ventilador y ventana), luz automática, cortina por luz, control de acceso, detección de puerta forzada y **modo seguridad** (alarma de intrusión). Modo automático o manual, con prioridad manual temporal |
| ≥ 4 salidas | Ventilador (relé), cortina (motor paso a paso), pestillo y ventana (servos), sirena y timbre (buzzers), luz (LED) |
| Frontend | Aplicación web/PWA con vista celular |
| Base de datos | SQLite: `events`, `telemetry` (una fila por segundo), `rfid_users`, `state` |

## 1. Montaje físico

Sigue **`docs/montaje.html`**. Tiene un diagrama y una tabla de conexiones por componente, el mapa de pines de la placa de 30 pines, el orden de montaje recomendado y los problemas frecuentes. Resumen:

| Componente | ESP32 | Alimentación |
|---|---|---|
| KY-028 (A0) | D32 | **3.3 V** |
| Potenciómetros: luz, gas, consigna, brillo (centro) | D33, D34, D35, VP (36) | Extremos a **3.3 V** y GND |
| KY-036 contacto (D0); su punta metálica va con un cable a los marcos | VN (39) | 3.3 V |
| Reed switch (puerta) | D15 y GND | — |
| Botón timbre / DIP 1 (modo) | D21 / D22, y GND | — |
| RFID RC522: SDA, SCK, MOSI, MISO | D5, D18, D23, D19 | **3.3 V**; RST a 3.3 V |
| Relé CW-020 (IN) | D4 | 5 V de la fuente |
| ULN2003: IN1, IN2, IN3, IN4 | D13, D12, D14, D27 | 5 V de la fuente |
| Servo pestillo / ventana (naranja) | D25 / D26 | 5 V de la fuente |
| Buzzer alarma / timbre (+) | RX2 (16) / TX2 (17) | − a GND |
| Luz (LED de la placa + LED externo opcional) | D2 | — |

**GND común:** el GND de la fuente de 5 V se une con el GND del ESP32. El + de la fuente nunca va al ESP32.

## 2. Probar cada componente

Con **Arduino IDE**, abre `firmware/arduino/pruebas/pruebas.ino`, elige la placa **ESP32 Dev Module** y el puerto COM del ESP32, y súbelo. Librerías necesarias: **ESP32Servo** y **MFRC522**.
Con **PlatformIO**: `pio run -e pruebas -t upload` y luego `pio device monitor`.

En el monitor serie (115200 baudios):

| Opción | Prueba |
|---|---|
| 1 | Luz (rampa de brillo en GPIO2) |
| 2 | Relé + ventilador |
| 3 / 4 | Servo del pestillo / de la ventana |
| 5 | Motor paso a paso (cortina) |
| 6 | Buzzers (sirena y ding-dong) |
| 7 | Potenciómetros + temperatura KY-028 |
| 8 | Puerta (reed), contacto KY-036, botón y DIP |
| 9 | RFID (muestra los UID) |
| 0 | Todas las salidas en secuencia |
| p / v / c | Calibrar pestillo / ventana / cortina |

Los valores calibrados se ponen en `firmware/src/pines.h`: `TIEMPO_PESTILLO_MS`, `TIEMPO_VENTANA_MS`, `PASOS_CORTINA` y las opciones `*_INVERTIR`. Luego ejecuta `python firmware/sincronizar_arduino.py` para actualizar las carpetas de Arduino IDE. **Edita siempre `src/`**, porque las copias de `arduino/` se sobrescriben.

**Al encender**, el pestillo, la ventana y la cortina deben estar **cerrados**: los servos continuos y el motor paso a paso cuentan su posición desde ahí.

## 3. Firmware principal y comunicación

Carga `firmware/arduino/casa_multimodal/casa_multimodal.ino` (o `pio run -e esp32dev -t upload`). Luego ejecuta:

```powershell
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python server.py
```

Abre `http://127.0.0.1:5000` → **Mi casa** → elige el puerto → **Conectar**.

| Canal | Cómo conectarlo |
|---|---|
| **USB** | El puerto COM del CH9102 (por ejemplo `COM7`) |
| **Bluetooth** (activo por defecto) | En Windows: *Configuración → Bluetooth → Agregar dispositivo* → **CasaMultimodal**. Luego, en *Más opciones de Bluetooth → Puertos COM*, usa el puerto **saliente** de CasaMultimodal |
| **Wi-Fi** | En `pines.h`: `COM_BLUETOOTH 0`, `COM_WIFI 1`, `WIFI_SSID` y `WIFI_CLAVE` (red de 2.4 GHz; el hotspot del computador sirve). Conecta la web a `socket://IP:3333`. La IP aparece en el historial al arrancar el ESP32 |
| **Wokwi** | `rfc2217://127.0.0.1:4000` |

> El puerto COM solo puede usarlo un programa a la vez: **cierra el monitor serie** de Arduino IDE antes de conectar desde la web.

**Vista celular:** el botón *▯ Vista celular* de la barra superior muestra la app con el tamaño de un teléfono, para simular el celular desde el computador.

### RFID
1. Acerca una tarjeta: aparece en **Últimas lecturas** como *Denegado* (biiip).
2. Pulsa **Autorizar** en esa fila y escribe un nombre.
3. Vuelve a acercarla: bip-bip y el pestillo se abre 5 s.

Las tarjetas autorizadas quedan en la base de datos y en la memoria del ESP32, así que el acceso funciona aunque el computador esté apagado.

## 4. Reglas de control (en el ESP32)

| Regla | Activa | Desactiva |
|---|---|---|
| Alarma de gas: sirena, ventilador, ventana y pestillo abiertos, aviso por voz | gas ≥ 60 % | gas < 45 % |
| Termostato, etapa 1: ventilador | T ≥ consigna + 1 °C | T ≤ consigna − 1 °C |
| Termostato, etapa 2: ventana | T ≥ consigna + 3 °C | T ≤ consigna + 1 °C |
| Luz al brillo del potenciómetro | luz ambiente < 200 lux | > 300 lux |
| Cortina | luz ambiente > 600 lux → abre | < 150 lux → cierra |
| Acceso RFID | tarjeta autorizada → pestillo 5 s + bip-bip (y desactiva la seguridad) | al cerrar la puerta |
| **Modo seguridad** | se activa desde la web ("activar seguridad"), con 10 s para salir; no se activa con la puerta abierta. Activado: **contacto en los marcos (KY-036)** o **puerta abierta** → intrusión, sirena y aviso por voz | web o tarjeta RFID autorizada |
| Puerta forzada | reed abierto con pestillo cerrado → sirena 3 s | — |
| Puerta abierta tras el acceso | 15 s → bip cada 2 s | al cerrar |
| Timbre | botón → ding-dong y aviso por voz en la web | — |
| Modo | DIP 1 ON = automático, OFF = manual (la web también lo cambia) | — |

En modo manual solo actúan las órdenes de la persona. La alarma de gas **siempre** actúa y bloquea los comandos de ventilador, ventana y pestillo. La sirena se puede silenciar desde la web. Los umbrales están al inicio de `firmware/src/main.cpp`.

## 5. Datos para el artículo

- `GET /api/telemetry.csv?minutes=60` (botón **Exportar telemetría CSV** en *Actividad*): una fila por segundo con temperatura, consigna, luz, gas, puerta, estado de cada actuador, modo y alarmas.
- La tabla `events` registra mensajes multimodales, órdenes, eventos de las reglas automáticas, accesos, timbre y alarmas.

## 6. Simulación en Wokwi

`pio run -e wokwi` y luego inicia Wokwi en VS Code (carpeta `firmware`). El diagrama replica el montaje: sensor NTC en lugar del KY-028, interruptores deslizantes para el reed y el DIP, y un botón con resistencia en lugar del KY-036. En Wokwi los servos son de 180° y no hay lector RFID; para el RFID usa **Simular lectura del UID** en la web.

## Protocolo (USB, Bluetooth y Wi-Fi usan el mismo)

- **ESP32 → PC:** una línea JSON por mensaje con `type`: `ready`, `telemetry` (cada 1 s), `event`, `rfid`, `ack` o `auth_list`.
- **PC → ESP32:** `light:0-100`, `fan:on|off`, `curtain:open|closed`, `window:open|closed`, `latch:locked|unlocked`, `siren:on|off`, `bell:ring`, `alarm:silence`, `mode:auto|manual`, `security:arm|disarm`, `rfid:<UID>`, `auth:add|del:<UID>`, `auth:clear`, `auth:list`, `auth:learn` y `status`.

## Rutas API

`GET /api/state` · `GET /api/ports` · `POST /api/bridge/connect` · `POST /api/bridge/disconnect` · `POST /api/sensors` (solo en simulación) · `POST /api/commands` · `POST /api/alarm/silence` · `POST /api/security` · `POST /api/mode` · `POST /api/messages` · `POST /api/braille` · `GET|POST /api/rfid/users` · `DELETE /api/rfid/users/<uid>` · `POST /api/rfid/learn` · `POST /api/rfid/simulate` · `GET /api/events` · `GET /api/telemetry` · `GET /api/telemetry.csv`
