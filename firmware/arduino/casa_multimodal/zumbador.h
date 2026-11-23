// Buzzer activo (suena con solo darle voltaje) controlado con patrones de encendido/apagado.
// No bloquea: llamar actualizar() en cada vuelta del loop.
#pragma once
#include <Arduino.h>

class Zumbador {
 public:
  explicit Zumbador(uint8_t pin) : pin_(pin) {}

  void iniciar() { pinMode(pin_, OUTPUT); digitalWrite(pin_, LOW); }

  // Patrón: duraciones en ms alternando sonido/silencio, terminado en 0.
  // repetir = true lo repite hasta llamar detener().
  void reproducir(const uint16_t *patron, bool repetir = false) {
    patron_ = patron; repetir_ = repetir; indice_ = 0;
    inicio_ = millis();
    digitalWrite(pin_, HIGH);
  }

  void detener() { patron_ = nullptr; digitalWrite(pin_, LOW); }
  bool sonando() const { return patron_ != nullptr; }

  void actualizar() {
    if (!patron_ || millis() - inicio_ < patron_[indice_]) return;
    inicio_ = millis();
    indice_++;
    if (patron_[indice_] == 0) {
      if (!repetir_) { detener(); return; }
      indice_ = 0;
    }
    digitalWrite(pin_, indice_ % 2 == 0 ? HIGH : LOW);  // posiciones pares = sonido
  }

 private:
  uint8_t pin_;
  const uint16_t *patron_ = nullptr;
  bool repetir_ = false;
  uint8_t indice_ = 0;
  unsigned long inicio_ = 0;
};

// Patrones (ms): sonido, silencio, sonido, ... , 0
static const uint16_t PATRON_SIRENA[] = {250, 250, 0};
static const uint16_t PATRON_TIMBRE[] = {350, 120, 700, 0};             // "ding-dong"
static const uint16_t PATRON_ACCESO_OK[] = {80, 80, 80, 0};              // bip-bip
static const uint16_t PATRON_ACCESO_NO[] = {900, 0};                      // biiiip
static const uint16_t PATRON_AVISO[] = {60, 1940, 0};                     // bip cada 2 s
static const uint16_t PATRON_PRUEBA[] = {200, 200, 200, 200, 200, 0};
static const uint16_t PATRON_ARMANDO[] = {70, 930, 0};                    // bip por segundo mientras se arma
