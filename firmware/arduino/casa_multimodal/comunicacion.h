// Canales de comunicación con el computador: USB siempre, y Bluetooth o Wi-Fi según pines.h.
// Todos usan el mismo protocolo de líneas de texto: lo que se envía sale por todos los canales
// y los comandos se aceptan desde cualquiera de ellos.
#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include "pines.h"

#if COM_BLUETOOTH
#include <BluetoothSerial.h>
BluetoothSerial canalBT;
#endif
#if COM_WIFI
#include <ESPmDNS.h>
#include <WiFi.h>
WiFiServer servidorTCP(PUERTO_TCP);
WiFiClient clienteTCP;
#endif

typedef void (*ManejadorLinea)(String linea);

class Comunicacion {
 public:
  void iniciar() {
    Serial.begin(115200);
#if COM_BLUETOOTH
    canalBT.begin(NOMBRE_BLUETOOTH);
#endif
#if COM_WIFI
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_CLAVE);
    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
      servidorTCP.begin();
      MDNS.begin("casa-multimodal");
    }
#endif
  }

  // printf hacia todos los canales activos.
  void enviar(const char *formato, ...) {
    char buffer[900];
    va_list args;
    va_start(args, formato);
    int n = vsnprintf(buffer, sizeof(buffer), formato, args);
    va_end(args);
    if (n <= 0) return;
    size_t largo = min((size_t)n, sizeof(buffer) - 1);
    Serial.write((const uint8_t *)buffer, largo);
#if COM_BLUETOOTH
    if (canalBT.hasClient()) canalBT.write((const uint8_t *)buffer, largo);
#endif
#if COM_WIFI
    if (clienteTCP && clienteTCP.connected()) clienteTCP.write((const uint8_t *)buffer, largo);
#endif
  }

  void atender(ManejadorLinea manejador) {
    leer(Serial, lineaUsb_, manejador);
#if COM_BLUETOOTH
    leer(canalBT, lineaBt_, manejador);
#endif
#if COM_WIFI
    if (servidorTCP.hasClient()) {        // un cliente a la vez: el más reciente reemplaza al anterior
      if (clienteTCP) clienteTCP.stop();
      clienteTCP = servidorTCP.accept();
      lineaWifi_ = "";
    }
    if (clienteTCP && clienteTCP.connected()) leer(clienteTCP, lineaWifi_, manejador);
#endif
  }

  const char *canales() const {
#if COM_BLUETOOTH
    return "usb+bluetooth";
#elif COM_WIFI
    return "usb+wifi";
#else
    return "usb";
#endif
  }

  String ip() const {
#if COM_WIFI
    if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
#endif
    return "";
  }

 private:
  void leer(Stream &origen, String &linea, ManejadorLinea manejador) {
    while (origen.available()) {
      char c = static_cast<char>(origen.read());
      if (c == '\n' || c == '\r') { if (linea.length()) manejador(linea); linea = ""; }
      else if (linea.length() < 120) linea += c;
    }
  }

  String lineaUsb_, lineaBt_, lineaWifi_;
};

Comunicacion com;
