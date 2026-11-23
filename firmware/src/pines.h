// Mapa de pines y configuración compartidos por el firmware principal y el de pruebas.
// Placa: ESP32 DevKit de 30 pines (15 por lado). Los 23 pines útiles están todos asignados.
// Ver la guía de montaje con diagramas: casa-multimodal/docs/montaje.html
#pragma once
#include <Arduino.h>

// =====================================================================
//  COMUNICACIÓN CON EL COMPUTADOR
// =====================================================================
// El USB siempre está activo. Además se puede activar UNO de estos canales inalámbricos.
// Bluetooth: emparejar "CasaMultimodal" en Windows y conectar la web a su puerto COM.
// Wi-Fi:     el ESP32 abre un servidor TCP; conectar la web a socket://<IP>:3333
#ifndef COM_BLUETOOTH
#define COM_BLUETOOTH 1
#endif
#ifndef COM_WIFI
#define COM_WIFI 0
#endif
#define NOMBRE_BLUETOOTH "CasaMultimodal"
#define WIFI_SSID "nombre-de-tu-red"      // Red de 2.4 GHz (por ejemplo, el hotspot del computador)
#define WIFI_CLAVE "clave-de-tu-red"
constexpr uint16_t PUERTO_TCP = 3333;

// =====================================================================
//  ENTRADAS ANALÓGICAS — solo ADC1 (GPIO32-39): el ADC2 no funciona con Wi-Fi/Bluetooth
//  Potenciómetros: extremos a 3V3 y GND (NUNCA a 5 V), pin central al GPIO.
// =====================================================================
constexpr uint8_t PIN_TEMPERATURA = 32;   // KY-028, salida A0. Alimentar el módulo con 3V3 (no 5 V)
constexpr uint8_t PIN_POT_LUZ = 33;       // Simula un sensor de luz ambiente (0-1000 lux)
constexpr uint8_t PIN_POT_GAS = 34;       // Simula un sensor de gas MQ-2 (0-100 %)
constexpr uint8_t PIN_POT_CONSIGNA = 35;  // Consigna (setpoint) del termostato: 18-32 °C
constexpr uint8_t PIN_POT_BRILLO = 36;    // "VP" en la placa. Brillo de la luz: 0-100 %

// =====================================================================
//  ENTRADAS DIGITALES
// =====================================================================
constexpr uint8_t PIN_CONTACTO = 39;      // "VN". Sensor de contacto metálico KY-036, salida D0 (modo seguridad)
constexpr uint8_t PIN_PUERTA = 15;        // Reed switch entre el pin y GND (pull-up interno). Imán cerca = puerta cerrada
constexpr uint8_t PIN_TIMBRE = 21;        // Botón del timbre entre el pin y GND
constexpr uint8_t PIN_DIP_MODO = 22;      // Interruptor 1 del DIP entre el pin y GND. ON = modo automático

// Si alguna lectura sale al revés, cambia el valor correspondiente.
constexpr bool PUERTA_ABIERTA_EN_ALTO = true;  // reed abierto -> pull-up -> ALTO = puerta abierta
constexpr bool CONTACTO_ACTIVO_EN_ALTO = true; // KY-036: D0 en ALTO al tocar el metal (LED2 del módulo encendido)

// =====================================================================
//  RFID RC522 (SPI) — alimentar a 3V3. El pin RST del módulo va a 3V3 (no usa GPIO).
// =====================================================================
constexpr uint8_t PIN_RFID_SCK = 18;
constexpr uint8_t PIN_RFID_MISO = 19;
constexpr uint8_t PIN_RFID_MOSI = 23;
constexpr uint8_t PIN_RFID_SS = 5;        // Pin "SDA" del módulo

// =====================================================================
//  SALIDAS
// =====================================================================
constexpr uint8_t PIN_LUZ = 2;            // LED azul integrado de la placa (+ LED externo opcional con 220 Ω)
constexpr uint8_t PIN_RELE_VENTILADOR = 4;
constexpr uint8_t PIN_SERVO_PESTILLO = 25;
// GPIO26 queda libre (antes: servo de la ventana, descartado).
constexpr uint8_t PIN_BUZZER_ALARMA = 16; // "RX2" en la placa. Sirena (gas, puerta forzada)
constexpr uint8_t PIN_BUZZER_TIMBRE = 17; // "TX2" en la placa. Timbre y confirmaciones de acceso
// Motor paso a paso 28BYJ-48 con tarjeta ULN2003: IN1, IN2, IN3, IN4 (cortina)
constexpr uint8_t PIN_PASO[4] = {13, 12, 14, 27};

// =====================================================================
//  SENSOR DE TEMPERATURA KY-028 (termistor NTC 10 kΩ, B = 3950, en divisor con 10 kΩ)
// =====================================================================
// true: el NTC está entre A0 y GND (la lectura BAJA cuando la temperatura sube).
// Si al calentar el sensor con los dedos la temperatura calculada baja, cambia a false.
constexpr bool NTC_HACIA_GND = true;
constexpr float NTC_BETA = 3950.0f;
constexpr float NTC_R25 = 10000.0f;          // resistencia del NTC a 25 °C
constexpr float NTC_R_FIJA = 10000.0f;       // resistencia fija del divisor en el módulo
constexpr float TEMPERATURA_AJUSTE_C = 0.0f; // corrección tras comparar con un termómetro

// =====================================================================
//  RELÉ (módulo CW-020, "low level trigger" = activo en BAJO)
// =====================================================================
#ifndef RELE_ACTIVO_EN_BAJO
#define RELE_ACTIVO_EN_BAJO true
#endif

// =====================================================================
//  SERVO del pestillo de la puerta
// =====================================================================
// true = rotación continua (360°): se mueve por TIEMPO. false = 180°: se mueve por ÁNGULO.
#ifdef SIMULACION_WOKWI
constexpr bool PESTILLO_CONTINUO = false;  // los servos de Wokwi son de 180°
#else
constexpr bool PESTILLO_CONTINUO = true;
#endif
// Servo continuo: tiempo de giro de cerrado a abierto (calibrar con la opción p del programa de pruebas).
constexpr unsigned long TIEMPO_PESTILLO_MS = 400;
constexpr bool PESTILLO_INVERTIR = false;
constexpr int VELOCIDAD_SERVO_US = 250;      // 0-1000: más alto = gira más rápido
// Servo de 180°: ángulos.
constexpr int ANGULO_PESTILLO_CERRADO = 0;
constexpr int ANGULO_PESTILLO_ABIERTO = 90;

// =====================================================================
//  MOTOR PASO A PASO (cortina)
// =====================================================================
constexpr long PASOS_CORTINA = 4096;            // 4096 medios pasos = 1 vuelta del eje (calibrar con la opción C)
constexpr unsigned int US_POR_PASO = 1200;      // menos = más rápido (mínimo recomendado ~1000 µs)
constexpr bool CORTINA_INVERTIR = false;

// =====================================================================
//  Utilidades
// =====================================================================
// Relé activo en bajo alimentado a 5 V: un "alto" de 3.3 V no alcanza a apagarlo.
// Para APAGAR se deja el pin en alta impedancia (INPUT); para ENCENDER, salida en BAJO.
inline void escribirRele(bool encendido) {
  if (RELE_ACTIVO_EN_BAJO) {
    if (encendido) {
      pinMode(PIN_RELE_VENTILADOR, OUTPUT);
      digitalWrite(PIN_RELE_VENTILADOR, LOW);
    } else {
      pinMode(PIN_RELE_VENTILADOR, INPUT);
    }
  } else {
    pinMode(PIN_RELE_VENTILADOR, OUTPUT);
    digitalWrite(PIN_RELE_VENTILADOR, encendido ? HIGH : LOW);
  }
}

// Promedio de varias lecturas para reducir el ruido del ADC.
inline int leerAnalogico(uint8_t pin, uint8_t muestras = 8) {
  uint32_t suma = 0;
  for (uint8_t i = 0; i < muestras; i++) suma += analogRead(pin);
  return suma / muestras;
}

// Temperatura del KY-028 en °C (NAN si el sensor está desconectado o en corto).
inline float leerTemperaturaC() {
#ifdef SIMULACION_WOKWI
  float fraccion = leerAnalogico(PIN_TEMPERATURA) / 4095.0f;
#else
  uint32_t mv = 0;
  for (uint8_t i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_TEMPERATURA);
  float fraccion = (mv / 8) / 3300.0f;   // el módulo se alimenta con 3V3
#endif
  if (fraccion < 0.01f || fraccion > 0.99f) return NAN;
  float rNtc = NTC_HACIA_GND ? NTC_R_FIJA * fraccion / (1.0f - fraccion)
                             : NTC_R_FIJA * (1.0f - fraccion) / fraccion;
  float kelvin = 1.0f / (logf(rNtc / NTC_R25) / NTC_BETA + 1.0f / 298.15f);
  return kelvin - 273.15f + TEMPERATURA_AJUSTE_C;
}

inline bool puertaAbiertaAhora() { return (digitalRead(PIN_PUERTA) == HIGH) == PUERTA_ABIERTA_EN_ALTO; }
inline bool contactoAhora() { return (digitalRead(PIN_CONTACTO) == HIGH) == CONTACTO_ACTIVO_EN_ALTO; }
