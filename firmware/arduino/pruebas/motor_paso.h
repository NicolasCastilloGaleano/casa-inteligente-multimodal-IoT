// Motor paso a paso 28BYJ-48 con tarjeta ULN2003, en medio paso (4096 pasos por vuelta).
// No bloquea: cada llamada a actualizar() avanza como máximo un paso.
// Lleva la cuenta de la posición, así que abrir/cerrar siempre llega al mismo punto.
// Al encender, la cortina debe estar CERRADA (posición 0).
#pragma once
#include <Arduino.h>

class MotorPaso {
 public:
  MotorPaso(const uint8_t pines[4], long recorrido, unsigned int usPorPaso, bool invertir)
      : recorrido_(recorrido), usPorPaso_(usPorPaso), invertir_(invertir) {
    for (uint8_t i = 0; i < 4; i++) pines_[i] = pines[i];
  }

  void iniciar() {
    for (uint8_t p : pines_) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  }

  void irA(long objetivo) { objetivo_ = constrain(objetivo, 0L, recorrido_); }
  void abrir(bool abierto) { irA(abierto ? recorrido_ : 0); }

  // Para calibrar: mueve n pasos (positivo = abrir) sin límites y bloqueando.
  void mover(long pasos) {
    int sentido = pasos > 0 ? 1 : -1;
    for (long i = 0; i < labs(pasos); i++) { darPaso(sentido); delayMicroseconds(usPorPaso_); }
    posicion_ += pasos;
    objetivo_ = posicion_;
    liberar();
  }
  void fijarCero() { posicion_ = objetivo_ = 0; }

  void actualizar() {
    if (posicion_ == objetivo_) {
      if (energizado_) liberar();  // sin corriente en reposo: el motor no se calienta
      return;
    }
    if (micros() - ultimoPaso_ < usPorPaso_) return;
    ultimoPaso_ = micros();
    int sentido = objetivo_ > posicion_ ? 1 : -1;
    darPaso(sentido);
    posicion_ += sentido;
  }

  bool enMovimiento() const { return posicion_ != objetivo_; }
  bool abierta() const { return objetivo_ >= recorrido_ / 2; }
  int porcentaje() const { return recorrido_ ? (int)(posicion_ * 100 / recorrido_) : 0; }
  long posicion() const { return posicion_; }

 private:
  void darPaso(int sentido) {
    static const uint8_t SECUENCIA[8] = {0b0001, 0b0011, 0b0010, 0b0110, 0b0100, 0b1100, 0b1000, 0b1001};
    fase_ = (fase_ + ((sentido > 0) != invertir_ ? 1 : 7)) % 8;
    for (uint8_t i = 0; i < 4; i++) digitalWrite(pines_[i], (SECUENCIA[fase_] >> i) & 1);
    energizado_ = true;
  }

  void liberar() {
    for (uint8_t p : pines_) digitalWrite(p, LOW);
    energizado_ = false;
  }

  uint8_t pines_[4];
  long recorrido_;
  unsigned int usPorPaso_;
  bool invertir_;
  long posicion_ = 0, objetivo_ = 0;
  uint8_t fase_ = 0;
  bool energizado_ = false;
  unsigned long ultimoPaso_ = 0;
};
