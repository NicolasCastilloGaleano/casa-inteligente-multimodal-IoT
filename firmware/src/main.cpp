// Firmware principal de la casa multimodal (ESP32 físico o Wokwi).
//
// Protocolo (115200 baudios por USB, o el mismo texto por Bluetooth / Wi-Fi), una línea por mensaje:
//   ESP32 -> PC : JSON con "type": ready | telemetry | event | rfid | ack | auth_list
//   PC -> ESP32 : texto "dispositivo:valor"
//       light:0-100  fan:on|off  curtain:open|closed  latch:locked|unlocked
//       siren:on|off (prueba)  bell:ring  alarm:silence  mode:auto|manual  security:arm|disarm
//       rfid:<UID> (simula una lectura)  auth:add:<UID> | auth:del:<UID> | auth:clear | auth:list | auth:learn
//       status
//
// Reglas de control:
//   - Gas >= 60 %       -> ALARMA: sirena, ventilador y pestillo abierto. Se apaga con gas < 45 %.
//                          Bloquea las órdenes manuales de ventilador y pestillo.
//   - Termostato        -> ventilador ON con T >= consigna + 1 °C, OFF con T <= consigna - 1 °C.
//   - Luz               -> enciende al brillo del potenciómetro con luz ambiente < 200 lux, apaga con > 300 lux.
//   - Cortina (paso a paso) -> abre con luz ambiente > 600 lux, cierra con < 150 lux.
//   - RFID              -> tarjeta autorizada: pestillo abierto 5 s y bip-bip. No autorizada: biiip.
//   - Puerta (reed)     -> abierta con el pestillo cerrado = puerta forzada (sirena 3 s).
//                          Abierta más de 15 s después de un acceso = aviso sonoro.
//   - Timbre (botón)    -> "ding-dong" y aviso en la web.
//   - Modo seguridad    -> se activa desde la web (10 s para salir; no se activa con la puerta abierta).
//                          Activado: tocar un marco metálico (KY-036) o abrir la puerta (reed) = INTRUSIÓN:
//                          sirena hasta desactivar (web o tarjeta RFID autorizada) o silenciar.
//   - DIP 1             -> modo automático (ON) o manual (OFF). La web también puede cambiar el modo.
//   En modo automático, una orden manual tiene prioridad 60 s sobre ese dispositivo.
#include <Arduino.h>
#include <ESP32Servo.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <SPI.h>
#include "pines.h"
#include "comunicacion.h"
#include "motor_paso.h"
#include "motor_servo.h"
#include "zumbador.h"

// ---------- Parámetros de control ----------
constexpr float GAS_ALARMA_ON = 60.0f, GAS_ALARMA_OFF = 45.0f;
constexpr float VENT_SOBRE_CONSIGNA = 1.0f, VENT_BAJO_CONSIGNA = 1.0f;
constexpr float LUX_LUZ_ON = 200.0f, LUX_LUZ_OFF = 300.0f;
constexpr float LUX_CORTINA_ABRIR = 600.0f, LUX_CORTINA_CERRAR = 150.0f;
constexpr unsigned long ACCESO_MS = 5000;
constexpr unsigned long PUERTA_ABIERTA_ALERTA_MS = 15000;
constexpr unsigned long PUERTA_FORZADA_MS = 3000;
constexpr unsigned long PRIORIDAD_MANUAL_MS = 60000;
constexpr unsigned long TELEMETRIA_MS = 1000;
constexpr unsigned long RETARDO_ARMADO_MS = 10000;  // tiempo para salir después de activar la seguridad
constexpr uint8_t MAX_UIDS = 20;

enum Dispositivo { D_LUZ, D_VENT, D_CORTINA, D_TOTAL };

MotorServo servoPestillo(PIN_SERVO_PESTILLO, PESTILLO_CONTINUO, ANGULO_PESTILLO_CERRADO, ANGULO_PESTILLO_ABIERTO,
                         TIEMPO_PESTILLO_MS, PESTILLO_INVERTIR, VELOCIDAD_SERVO_US);
MotorPaso cortina(PIN_PASO, PASOS_CORTINA, US_POR_PASO, CORTINA_INVERTIR);
Zumbador sirena(PIN_BUZZER_ALARMA);
Zumbador timbre(PIN_BUZZER_TIMBRE);
MFRC522 rfid(PIN_RFID_SS, MFRC522::UNUSED_PIN);  // RST del módulo conectado a 3V3
Preferences preferencias;

// ---------- Entradas con antirrebote ----------
struct EntradaDigital {
  bool estable = false, ultimo = false;
  unsigned long cambio = 0;
  // Devuelve true cuando el valor estable cambia.
  bool actualizar(bool crudo, unsigned long ms = 40) {
    if (crudo != ultimo) { ultimo = crudo; cambio = millis(); }
    if (crudo != estable && millis() - cambio >= ms) { estable = crudo; return true; }
    return false;
  }
};
EntradaDigital entPuerta, entContacto, entTimbre, entDip;

// ---------- Estado ----------
String uids[MAX_UIDS];
uint8_t totalUids = 0;
bool modoAprender = false, rfidOk = false;

int rawLuz = 0, rawGas = 0, rawConsigna = 0, rawBrillo = 0;
float temperatura = NAN, luxAmbiente = 0, gasPct = 0, consigna = 24, brilloPct = 80;
bool puertaAbierta = false, dipAuto = true;

bool modoAuto = true, alarmaGas = false, sirenaSilenciada = false, sirenaManual = false;
bool autoVent = false, autoLuz = false, autoCortina = false;
int manualNivelLuz = 0;
bool manualVent = false, manualCortina = false, manualPestillo = false;
unsigned long prioridadHasta[D_TOTAL] = {0, 0, 0};
bool sensorTempOk = true;

bool accesoActivo = false, alertaPuertaAbierta = false, avisoSonando = false;
// Modo seguridad
bool seguridadArmada = false, armando = false, intrusion = false, sirenaIntrusionSilenciada = false, beepArmando = false;
unsigned long armadoInicio = 0;
String causaIntrusion;
unsigned long accesoInicio = 0, puertaForzadaHasta = 0, ultimoTimbre = 0;

int salidaLuz = -1, salidaVent = -1, salidaCortina = -1, salidaPestillo = -1;
unsigned long ultimaTelemetria = 0, ultimoMuestreo = 0, ultimoRfid = 0, ultimaRevisionRfid = 0;
String ultimoUid; unsigned long ultimoUidMs = 0;

// ---------- Utilidades ----------
const char *bstr(bool v) { return v ? "true" : "false"; }
bool prioridadManual(Dispositivo d) { return !modoAuto || millis() < prioridadHasta[d]; }
void evento(const char *nombre, const String &detalle = "") {
  com.enviar("{\"type\":\"event\",\"event\":\"%s\",\"detail\":\"%s\"}\n", nombre, detalle.c_str());
}
void ack(String cmd, bool ok, const char *nota = "") {
  cmd.replace("\\", "/");
  cmd.replace("\"", "'");
  com.enviar("{\"type\":\"ack\",\"cmd\":\"%s\",\"ok\":%s,\"note\":\"%s\"}\n", cmd.c_str(), bstr(ok), nota);
}
float mapear(int raw, float minimo, float maximo) { return minimo + raw * (maximo - minimo) / 4095.0f; }
void desactivarSeguridad(const String &origen);

// ---------- Tarjetas autorizadas (memoria no volátil) ----------
void guardarUids() {
  String todo;
  for (uint8_t i = 0; i < totalUids; i++) { if (i) todo += ','; todo += uids[i]; }
  preferencias.putString("uids", todo);
}
void cargarUids() {
  String todo = preferencias.getString("uids", "");
  totalUids = 0;
  int inicio = 0;
  while (inicio < (int)todo.length() && totalUids < MAX_UIDS) {
    int coma = todo.indexOf(',', inicio);
    if (coma < 0) coma = todo.length();
    String uid = todo.substring(inicio, coma);
    if (uid.length()) uids[totalUids++] = uid;
    inicio = coma + 1;
  }
}
int buscarUid(const String &uid) {
  for (uint8_t i = 0; i < totalUids; i++) if (uids[i] == uid) return i;
  return -1;
}
bool uidValido(const String &uid) {
  if (uid.length() < 4 || uid.length() > 20) return false;
  for (char c : uid) if (!isxdigit(c)) return false;
  return true;
}
void listarUids() {
  String lista;
  for (uint8_t i = 0; i < totalUids; i++) { if (i) lista += ','; lista += '"' + uids[i] + '"'; }
  com.enviar("{\"type\":\"auth_list\",\"uids\":[%s]}\n", lista.c_str());
}
bool agregarUid(const String &uid) {
  if (buscarUid(uid) >= 0) return true;
  if (totalUids >= MAX_UIDS) return false;
  uids[totalUids++] = uid;
  guardarUids();
  return true;
}
void eliminarUid(const String &uid) {
  int i = buscarUid(uid);
  if (i < 0) return;
  for (uint8_t j = i; j + 1 < totalUids; j++) uids[j] = uids[j + 1];
  totalUids--;
  guardarUids();
}

// ---------- Acceso ----------
void procesarTarjeta(String uid, bool simulada) {
  uid.toUpperCase();
  if (uid == ultimoUid && millis() - ultimoUidMs < 2000) return;  // evita lecturas repetidas
  ultimoUid = uid; ultimoUidMs = millis();
  if (modoAprender) {
    modoAprender = false;
    evento(agregarUid(uid) ? "rfid_learned" : "rfid_full", uid);
    listarUids();
  }
  bool autorizado = buscarUid(uid) >= 0;
  com.enviar("{\"type\":\"rfid\",\"uid\":\"%s\",\"authorized\":%s,\"simulated\":%s}\n", uid.c_str(), bstr(autorizado), bstr(simulada));
  if (autorizado && (seguridadArmada || armando)) desactivarSeguridad("tarjeta " + uid);
  if (alarmaGas) return;  // en alarma el pestillo ya está abierto
  if (autorizado) {
    accesoActivo = true; accesoInicio = millis(); alertaPuertaAbierta = false;
    timbre.reproducir(PATRON_ACCESO_OK);
    evento("access_granted", uid);
  } else {
    timbre.reproducir(PATRON_ACCESO_NO);
    evento("access_denied", uid);
  }
}

void leerRfid() {
#ifndef SIMULACION_WOKWI
  if (millis() - ultimaRevisionRfid > 10000) {  // el RC522 a veces se cuelga: verificar y reiniciar
    ultimaRevisionRfid = millis();
    byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
    bool ok = v != 0x00 && v != 0xFF;
    if (!ok) rfid.PCD_Init();
    if (ok != rfidOk) { rfidOk = ok; evento(ok ? "rfid_online" : "rfid_offline"); }
  }
  if (!rfidOk || millis() - ultimoRfid < 150) return;
  ultimoRfid = millis();
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;
  String uid;
  for (byte i = 0; i < rfid.uid.size; i++) { char hex[3]; sprintf(hex, "%02X", rfid.uid.uidByte[i]); uid += hex; }
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  procesarTarjeta(uid, false);
#endif
}

// ---------- Entradas ----------
void cambiarModo(bool automatico, const char *origen) {
  modoAuto = automatico;
  for (auto &p : prioridadHasta) p = 0;
  evento(automatico ? "mode_auto" : "mode_manual", origen);
}

void dispararIntrusion(const char *causa) {
  if (!seguridadArmada || intrusion) return;
  intrusion = true;
  sirenaIntrusionSilenciada = false;
  causaIntrusion = causa;
  evento("intrusion", causa);
}

void armarSeguridad() {
  armando = true; seguridadArmada = false; intrusion = false;
  armadoInicio = millis();
  evento("security_arming", String(RETARDO_ARMADO_MS / 1000) + " s");
}

void desactivarSeguridad(const String &origen) {
  bool estaba = seguridadArmada || armando || intrusion;
  seguridadArmada = armando = intrusion = false;
  sirenaIntrusionSilenciada = false;
  causaIntrusion = "";
  preferencias.putBool("armada", false);
  if (estaba) evento("security_disarmed", origen);
}

void leerSensores() {
  rawLuz = leerAnalogico(PIN_POT_LUZ);
  rawGas = leerAnalogico(PIN_POT_GAS);
  rawConsigna = leerAnalogico(PIN_POT_CONSIGNA);
  rawBrillo = leerAnalogico(PIN_POT_BRILLO);
  luxAmbiente = mapear(rawLuz, 0, 1000);       // simulado con potenciómetro
  gasPct = mapear(rawGas, 0, 100);             // simulado con potenciómetro
  consigna = roundf(mapear(rawConsigna, 18, 32) * 2) / 2;  // pasos de 0.5 °C
  brilloPct = mapear(rawBrillo, 5, 100);

  float t = leerTemperaturaC();
  bool ok = !isnan(t) && t > -20 && t < 90;
  if (ok) temperatura = isnan(temperatura) ? t : temperatura * 0.7f + t * 0.3f;  // filtro suave
  else temperatura = NAN;
  if (ok != sensorTempOk) { sensorTempOk = ok; evento(ok ? "temp_sensor_ok" : "temp_sensor_error"); }

  if (entPuerta.actualizar(puertaAbiertaAhora())) {
    puertaAbierta = entPuerta.estable;
    if (puertaAbierta && seguridadArmada) dispararIntrusion("puerta");
    else if (puertaAbierta && salidaPestillo == 0) { puertaForzadaHasta = millis() + PUERTA_FORZADA_MS; evento("door_forced"); }
    else evento(puertaAbierta ? "door_opened" : "door_closed");
  }
}

void leerControles() {
  // 80 ms estables para filtrar ruido del cable largo que va a los marcos metálicos.
  if (entContacto.actualizar(contactoAhora(), 80) && entContacto.estable) {
    if (seguridadArmada) dispararIntrusion("contacto en marco metálico");
    else evento("contact_detected");
  }
  if (entTimbre.actualizar(digitalRead(PIN_TIMBRE) == LOW) && entTimbre.estable && millis() - ultimoTimbre > 2000) {
    ultimoTimbre = millis();
    timbre.reproducir(PATRON_TIMBRE);
    evento("doorbell");
  }
  if (entDip.actualizar(digitalRead(PIN_DIP_MODO) == LOW, 60)) {
    dipAuto = entDip.estable;
    if (dipAuto != modoAuto) cambiarModo(dipAuto, "DIP");
  }
}

// ---------- Reglas de control ----------
void evaluarReglas() {
  unsigned long ahora = millis();

  bool gas = alarmaGas ? gasPct >= GAS_ALARMA_OFF : gasPct >= GAS_ALARMA_ON;
  if (gas != alarmaGas) {
    alarmaGas = gas;
    if (!gas) sirenaSilenciada = false;
    evento(gas ? "gas_alarm_on" : "gas_alarm_off", String(gasPct, 0) + "%");
  }

  if (isnan(temperatura)) autoVent = false;
  else {
    bool vent = autoVent ? temperatura > consigna - VENT_BAJO_CONSIGNA : temperatura >= consigna + VENT_SOBRE_CONSIGNA;
    if (vent != autoVent) { autoVent = vent; if (modoAuto) evento(vent ? "auto_fan_on" : "auto_fan_off", String(temperatura, 1) + "C"); }
  }

  bool luz = autoLuz ? luxAmbiente < LUX_LUZ_OFF : luxAmbiente < LUX_LUZ_ON;
  if (luz != autoLuz) { autoLuz = luz; if (modoAuto) evento(luz ? "auto_light_on" : "auto_light_off"); }

  bool abrirCortina = autoCortina ? luxAmbiente > LUX_CORTINA_CERRAR : luxAmbiente > LUX_CORTINA_ABRIR;
  if (abrirCortina != autoCortina) { autoCortina = abrirCortina; if (modoAuto) evento(abrirCortina ? "auto_curtain_open" : "auto_curtain_closed"); }

  if (armando && ahora - armadoInicio >= RETARDO_ARMADO_MS) {
    armando = false;
    if (puertaAbierta) evento("security_arm_failed", "puerta abierta");
    else { seguridadArmada = true; preferencias.putBool("armada", true); evento("security_armed"); }
  }

  if (accesoActivo && ahora - accesoInicio >= ACCESO_MS) {
    if (!puertaAbierta) { accesoActivo = false; evento("access_closed"); }
    else if (!alertaPuertaAbierta && ahora - accesoInicio >= PUERTA_ABIERTA_ALERTA_MS) {
      alertaPuertaAbierta = true; evento("door_left_open");
    }
  }
  if (!puertaAbierta && alertaPuertaAbierta) { alertaPuertaAbierta = false; accesoActivo = false; }
}

void aplicarSalidas() {
  int luz = prioridadManual(D_LUZ) ? manualNivelLuz : (autoLuz ? (int)brilloPct : 0);
  if (!prioridadManual(D_LUZ) && autoLuz && salidaLuz > 0 && abs(luz - salidaLuz) < 3) luz = salidaLuz;  // ruido del pot
  int vent = alarmaGas || (prioridadManual(D_VENT) ? manualVent : autoVent);
  int cort = prioridadManual(D_CORTINA) ? manualCortina : autoCortina;
  int pestillo = alarmaGas || accesoActivo || manualPestillo;

  if (luz != salidaLuz) { salidaLuz = luz; analogWrite(PIN_LUZ, map(luz, 0, 100, 0, 255)); }
  if (vent != salidaVent) { salidaVent = vent; escribirRele(vent); }
  if (cort != salidaCortina) { salidaCortina = cort; cortina.abrir(cort); }
  if (pestillo != salidaPestillo) { salidaPestillo = pestillo; servoPestillo.mover(pestillo); }

  bool quiereSirena = (alarmaGas && !sirenaSilenciada) || millis() < puertaForzadaHasta || sirenaManual ||
                      (intrusion && !sirenaIntrusionSilenciada);
  if (quiereSirena && !sirena.sonando()) sirena.reproducir(PATRON_SIRENA, true);
  if (!quiereSirena && sirena.sonando()) sirena.detener();

  if (armando && !timbre.sonando()) { timbre.reproducir(PATRON_ARMANDO, true); beepArmando = true; }
  if (!armando && beepArmando) { timbre.detener(); beepArmando = false; if (seguridadArmada) timbre.reproducir(PATRON_ACCESO_NO); }
  if (alertaPuertaAbierta && !timbre.sonando()) { timbre.reproducir(PATRON_AVISO, true); avisoSonando = true; }
  if (!alertaPuertaAbierta && avisoSonando) { timbre.detener(); avisoSonando = false; }
}

// ---------- Telemetría ----------
void enviarTelemetria() {
  long restante = accesoActivo ? max(0L, (long)(ACCESO_MS - (millis() - accesoInicio)) / 1000) : 0;
  char temp[12];
  if (isnan(temperatura)) strcpy(temp, "null"); else snprintf(temp, sizeof(temp), "%.1f", temperatura);
  com.enviar(
    "{\"type\":\"telemetry\",\"mode\":\"%s\",\"dip_auto\":%s,"
    "\"sensors\":{\"temperature_c\":%s,\"setpoint_c\":%.1f,\"light_lux\":%.0f,\"gas_pct\":%.0f,\"brightness_pct\":%.0f,\"door_open\":%s,\"contact\":%s},"
    "\"raw\":{\"light\":%d,\"gas\":%d,\"setpoint\":%d,\"brightness\":%d},"
    "\"actuators\":{\"light\":%d,\"fan\":%s,\"curtain\":\"%s\",\"curtain_pct\":%d,\"curtain_moving\":%s,"
    "\"latch\":\"%s\",\"siren\":%s,\"bell\":%s},"
    "\"alarms\":{\"gas\":%s,\"siren_muted\":%s,\"door_forced\":%s,\"door_left_open\":%s,\"temp_sensor\":%s},"
    "\"access\":{\"active\":%s,\"remaining_s\":%ld},"
    "\"security\":{\"armed\":%s,\"arming_s\":%ld,\"intrusion\":%s,\"cause\":\"%s\",\"siren_muted\":%s},"
    "\"manual\":{\"light\":%s,\"fan\":%s,\"curtain\":%s},"
    "\"rfid_ok\":%s,\"authorized_cards\":%u,\"learning\":%s,\"channels\":\"%s\",\"ip\":\"%s\"}\n",
    modoAuto ? "auto" : "manual", bstr(dipAuto),
    temp, consigna, luxAmbiente, gasPct, brilloPct, bstr(puertaAbierta), bstr(entContacto.estable),
    rawLuz, rawGas, rawConsigna, rawBrillo,
    salidaLuz, bstr(salidaVent), salidaCortina ? "open" : "closed", cortina.porcentaje(), bstr(cortina.enMovimiento()),
    salidaPestillo ? "unlocked" : "locked", bstr(sirena.sonando()), bstr(timbre.sonando()),
    bstr(alarmaGas), bstr(sirenaSilenciada), bstr(millis() < puertaForzadaHasta), bstr(alertaPuertaAbierta), bstr(sensorTempOk),
    bstr(accesoActivo), restante,
    bstr(seguridadArmada), armando ? (long)((RETARDO_ARMADO_MS - (millis() - armadoInicio)) / 1000 + 1) : 0L, bstr(intrusion),
    causaIntrusion.c_str(), bstr(sirenaIntrusionSilenciada),
    bstr(prioridadManual(D_LUZ)), bstr(prioridadManual(D_VENT)), bstr(prioridadManual(D_CORTINA)),
    bstr(rfidOk), totalUids, bstr(modoAprender), com.canales(), com.ip().c_str());
}

// ---------- Comandos ----------
bool esOnOff(const String &v) { return v == "on" || v == "off"; }

void procesarComando(String linea) {
  linea.trim();
  if (!linea.length()) return;
  String original = linea;
  linea.toLowerCase();
  if (linea == "status") { enviarTelemetria(); return; }
  int sep = linea.indexOf(':');
  if (sep < 1) { ack(original, false, "formato dispositivo:valor"); return; }
  String disp = linea.substring(0, sep), valor = linea.substring(sep + 1);
  valor.trim();
  unsigned long hasta = millis() + PRIORIDAD_MANUAL_MS;
  bool bloqueado = alarmaGas && (disp == "fan" || disp == "latch");
  const char *nota = "";

  if (disp == "mode") {
    if (valor != "auto" && valor != "manual") { ack(original, false, "auto o manual"); return; }
    cambiarModo(valor == "auto", "web");
  } else if (disp == "light") {
    int nivel = valor.toInt();
    if (nivel < 0 || nivel > 100 || (nivel == 0 && valor != "0")) { ack(original, false, "nivel 0-100"); return; }
    manualNivelLuz = nivel; prioridadHasta[D_LUZ] = hasta;
  } else if (disp == "fan") {
    if (!esOnOff(valor)) { ack(original, false, "on u off"); return; }
    manualVent = valor == "on"; prioridadHasta[D_VENT] = hasta;
  } else if (disp == "curtain") {
    if (valor != "open" && valor != "closed") { ack(original, false, "open o closed"); return; }
    manualCortina = valor == "open"; prioridadHasta[D_CORTINA] = hasta;
  } else if (disp == "latch") {
    if (valor != "locked" && valor != "unlocked") { ack(original, false, "locked o unlocked"); return; }
    manualPestillo = valor == "unlocked";
    if (!manualPestillo) accesoActivo = false;
  } else if (disp == "siren") {
    if (!esOnOff(valor)) { ack(original, false, "on u off"); return; }
    sirenaManual = valor == "on";
  } else if (disp == "security") {
    if (valor == "arm") {
      if (puertaAbierta) { ack(original, false, "cierra la puerta para activar la seguridad"); return; }
      armarSeguridad();
    } else if (valor == "disarm") desactivarSeguridad("web");
    else { ack(original, false, "arm o disarm"); return; }
  } else if (disp == "bell") {
    timbre.reproducir(PATRON_TIMBRE);
  } else if (disp == "alarm") {
    if (valor != "silence") { ack(original, false, "alarm:silence"); return; }
    sirenaSilenciada = true; sirenaManual = false; puertaForzadaHasta = 0;
    if (intrusion) sirenaIntrusionSilenciada = true;  // la intrusión sigue registrada hasta desactivar
    evento("siren_silenced");
  } else if (disp == "rfid") {
    String uid = original.substring(sep + 1); uid.trim(); uid.toUpperCase();
    if (!uidValido(uid)) { ack(original, false, "UID hexadecimal"); return; }
    ack(original, true);
    procesarTarjeta(uid, true);
    return;
  } else if (disp == "auth") {
    String resto = original.substring(sep + 1); resto.trim();
    int sep2 = resto.indexOf(':');
    String accion = sep2 < 0 ? resto : resto.substring(0, sep2);
    String uid = sep2 < 0 ? "" : resto.substring(sep2 + 1);
    accion.toLowerCase(); uid.trim(); uid.toUpperCase();
    if (accion == "list") { listarUids(); return; }
    if (accion == "clear") { totalUids = 0; guardarUids(); }
    else if (accion == "learn") { modoAprender = true; evento("rfid_learning"); }
    else if (accion == "add" && uidValido(uid)) { if (!agregarUid(uid)) { ack(original, false, "memoria llena"); return; } }
    else if (accion == "del" && uidValido(uid)) eliminarUid(uid);
    else { ack(original, false, "auth:add|del|clear|list|learn"); return; }
    ack(original, true);
    listarUids();
    return;
  } else {
    ack(original, false, "dispositivo desconocido");
    return;
  }
  if (bloqueado) nota = "alarma de gas activa: se aplicara al terminar";
  else if (modoAuto && (disp == "light" || disp == "fan" || disp == "curtain")) nota = "prioridad manual 60 s";
  ack(original, true, nota);
}

void setup() {
  com.iniciar();
  escribirRele(false);
  pinMode(PIN_LUZ, OUTPUT);
  pinMode(PIN_PUERTA, INPUT_PULLUP);
  pinMode(PIN_TIMBRE, INPUT_PULLUP);
  pinMode(PIN_DIP_MODO, INPUT_PULLUP);
  pinMode(PIN_CONTACTO, INPUT);  // GPIO39 no tiene resistencias internas; el KY-036 trae su propia resistencia en D0
  analogReadResolution(12);
  sirena.iniciar();
  timbre.iniciar();
  cortina.iniciar();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servoPestillo.iniciar();  // los servos continuos asumen que arrancan CERRADOS

  preferencias.begin("casa", false);
  cargarUids();
  seguridadArmada = preferencias.getBool("armada", false);  // tras un corte de energía sigue armada

#ifndef SIMULACION_WOKWI
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();
  delay(50);
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  rfidOk = version != 0x00 && version != 0xFF;
#endif

  // Estado inicial de las entradas sin generar eventos.
  entPuerta.estable = entPuerta.ultimo = puertaAbierta = puertaAbiertaAhora();
  entContacto.estable = entContacto.ultimo = contactoAhora();
  entTimbre.estable = entTimbre.ultimo = digitalRead(PIN_TIMBRE) == LOW;
  entDip.estable = entDip.ultimo = dipAuto = digitalRead(PIN_DIP_MODO) == LOW;
  modoAuto = dipAuto;
  leerSensores();
  alarmaGas = gasPct >= GAS_ALARMA_ON;
  autoLuz = luxAmbiente < LUX_LUZ_ON;
  autoCortina = luxAmbiente > LUX_CORTINA_ABRIR;
  aplicarSalidas();

  com.enviar("{\"type\":\"ready\",\"firmware\":\"casa-multimodal 3.0\",\"rfid_ok\":%s,\"authorized_cards\":%u,\"channels\":\"%s\",\"ip\":\"%s\"}\n",
             bstr(rfidOk), totalUids, com.canales(), com.ip().c_str());
}

void loop() {
  com.atender(procesarComando);
  leerControles();
  leerRfid();
  if (millis() - ultimoMuestreo >= 100) {
    ultimoMuestreo = millis();
    leerSensores();
    evaluarReglas();
  }
  aplicarSalidas();
  cortina.actualizar();
  servoPestillo.actualizar();
  sirena.actualizar();
  timbre.actualizar();
  if (millis() - ultimaTelemetria >= TELEMETRIA_MS) {
    ultimaTelemetria = millis();
    enviarTelemetria();
  }
}
