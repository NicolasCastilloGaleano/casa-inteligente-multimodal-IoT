"""Genera los archivos de fabricación de la maqueta a partir de maqueta.scad.

    python generar.py            -> todo (planos SVG, piezas STL y renders)
    python generar.py svg        -> solo planos de madera
    python generar.py stl        -> solo piezas impresas
    python generar.py png        -> solo renders

Requiere OpenSCAD instalado (winget install OpenSCAD.OpenSCAD).
Si cambias una medida en maqueta.scad, vuelve a ejecutar este script.
"""
from pathlib import Path
import shutil
import subprocess
import sys

BASE = Path(__file__).resolve().parent
SCAD = BASE / "maqueta.scad"
OPENSCAD = shutil.which("openscad") or r"C:\Program Files\OpenSCAD\openscad.com"

# pieza: cantidad a cortar
MADERA = {
    "frente": 1, "trasera": 1, "pared_compartida": 1, "entrepiso": 1, "techo": 2,
    "comp_trasera": 1, "comp_lateral": 1, "comp_frente": 1, "comp_tapa": 1, "base": 1,
    "panel_lado": 2, "panel_frente": 1, "panel_trasera": 1, "panel_tapa": 1, "puerta": 1, "marco_ventana": 1,
}
IMPRESAS = {
    "acople_28byj": 1, "soporte_28byj": 1, "soporte_rodillo": 1, "carrete_servo": 2, "soporte_servo": 2,
    "guia_pestillo": 1, "cerrojo": 1, "cerradero": 1, "riel_ventana": 2, "soporte_reed": 1, "soporte_iman": 1,
}
RENDERS = {
    "vista_frontal": ("-700,-1050,820,240,60,150", {}),
    "vista_interior": ("-1000,1150,900,190,40,170", {"MOSTRAR_TECHO": "false"}),
    "vista_compartimento": ("200,-1350,820,420,130,150", {"MOSTRAR_TECHO": "false"}),
}


def correr(salida: Path, definiciones: dict[str, str], extra: list[str] | None = None) -> None:
    args = [OPENSCAD, "-o", str(salida)]
    for clave, valor in definiciones.items():
        args += ["-D", f"{clave}={valor}"]
    args += (extra or []) + [str(SCAD)]
    resultado = subprocess.run(args, capture_output=True, text=True)
    if resultado.returncode != 0 or not salida.exists():
        print(resultado.stderr)
        raise SystemExit(f"Error generando {salida.name}")
    print(f"  {salida.relative_to(BASE)}")


def main() -> None:
    que = set(sys.argv[1:]) or {"svg", "stl", "png"}
    if "svg" in que:
        print("Planos de madera (SVG, escala 1:1 en mm):")
        (BASE / "corte_madera").mkdir(exist_ok=True)
        for pieza in MADERA:
            correr(BASE / "corte_madera" / f"{pieza}.svg", {"PIEZA": f'"{pieza}"'})
    if "stl" in que:
        print("Piezas para imprimir (STL):")
        (BASE / "piezas_stl").mkdir(exist_ok=True)
        for pieza in IMPRESAS:
            correr(BASE / "piezas_stl" / f"{pieza}.stl", {"PIEZA": f'"{pieza}"'})
    if "png" in que:
        print("Renders:")
        (BASE / "renders").mkdir(exist_ok=True)
        for nombre, (camara, defs) in RENDERS.items():
            correr(BASE / "renders" / f"{nombre}.png", defs, ["--imgsize=1600,1200", f"--camera={camara}", "--colorscheme=Tomorrow"])
        for pieza in IMPRESAS:   # miniatura de cada pieza impresa
            correr(BASE / "renders" / f"pieza_{pieza}.png", {"PIEZA": f'"{pieza}"'},
                   ["--imgsize=500,400", "--viewall", "--autocenter", "--colorscheme=Tomorrow"])


if __name__ == "__main__":
    main()
