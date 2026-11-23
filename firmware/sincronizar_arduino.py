"""Genera las carpetas de sketch para Arduino IDE a partir de firmware/src.

El código fuente "oficial" está en src/ (PlatformIO). Ejecuta este script después de
cualquier cambio en src/ para actualizar las copias de Arduino IDE:

    python sincronizar_arduino.py

Resultado:
    arduino/pruebas/pruebas.ino                 (programa de pruebas por componente)
    arduino/casa_multimodal/casa_multimodal.ino (firmware principal)
Cada carpeta lleva su copia de pines.h y motor_servo.h. No edites esas copias: se sobrescriben.
"""
from pathlib import Path
import shutil

BASE = Path(__file__).resolve().parent
SRC = BASE / "src"
HEADERS = ["pines.h", "motor_servo.h", "motor_paso.h", "zumbador.h", "comunicacion.h"]
SKETCHES = {"pruebas": "pruebas.cpp", "casa_multimodal": "main.cpp"}
AVISO = "// ARCHIVO GENERADO por sincronizar_arduino.py desde firmware/src. No lo edites aquí.\n"

for sketch, source in SKETCHES.items():
    folder = BASE / "arduino" / sketch
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f"{sketch}.ino").write_text(AVISO + (SRC / source).read_text(encoding="utf8"), encoding="utf8")
    for header in HEADERS:
        shutil.copyfile(SRC / header, folder / header)
    print(f"Actualizado: {folder.relative_to(BASE)}")
