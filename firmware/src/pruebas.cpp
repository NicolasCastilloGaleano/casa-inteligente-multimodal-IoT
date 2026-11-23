// Programa de pruebas por componente. Se compila con el entorno "pruebas":
//   pio run -e pruebas -t upload && pio device monitor
// En Arduino IDE: abrir firmware/arduino/pruebas/pruebas.ino.
// Monitor serie a 115200 baudios; escribe la letra o el número de la prueba y Enter.
#include <Arduino.h>
#include <ESP32Servo.h>
#include <MFRC522.h>
#include <SPI.h>
#include "pines.h"
#include "motor_paso.h"
#include "motor_servo.h"
#include "zumbador.h"

MotorServo servoPestillo(PIN_SERVO_PESTILLO, PESTILLO_CONTINUO, ANGULO_PESTILLO_CERRADO, ANGULO_PESTILLO_ABIERTO,
                         TIEMPO_PESTILLO_MS, PESTILLO_INVERTIR, VELOCIDAD_SERVO_US);
MotorPaso cortina(PIN_PASO, PASOS_CORTINA, US_POR_PASO, CORTINA_INVERTIR);
Zumbador sirena(PIN_BUZZER_ALARMA);
Zumbador timbre(PIN_BUZZER_TIMBRE);
MFRC522 rfid(PIN_RFID_SS, MFRC522::UNUSED_PIN);

void menu() {
  Serial.println();
  Serial.println(F("============ PRUEBAS CASA MULTIMODAL ============"));
  Serial.println(F(" 1  Luz (LED GPIO2, rampa de brillo)"));
  Serial.println(F(" 2  Rele + ventilador (3 ciclos)"));
  Serial.println(F(" 3  Servo del pestillo (GPIO25)"));
  Serial.println(F(" 5  Motor paso a paso de la cortina (abre y cierra)"));
  Serial.println(F(" 6  Buzzers: alarma (GPIO16) y timbre (GPIO17)"));
  Serial.println(F(" 7  Analogicos: 4 potenciometros + temperatura KY-028"));
  Serial.println(F(" 8  Digitales: puerta (reed), contacto KY-036, boton timbre, DIP"));
  Serial.println(F(" 9  Lector RFID (muestra el UID de cada tarjeta)"));
  Serial.println(F(" 0  Todas las salidas en secuencia"));
  Serial.println(F(" p / c  Calibrar pestillo / cortina"));
  Serial.println(F(" En las pruebas que se repiten escribe 'q' para salir."));
  Serial.println(F("================================================="));
  Serial.print(F("> "));
}

bool pidioSalir() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'q' || c == 'Q') return true;
  }
  return false;
}

void esperar(unsigned long ms) {  // espera atendiendo motores y buzzers
  unsigned long inicio = millis();
  while (millis() - inicio < ms) {
    cortina.actualizar(); servoPestillo.actualizar();
    sirena.actualizar(); timbre.actualizar();
  }
}

void probarLuz() {
  Serial.println(F("[LUZ] Rampa de brillo en GPIO2 (LED azul de la placa y LED externo si lo conectaste)..."));
  for (int r = 0; r < 2; r++) {
    for (int i = 0; i <= 255; i += 5) { analogWrite(PIN_LUZ, i); delay(12); }
    for (int i = 255; i >= 0; i -= 5) { analogWrite(PIN_LUZ, i); delay(12); }
  }
  Serial.println(F("[LUZ] Listo."));
}

void probarRele() {
  Serial.printf("[RELE] GPIO%d, activo en %s\n", PIN_RELE_VENTILADOR, RELE_ACTIVO_EN_BAJO ? "BAJO (CW-020 low level trigger)" : "ALTO");
  for (int i = 0; i < 3; i++) {
    Serial.println(F("[RELE] ENCENDIDO: clic y el ventilador gira"));
    escribirRele(true); delay(2000);
    Serial.println(F("[RELE] APAGADO"));
    escribirRele(false); delay(1500);
  }
  Serial.println(F("[RELE] Si quedo encendido siempre, revisa que VCC del rele vaya a 5 V y GND sea comun."));
}

void esperarServo(MotorServo &servo) { while (servo.enMovimiento()) { servo.actualizar(); delay(5); } }

void probarServo(MotorServo &servo, const char *nombre, char tecla) {
  Serial.printf("[SERVO %s] Tipo: %s. Debe empezar CERRADO.\n", nombre, servo.continuo() ? "rotacion continua (por tiempo)" : "180 grados");
  Serial.printf("[SERVO %s] Abriendo...\n", nombre);
  servo.mover(true); esperarServo(servo); delay(1500);
  Serial.printf("[SERVO %s] Cerrando...\n", nombre);
  servo.mover(false); esperarServo(servo); delay(300);
  Serial.printf("[SERVO %s] Listo y quieto. Si no llega o se pasa, calibra con '%c'.\n", nombre, tecla);
}

void calibrarServo(MotorServo &servo, const char *nombre, const char *constante) {
  if (!servo.continuo()) { Serial.println(F("[CAL] Servo configurado como de 180 grados: ajusta los angulos en pines.h.")); return; }
  Serial.printf("[CAL %s] Pon el mecanismo CERRADO a mano.\n", nombre);
  Serial.println(F("  a = abrir 50 ms   A = abrir 250 ms   c = cerrar 50 ms   C = cerrar 250 ms"));
  Serial.println(F("  r = contador a 0   q = salir"));
  long total = 0;
  while (true) {
    if (!Serial.available()) { delay(10); continue; }
    char c = Serial.read();
    if (c == 'q' || c == 'Q') break;
    long paso = c == 'a' ? 50 : c == 'A' ? 250 : c == 'c' ? -50 : c == 'C' ? -250 : 0;
    if (c == 'r') { total = 0; Serial.println(F("  contador en 0")); continue; }
    if (!paso) continue;
    servo.pulso(paso > 0 ? 1 : -1, labs(paso));
    total += paso;
    Serial.printf("  recorrido desde cerrado: %ld ms\n", total);
  }
  Serial.printf("[CAL %s] Con el mecanismo totalmente ABIERTO, pon ese valor en %s (pines.h).\n", nombre, constante);
  Serial.println(F("  Si 'a' gira hacia el lado de cerrar, cambia *_INVERTIR a true."));
}

void probarCortina() {
  Serial.printf("[CORTINA] 28BYJ-48: %ld pasos de recorrido. Mira los LEDs A-D de la tarjeta ULN2003.\n", PASOS_CORTINA);
  Serial.println(F("[CORTINA] Abriendo..."));
  cortina.abrir(true);
  while (cortina.enMovimiento()) cortina.actualizar();
  esperar(800);
  Serial.println(F("[CORTINA] Cerrando..."));
  cortina.abrir(false);
  while (cortina.enMovimiento()) cortina.actualizar();
  esperar(50);
  Serial.println(F("[CORTINA] Listo. Si vibra sin girar, revisa el orden IN1-IN4 (13, 12, 14, 27) y que la tarjeta tenga 5 V."));
}

void calibrarCortina() {
  Serial.println(F("[CAL CORTINA] Pon la cortina CERRADA."));
  Serial.println(F("  a = abrir 128 pasos   A = abrir 1024   c = cerrar 128   C = cerrar 1024   r = contador a 0   q = salir"));
  cortina.fijarCero();
  while (true) {
    if (!Serial.available()) { delay(10); continue; }
    char c = Serial.read();
    if (c == 'q' || c == 'Q') break;
    long pasos = c == 'a' ? 128 : c == 'A' ? 1024 : c == 'c' ? -128 : c == 'C' ? -1024 : 0;
    if (c == 'r') { cortina.fijarCero(); Serial.println(F("  contador en 0")); continue; }
    if (!pasos) continue;
    cortina.mover(pasos);
    Serial.printf("  posicion: %ld pasos\n", cortina.posicion());
  }
  Serial.println(F("[CAL CORTINA] Con la cortina ABIERTA, pon ese numero en PASOS_CORTINA (pines.h)."));
  Serial.println(F("  Si 'a' la cierra, cambia CORTINA_INVERTIR a true. Vuelve a cerrarla antes de salir."));
  cortina.fijarCero();
}

void probarBuzzers() {
  Serial.println(F("[BUZZER] Alarma (GPIO16): sirena 2 s"));
  sirena.reproducir(PATRON_SIRENA, true); esperar(2000); sirena.detener();
  delay(400);
  Serial.println(F("[BUZZER] Timbre (GPIO17): ding-dong"));
  timbre.reproducir(PATRON_TIMBRE); esperar(1500);
  Serial.println(F("[BUZZER] Si suenan muy bajo: son de 12 V y aqui reciben 3.3 V. Ver la guia de montaje (transistor)."));
}

void probarAnalogicos() {
  Serial.println(F("[ANALOG] Gira los potenciometros y calienta el KY-028 con los dedos (q para salir)"));
  while (!pidioSalir()) {
    int luz = leerAnalogico(PIN_POT_LUZ), gas = leerAnalogico(PIN_POT_GAS);
    int cons = leerAnalogico(PIN_POT_CONSIGNA), bri = leerAnalogico(PIN_POT_BRILLO);
    int mvTemp = analogReadMilliVolts(PIN_TEMPERATURA);
    float t = leerTemperaturaC();
    Serial.printf("luz(33)=%4d gas(34)=%4d consigna(35)=%4d brillo(36/VP)=%4d | KY-028(32)=%4d mV -> ", luz, gas, cons, bri, mvTemp);
    if (isnan(t)) Serial.println(F("sin lectura (revisa A0->32, + ->3V3, G->GND)"));
    else Serial.printf("%.1f C\n", t);
    delay(500);
  }
  Serial.println(F("[ANALOG] Si al calentar el sensor la temperatura BAJA, cambia NTC_HACIA_GND en pines.h."));
}

void probarDigitales() {
  Serial.println(F("[DIG] Acerca/aleja el iman, toca el marco metalico, presiona el timbre y mueve el DIP 1 (q para salir)"));
  Serial.println(F("      KY-036: ajusta su potenciometro azul hasta que el LED2 se encienda SOLO al tocar el metal."));
  int anterior = -1;
  while (!pidioSalir()) {
    bool puerta = puertaAbiertaAhora(), contacto = contactoAhora();
    bool boton = digitalRead(PIN_TIMBRE) == LOW, dip = digitalRead(PIN_DIP_MODO) == LOW;
    int estado = puerta | (contacto << 1) | (boton << 2) | (dip << 3);
    if (estado != anterior) {
      Serial.printf("puerta(15)=%-8s contacto(39/VN)=%-9s timbre(21)=%-11s DIP1(22)=%s\n", puerta ? "ABIERTA" : "cerrada",
                    contacto ? "CONTACTO" : "-", boton ? "PRESIONADO" : "-", dip ? "ON (auto)" : "OFF (manual)");
      anterior = estado;
    }
    delay(20);
  }
}

void probarRfid() {
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("[RFID] Version del chip: 0x%02X ", version);
  if (version == 0x00 || version == 0xFF) {
    Serial.println(F("-> NO RESPONDE. Revisa: 3.3V y RST a 3V3, GND, SDA->5, SCK->18, MOSI->23, MISO->19."));
    return;
  }
  Serial.println(F("(0x91/0x92 = original; otros valores suelen ser clones que funcionan)"));
  Serial.println(F("[RFID] Acerca la tarjeta o el tag (q para salir). Anota los UID."));
  while (!pidioSalir()) {
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      Serial.print(F("[RFID] UID: "));
      for (byte i = 0; i < rfid.uid.size; i++) Serial.printf("%02X", rfid.uid.uidByte[i]);
      Serial.printf("  tipo: %s\n", rfid.PICC_GetTypeName(rfid.PICC_GetType(rfid.uid.sak)));
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      timbre.reproducir(PATRON_ACCESO_OK); esperar(300);
    }
    delay(50);
  }
}

void setup() {
  Serial.begin(115200);
  escribirRele(false);
  pinMode(PIN_LUZ, OUTPUT);
  pinMode(PIN_PUERTA, INPUT_PULLUP);
  pinMode(PIN_TIMBRE, INPUT_PULLUP);
  pinMode(PIN_DIP_MODO, INPUT_PULLUP);
  pinMode(PIN_CONTACTO, INPUT);
  analogReadResolution(12);
  sirena.iniciar();
  timbre.iniciar();
  cortina.iniciar();
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  servoPestillo.iniciar();
  SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_SS);
  rfid.PCD_Init();
  delay(500);
  menu();
}

void loop() {
  if (!Serial.available()) return;
  char opcion = Serial.read();
  if (opcion == '\n' || opcion == '\r' || opcion == ' ') return;
  Serial.println(opcion);
  switch (opcion) {
    case '1': probarLuz(); break;
    case '2': probarRele(); break;
    case '3': probarServo(servoPestillo, "PESTILLO", 'p'); break;
    case '5': probarCortina(); break;
    case '6': probarBuzzers(); break;
    case '7': probarAnalogicos(); break;
    case '8': probarDigitales(); break;
    case '9': probarRfid(); break;
    case '0':
      probarLuz(); probarRele(); probarBuzzers();
      probarServo(servoPestillo, "PESTILLO", 'p');
      probarCortina();
      break;
    case 'p': calibrarServo(servoPestillo, "PESTILLO", "TIEMPO_PESTILLO_MS"); break;
    case 'c': calibrarCortina(); break;
    default: Serial.println(F("Opcion no valida"));
  }
  menu();
}
