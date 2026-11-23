// Control de un servo que puede ser de 180° (por ángulo) o de rotación continua (por tiempo).
//
// Servo continuo: no sabe en qué posición está, así que se estima por tiempo de giro.
// Al encender, el mecanismo debe estar en la posición CERRADA. Al terminar cada
// movimiento se corta la señal (detach): así se detiene siempre, aunque el punto
// neutro del servo no sea exacto.
#pragma once
#include <Arduino.h>
#include <ESP32Servo.h>

class MotorServo {
 public:
  MotorServo(uint8_t pin, bool continuo, int anguloCerrado, int anguloAbierto,
             unsigned long tiempoMs, bool invertir, int velocidadUs)
      : pin_(pin), continuo_(continuo), anguloCerrado_(anguloCerrado), anguloAbierto_(anguloAbierto),
        tiempoMs_(tiempoMs), invertir_(invertir), velocidadUs_(velocidadUs) {}

  void iniciar() {
    abierto_ = false;
    posicionMs_ = 0;
    if (!continuo_) {
      conectar();
      servo_.write(anguloCerrado_);
    }
  }

  // No bloquea: el movimiento termina en actualizar().
  void mover(bool abrir) {
    abierto_ = abrir;
    if (!continuo_) {
      conectar();
      servo_.write(abrir ? anguloAbierto_ : anguloCerrado_);
      return;
    }
    long actual = posicionActual();
    long objetivo = abrir ? (long)tiempoMs_ : 0;
    if (actual == objetivo) {
      posicionMs_ = actual;
      if (sentido_ != 0) detener();
      return;
    }
    posicionInicio_ = actual;
    sentido_ = objetivo > actual ? 1 : -1;
    duracionMs_ = labs(objetivo - actual);
    inicioMs_ = millis();
    girar(sentido_);
  }

  void actualizar() {
    if (continuo_ && sentido_ != 0 && millis() - inicioMs_ >= duracionMs_) {
      posicionMs_ = posicionInicio_ + sentido_ * (long)duracionMs_;
      detener();
    }
  }

  // Para calibrar: gira un tiempo fijo en un sentido (bloqueante). sentido: +1 abrir, -1 cerrar.
  void pulso(int sentido, unsigned long ms) {
    if (!continuo_) return;
    girar(sentido);
    delay(ms);
    detener();
  }

  bool abierto() const { return abierto_; }
  bool enMovimiento() const { return sentido_ != 0; }
  bool continuo() const { return continuo_; }

 private:
  void conectar() {
    if (servo_.attached()) return;
    servo_.setPeriodHertz(50);
    servo_.attach(pin_, 500, 2400);
  }

  void girar(int sentido) {
    conectar();
    int signo = invertir_ ? -sentido : sentido;
    servo_.writeMicroseconds(1500 + signo * velocidadUs_);
  }

  void detener() {
    sentido_ = 0;
    if (servo_.attached()) {
      servo_.writeMicroseconds(1500);
      servo_.detach();
    }
  }

  long posicionActual() const {
    if (sentido_ == 0) return posicionMs_;
    long p = posicionInicio_ + sentido_ * (long)(millis() - inicioMs_);
    return constrain(p, 0L, (long)tiempoMs_);
  }

  Servo servo_;
  uint8_t pin_;
  bool continuo_;
  int anguloCerrado_, anguloAbierto_;
  unsigned long tiempoMs_;
  bool invertir_;
  int velocidadUs_;
  bool abierto_ = false;
  long posicionMs_ = 0, posicionInicio_ = 0;
  int sentido_ = 0;
  unsigned long inicioMs_ = 0, duracionMs_ = 0;
};
