// =====================================================================
//  Maqueta casa multimodal — modelo paramétrico (OpenSCAD)
//  Unidades: milímetros. Madera recomendada: MDF o triplex de 5 mm (base de 9 mm).
//
//  PIEZA = "ensamble"        -> vista 3D con componentes (F5)
//  PIEZA = "<pieza de madera>" -> plano 2D para cortar (exportar SVG/DXF con F6)
//  PIEZA = "<pieza impresa>"   -> pieza 3D para imprimir (exportar STL con F6)
//  Desde la terminal:  openscad -D 'PIEZA="frente"' -o frente.svg maqueta.scad
// =====================================================================
PIEZA = "ensamble";
MOSTRAR_TECHO = true;          // false para ver el interior desde arriba
MOSTRAR_TAPA_COMPARTIMENTO = false;
MOSTRAR_COMPONENTES = true;

// ---------------- dimensiones generales ----------------
T = 5;            // espesor de las paredes (MDF/triplex)
TB = 9;           // espesor de la base
ANCHO = 350;      // ancho exterior de la casa (X)
FONDO = 260;      // fondo exterior (Y)
H1 = 170;         // altura de la planta baja
H2 = 150;         // altura de la planta alta
H = H1 + T + H2;  // altura de los muros (325)
CUMBRE = 110;     // altura del triángulo del techo
COMP = 120;       // ancho del compartimento técnico
ALERO = 12;       // voladizo del techo

BASE_X0 = -45; BASE_Y0 = -150; BASE_W = ANCHO + COMP + 90; BASE_D = FONDO + 190;

// ---------------- aberturas de la fachada (coordenadas x, z de la pared) ----------------
PUERTA = [50, 0, 70, 140];          // x, z, ancho, alto
VENTANA = [130, 215, 100, 80];
TIMBRE = [28, 95];                  // centro del botón (pulsador 12x12)
BUZZER_ALARMA = [295, 265];         // centro
BUZZER_D = 23.5;                    // diámetro del buzzer de 12 V (medir el tuyo)
VENTILADOR = 40;                    // lado del ventilador (40 o 50 mm)
VENTILADOR_TORNILLOS = 32;          // distancia entre tornillos (32 para 40 mm, 40 para 50 mm)
VENTILADOR_C = [100, 255];          // centro en la pared trasera
PASACABLES = [[30, 40], [30, 205], [205, 255], [125, 120]];  // (y, z) en la pared compartida
PASACABLE_D = 16;

// ---------------- panel de control ----------------
PANEL_X = 195; PANEL_W = 280; PANEL_D = 110; PANEL_Y = -135; PANEL_H1 = 35; PANEL_H2 = 75;
POT_D = 7.5;                        // agujero para potenciómetro de eje M7

$fn = 48;
MADERA = [0.80, 0.64, 0.45];
MADERA2 = [0.72, 0.55, 0.38];

// =====================================================================
//  Utilidades de colocación
// =====================================================================
module placa_xz(x, y) { translate([x, y + T, 0]) rotate([90, 0, 0]) linear_extrude(T) children(); }
module placa_yz(x, y) { translate([x, y, 0]) rotate([90, 0, 90]) linear_extrude(T) children(); }
module placa_xy(x, y, z, e = T) { translate([x, y, z]) linear_extrude(e) children(); }
function ang_techo() = atan(CUMBRE / (ANCHO / 2));
function largo_techo() = sqrt(pow(ANCHO / 2, 2) + pow(CUMBRE, 2)) + ALERO;

// =====================================================================
//  PIEZAS DE MADERA (2D)
// =====================================================================
module muro_con_cumbre() { polygon([[0, 0], [ANCHO, 0], [ANCHO, H], [ANCHO / 2, H + CUMBRE], [0, H]]); }

module p_frente() {
  difference() {
    muro_con_cumbre();
    translate([PUERTA[0], PUERTA[1] - 1]) square([PUERTA[2], PUERTA[3] + 1]);
    translate([VENTANA[0], VENTANA[1]]) square([VENTANA[2], VENTANA[3]]);
    translate(TIMBRE) square(12.6, center = true);
    translate(BUZZER_ALARMA) circle(d = BUZZER_D);
    translate([VENTANA[0] + VENTANA[2] + 14, VENTANA[1] + VENTANA[3] - 8]) circle(d = 4);   // cable del KY-036
  }
}
module p_trasera() {
  difference() {
    muro_con_cumbre();
    translate(VENTILADOR_C) circle(d = VENTILADOR - 3);
    for (i = [-1, 1], j = [-1, 1]) translate(VENTILADOR_C + [i, j] * VENTILADOR_TORNILLOS / 2) circle(d = 3.4);
  }
}
module p_pared_compartida() {   // pared derecha de la casa = izquierda del compartimento
  difference() {
    square([FONDO - 2 * T, H]);
    for (p = PASACABLES) translate(p - [T, 0]) circle(d = PASACABLE_D);
  }
}
module p_entrepiso() {
  difference() {
    square([ANCHO - T, FONDO - 2 * T]);
    translate([(ANCHO - T) / 2, (FONDO - 2 * T) / 2]) circle(d = 5.2);     // LED de la luz
    translate([30, FONDO - 2 * T - 110 - 10]) square([60, 110]);            // hueco de la escalera (decorativa)
  }
}
module p_techo() { square([largo_techo(), FONDO + 2 * ALERO]); }
module p_comp_trasera() {
  difference() {
    square([COMP, H]);
    translate([COMP / 2, 150]) circle(d = 14);   // cable USB del ESP32
    translate([COMP / 2, 35]) circle(d = 12);    // entrada de la fuente de 5 V (jack o cables)
    for (i = [0:5]) translate([20 + i * 14, 260]) square([6, 40]);  // ventilación
  }
}
module p_comp_lateral() { square([FONDO - 2 * T, H]); }
module p_comp_frente() {   // tapa removible (se fija con imanes o 2 tornillos)
  difference() { square([COMP, H]); translate([COMP / 2, H - 25]) circle(d = 18); }
}
module p_comp_tapa() { square([COMP, FONDO]); }
module p_base() { square([BASE_W, BASE_D]); }
module p_panel_lado() { polygon([[0, 0], [PANEL_D, 0], [PANEL_D, PANEL_H2], [0, PANEL_H1]]); }
module p_panel_frente() { square([PANEL_W, PANEL_H1]); }
module p_panel_trasera() {
  difference() { square([PANEL_W, PANEL_H2]); translate([PANEL_W - 30, PANEL_H2 / 2]) circle(d = 14); }  // salida de cables
}
function largo_panel() = sqrt(pow(PANEL_D, 2) + pow(PANEL_H2 - PANEL_H1, 2));
module p_panel_tapa() {
  w = PANEL_W - 2 * T; l = largo_panel();
  difference() {
    square([w, l]);
    for (i = [0:3]) translate([110 + i * 40, l / 2]) circle(d = POT_D);  // potenciómetros
    translate([35, l / 2]) square([24, 14], center = true);               // DIP switch
    translate([75, l / 2]) square(12.6, center = true);                   // botón RESET (pin EN)
  }
}
module p_puerta() { square([PUERTA[2] - 2, PUERTA[3] - 2]); }

// =====================================================================
//  PIEZAS IMPRESAS (3D)
// =====================================================================
// Acople del eje del 28BYJ-48 (D de 5 mm con planos a 3 mm) a un palo de madera de 8 mm.
module imp_acople_28byj() {
  difference() {
    cylinder(d = 15, h = 22);
    translate([0, 0, -1]) intersection() { cylinder(d = 5.3, h = 10); cube([3.3, 10, 22], center = true); }
    translate([0, 0, 11]) cylinder(d = 8.4, h = 12);
  }
}
// Soporte del motor 28BYJ-48 (orejas a 35 mm, eje desplazado 8 mm del centro).
module imp_soporte_28byj() {
  difference() {
    union() {
      cube([50, 3, 45]);                                   // placa vertical (el motor se atornilla aquí)
      cube([50, 24, 3]);                                   // pie: se atornilla al techo/muro
    }
    translate([25, -1, 26]) rotate([-90, 0, 0]) {
      for (s = [-1, 1]) translate([s * 17.5, 0, 0]) cylinder(d = 4.3, h = 5);
      translate([0, -8, 0]) cylinder(d = 10, h = 5);       // paso del eje
    }
    for (x = [10, 40]) translate([x, 15, -1]) cylinder(d = 3.5, h = 5);
  }
}
// Soporte del otro extremo del rodillo (agujero para el palo de 8 mm a la altura del eje).
module imp_soporte_rodillo() {
  difference() {
    union() { cube([30, 3, 45]); cube([30, 24, 3]); }
    translate([15, -1, 34]) rotate([-90, 0, 0]) cylinder(d = 8.8, h = 5);
    translate([15, 15, -1]) cylinder(d = 3.5, h = 5);
  }
}
// Carrete para servo continuo: el brazo simple del servo se pega en la ranura de abajo.
module imp_carrete_servo() {
  difference() {
    union() { cylinder(d = 32, h = 2); cylinder(d = 16, h = 12); translate([0, 0, 10]) cylinder(d = 32, h = 2); }
    translate([0, 0, -1]) cylinder(d = 5.5, h = 14);                     // acceso al tornillo del eje
    translate([0, 0, -0.01]) { cylinder(d = 7.6, h = 2.6); translate([0, -3.1, 0]) cube([20, 6.2, 2.6]); }  // brazo del servo
    translate([5, 0, 6]) rotate([90, 0, 0]) cylinder(d = 1.8, h = 20, center = true);  // amarre del hilo
  }
}
// Soporte SG90/MG90: el servo entra por el hueco y queda con el eje hacia afuera del muro.
module imp_soporte_servo() {
  difference() {
    union() {
      cube([44, 24, 3]);
      for (x = [0, 38]) cube([6, 24, 18]);                // patas
      for (x = [-6, 44]) translate([x, 0, 15]) cube([6, 24, 3]);  // pestañas al muro
    }
    translate([22 - 11.7, 12 - 6.4, -1]) cube([23.4, 12.8, 5]);       // cuerpo del servo
    for (s = [-1, 1]) translate([22 + s * 13.9, 12, -1]) cylinder(d = 1.9, h = 5);  // tornillos del servo
    for (x = [-3, 47]) translate([x, 12, 14]) cylinder(d = 3.4, h = 5);
  }
}
// Pestillo: guía + cerrojo + cerradero. El servo tira del cerrojo con un hilo (abre);
// una liga lo devuelve (cierra). Así un servo continuo nunca fuerza el mecanismo.
module imp_guia_pestillo() {
  difference() {
    union() { cube([46, 16, 2.5]); translate([8, 0, 0]) cube([30, 16, 10]); translate([42, 8, 0]) cylinder(d = 4, h = 9); }
    translate([7, 4, 2.5]) cube([32, 8.2, 6.2]);
    for (x = [3.5]) translate([x, 8, -1]) cylinder(d = 3.2, h = 5);
  }
}
module imp_cerrojo() {
  difference() {
    union() { cube([55, 7.6, 5.6]); translate([46, 0, 0]) cube([6, 7.6, 12]); }   // tope que asoma por la ranura
    translate([50, 3.8, -1]) cylinder(d = 2, h = 20);                                 // amarre del hilo y la liga
  }
}
module imp_cerradero() {
  difference() {
    union() { cube([18, 16, 2.5]); translate([0, 2, 0]) cube([18, 12, 11]); }
    translate([-1, 3.7, 2.5]) cube([20, 8.6, 6.6]);
  }
}
module imp_soporte_reed() {
  difference() {
    cube([34, 10, 7]);
    translate([3, 5, 7]) rotate([0, 90, 0]) cylinder(d = 3.2, h = 28);
    for (x = [-1, 31]) translate([x, -1, 3]) cube([4, 12, 5]);   // salida de las patas del reed
  }
}
IMAN_D = 10; IMAN_H = 3;
module imp_soporte_iman() {
  difference() { cube([16, 16, IMAN_H + 2]); translate([8, 8, 2]) cylinder(d = IMAN_D + 0.4, h = IMAN_H + 1); }
}
module imp_marco_ventana() {   // marco exterior: se forra con cinta de aluminio y es la "zona" del KY-036
  difference() {
    translate([-8, -8]) square([VENTANA[2] + 16, VENTANA[3] + 16]);
    square([VENTANA[2], VENTANA[3]]);
  }
}

// =====================================================================
//  COMPONENTES (solo para la vista del ensamble)
// =====================================================================
module comp(c, a = 1) { if (MOSTRAR_COMPONENTES) color(c, a) children(); }
module componentes() {
  // --- fachada ---
  comp("SlateGray") translate([PUERTA[0] + 2, T, 1]) cube([PUERTA[2] - 4, 3, PUERTA[3] - 2]);           // hoja de puerta (cerrada)
  comp("Silver") translate([VENTANA[0] - 8, -1.5, VENTANA[1] - 8]) rotate([90, 0, 0]) linear_extrude(2) imp_marco_ventana_frame();
  comp("LightBlue", 0.6) translate([VENTANA[0] - 5, T, VENTANA[1] - 5]) cube([VENTANA[2] + 10, 3, VENTANA[3] + 10]);      // vidrio fijo
  comp("Black") translate([TIMBRE[0] - 6, -3, TIMBRE[1] - 6]) cube([12, 6, 12]);                         // botón timbre
  comp("Black") translate([BUZZER_ALARMA[0], 3, BUZZER_ALARMA[1]]) rotate([90, 0, 0]) cylinder(d = BUZZER_D - 0.5, h = 12);
  comp("RoyalBlue") translate([PUERTA[0] + PUERTA[2] + 12, T, 100]) cube([40, 3, 60]);                   // RFID RC522 (detrás del muro, encima del pestillo)
  comp("DimGray") translate([PUERTA[0] + PUERTA[2] + 18, -2, 114]) cube([28, 2, 32]);                    // marca del lector afuera
  // --- pestillo y reed en la puerta (por dentro) ---
  comp("White") translate([PUERTA[0] + PUERTA[2] + 3, T, 70]) cube([40, 16, 10]);                        // guía del pestillo
  comp("RoyalBlue") translate([PUERTA[0] + PUERTA[2] + 48, T, 62]) cube([23, 15, 25]);                   // servo pestillo
  comp("Red") translate([PUERTA[0] + PUERTA[2] - 34, T, PUERTA[3] + 6]) cube([34, 8, 7]);               // reed
  // --- planta alta: ventana, cortina, KY-036 ---
  comp("Goldenrod") translate([VENTANA[0] - 5, T + 22, VENTANA[1] + VENTANA[3] + 22]) rotate([0, 90, 0]) cylinder(d = 8, h = 125);  // rodillo
  comp("Beige", 0.8) translate([VENTANA[0] - 5, T + 21, VENTANA[1] + 25]) cube([110, 1, 45]);           // cortina (a medio bajar)
  comp("SteelBlue") translate([VENTANA[0] + 125, T + 22, VENTANA[1] + VENTANA[3] + 22]) rotate([0, 90, 0]) cylinder(d = 28, h = 19);  // 28BYJ-48
  comp("DarkRed") translate([VENTANA[0] + 10, T, H1 + T + 8]) cube([35, 3, 15]);                        // KY-036 (bajo la ventana)
  // --- interior ---
  comp("Black") translate([VENTILADOR_C[0] - VENTILADOR / 2, FONDO - T - 10, VENTILADOR_C[1] - VENTILADOR / 2]) cube([VENTILADOR, 10, VENTILADOR]);
  comp("DarkGreen") translate([ANCHO - T - 3, 150, 230]) cube([3, 20, 15]);                                // KY-028
  comp("Yellow") translate([(ANCHO - T) / 2, FONDO / 2, H1 - 6]) cylinder(d = 5, h = 6);                 // LED luz
  comp("Black") translate([ANCHO - T - 14, 60, 120]) rotate([0, 90, 0]) cylinder(d = BUZZER_D - 0.5, h = 12); // buzzer timbre
  // --- compartimento técnico ---
  X = ANCHO + COMP - T;
  comp("WhiteSmoke") translate([X - 10, 40, 110]) cube([10, 170, 55]);          // protoboard
  comp("DarkSlateGray") translate([X - 20, 95, 120]) cube([10, 52, 28]);        // ESP32
  comp("RoyalBlue") translate([X - 20, 60, 215]) cube([20, 50, 26]);            // relé
  comp("DarkGreen") translate([X - 16, 140, 265]) cube([16, 35, 32]);           // ULN2003
  comp("DimGray") translate([X - 18, 60, 25]) cube([18, 120, 30]);              // bornera de alimentación
  // --- panel de control ---
  translate([PANEL_X, PANEL_Y, 0]) {
    a = atan((PANEL_H2 - PANEL_H1) / PANEL_D);
    translate([T, 0, PANEL_H1]) rotate([a, 0, 0]) {
      for (i = [0:3]) comp("Black") translate([110 + i * 40, largo_panel() / 2, T]) cylinder(d = 13, h = 12);
      comp("Red") translate([35 - 11, largo_panel() / 2 - 6, T]) cube([22, 12, 6]);
      comp("Black") translate([75 - 6, largo_panel() / 2 - 6, T]) cube([12, 12, 7]);
    }
  }
}
module imp_marco_ventana_frame() { translate([8, 8]) imp_marco_ventana(); }

// =====================================================================
//  ENSAMBLE
// =====================================================================
module ensamble() {
  color(MADERA2) translate([BASE_X0, BASE_Y0, -TB]) linear_extrude(TB) p_base();
  color(MADERA) {
    placa_xz(0, 0) p_frente();
    placa_xz(0, FONDO - T) p_trasera();
    placa_yz(ANCHO - T, T) p_pared_compartida();
    placa_xy(0, T, H1) p_entrepiso();
    // compartimento técnico
    placa_xz(ANCHO, FONDO - T) p_comp_trasera();
    placa_yz(ANCHO + COMP - T, T) p_comp_lateral();
    if (MOSTRAR_TAPA_COMPARTIMENTO) { placa_xz(ANCHO, 0) p_comp_frente(); placa_xy(ANCHO, 0, H) p_comp_tapa(); }
    // panel de control
    translate([PANEL_X, PANEL_Y, 0]) {
      placa_yz(0, 0) p_panel_lado();
      placa_yz(PANEL_W - T, 0) p_panel_lado();
      placa_xz(0, 0) p_panel_frente();
      placa_xz(0, PANEL_D - T) p_panel_trasera();
      a = atan((PANEL_H2 - PANEL_H1) / PANEL_D);
      translate([T, 0, PANEL_H1]) rotate([a, 0, 0]) linear_extrude(T) p_panel_tapa();
    }
  }
  if (MOSTRAR_TECHO) color([0.25, 0.27, 0.30]) {
    a = ang_techo(); L = largo_techo();
    translate([ANCHO / 2, -ALERO, H + CUMBRE]) rotate([0, -a, 0]) translate([-L, 0, 0]) cube([L, FONDO + 2 * ALERO, T]);
    translate([ANCHO, 0, 0]) mirror([1, 0, 0])
      translate([ANCHO / 2, -ALERO, H + CUMBRE]) rotate([0, -a, 0]) translate([-L, 0, 0]) cube([L, FONDO + 2 * ALERO, T]);
  }
  componentes();
}

// =====================================================================
//  Selector de pieza
// =====================================================================
if (PIEZA == "ensamble") ensamble();
// madera (2D)
else if (PIEZA == "frente") p_frente();
else if (PIEZA == "trasera") p_trasera();
else if (PIEZA == "pared_compartida") p_pared_compartida();
else if (PIEZA == "entrepiso") p_entrepiso();
else if (PIEZA == "techo") p_techo();
else if (PIEZA == "comp_trasera") p_comp_trasera();
else if (PIEZA == "comp_lateral") p_comp_lateral();
else if (PIEZA == "comp_frente") p_comp_frente();
else if (PIEZA == "comp_tapa") p_comp_tapa();
else if (PIEZA == "base") p_base();
else if (PIEZA == "panel_lado") p_panel_lado();
else if (PIEZA == "panel_frente") p_panel_frente();
else if (PIEZA == "panel_trasera") p_panel_trasera();
else if (PIEZA == "panel_tapa") p_panel_tapa();
else if (PIEZA == "puerta") p_puerta();
else if (PIEZA == "marco_ventana") imp_marco_ventana();
// impresas (3D)
else if (PIEZA == "acople_28byj") imp_acople_28byj();
else if (PIEZA == "soporte_28byj") imp_soporte_28byj();
else if (PIEZA == "soporte_rodillo") imp_soporte_rodillo();
else if (PIEZA == "carrete_servo") imp_carrete_servo();
else if (PIEZA == "soporte_servo") imp_soporte_servo();
else if (PIEZA == "guia_pestillo") imp_guia_pestillo();
else if (PIEZA == "cerrojo") imp_cerrojo();
else if (PIEZA == "cerradero") imp_cerradero();
else if (PIEZA == "soporte_reed") imp_soporte_reed();
else if (PIEZA == "soporte_iman") imp_soporte_iman();
