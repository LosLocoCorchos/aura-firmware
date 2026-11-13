// Codec de ChirpStack v4 para E2-AZ-TERR-TAN01: nivel de tres tanques de agua (clase A).
// Es el par de E2-AZ-TERR-TAN01.ino: si cambia el formato de los bytes de uno, cambia el otro.
// Sigue el esquema del codec de E1-AZ-TERR-TAN01 (puertos, ref y reporte de comandos).
// Lo que devuelve decodeUplink es el "object" del uplink: el lorawan-bridge lo publica como
// "values" en hw/eui-<DevEUI>/data, o como "response" si es un reporte de comando
// (contrato v4.0, §3.1, §3.3 y §3.4).
//
// SUBIDA
// fPort 1, medicion: [0] mascara de tanques (bit0 = tanque 1 ... bit2 = tanque 3) y, por cada
//   tanque de la mascara, en orden: distancia en mm = cm x 10 (uint16) + nivel en decimas de %
//   (uint16, 0..1000; 0xFFFF = tanque sin calibrar). Big endian.
//   Cada ciclo (y medir_ahora) manda los tres tanques: 13 bytes. Un tanque cuyo sensor no midio
//   no va en la mascara.
//   values: distancia_tanqueN_cm (cm, un decimal) y nivel_tanqueN_pct (%, un decimal; lo calcula
//   el nodo con la altura y el lleno del tanque).
//   0xFFFE = el agua esta dentro de la zona ciega del sensor (tanque lleno). No es una falla:
//   se informa 25 cm, el nivel maximo que el sensor puede medir (saturacion del rango, como
//   un transmisor 4-20 mA en su tope). Quiere decir "25 cm o menos".
//   0xFFFF sin eco, 0xFFFD demasiado lejos o cualquier otro valor fuera de 25..600 cm: falla,
//   el tanque no aparece en values (§3.1). El firmware ya no los manda; el codec igual los filtra.
//   Mascara 0 (4 bytes): mensaje de estado, ningun sensor midio; los 3 bytes siguientes son la
//   falla de cada tanque (1 sin eco, 2 demasiado lejos). No hay values: el codec devuelve
//   errors con la falla de cada tanque. El nodo lo manda para no quedar mudo.
// fPort 2, 16 bytes, reporte de un comando: [0] ref  [1] resultado (0 aplicado, 1 fuera de
//   rango, 2 comando desconocido, 3 calibracion invalida)  [2..3] intervalo_s
//   [4..9] altura de los tanques 1..3 en mm  [10..15] lleno de los tanques 1..3 en mm
//   (0xFFFF = sin definir: va como null). Es la config vigente del nodo: va en "config".
//
// BAJADA (fPort 10). El lorawan-bridge pasa {command, params, ref}:
//   medir_ahora -> 01 ref
//   set_config solo con intervalo_s -> 02 ref hi lo (4 bytes: entra en un profile con
//   aura_max_downlink_bytes = 4)
//   set_config con altura o lleno -> 02 ref y una entrada (id hi lo) por parametro:
//     intervalo_s        (60..43200 s, entero)      id 0x01, en s
//     altura_tanqueN_cm  (25..600 cm, un decimal)   id 0x10 + N, en mm
//     lleno_tanqueN_cm   (25..600 cm, un decimal)   id 0x20 + N, en mm
//   altura = distancia del sensor (en la tapa) al fondo del tanque: lo que mide vacio (0 %);
//   lleno = distancia del sensor al agua con el tanque lleno (100 %). La altura tiene que ser
//   al menos 10 cm mayor que el lleno. Por defecto, el nodo usa el tanque Mayper de 5000 L
//   (altura 238 cm, lleno 35 cm). cero_tanqueN_cm se acepta como otro nombre de la altura.
//   null o 0 deja el tanque sin altura (o sin lleno): el nodo manda la distancia sin nivel.
//   intervalo_s es cada cuanto se miden los tres tanques y se manda la medicion.

var N_TANQUES = 3;
var DIST_MIN_MM = 250;            // iguales al sketch; en values: 25 a 600 cm (conector.toml)
var DIST_MAX_MM = 6000;
var ZONA_CIEGA = 0xFFFE;
var SIN_CALIBRAR = 0xFFFF;
var SPAN_MIN_MM = 100;
var INTERVALO_MIN_S = 60;
var INTERVALO_MAX_S = 43200;      // 12 h; el uplink interval del device profile tiene que ser este maximo
var MOTIVOS = { 1: "fuera_de_rango", 2: "comando_desconocido", 3: "calibracion_invalida" };
var FALLAS = { 0xFFFF: "sin eco", 0xFFFD: "demasiado lejos" };
var FALLAS_ESTADO = { 1: "sin eco", 2: "demasiado lejos" };

function u16(bytes, i) {
  return (bytes[i] << 8) | bytes[i + 1];
}

function decimas(v) {
  return v / 10;                  // 2462 -> 246.2: un decimal exacto
}

function decodeUplink(input) {
  var b = input.bytes;
  if (input.fPort === 2) return decodificarReporte(b);
  if (input.fPort !== 1) {
    return { errors: ["fPort " + input.fPort + " desconocido (1 medicion, 2 reporte)"] };
  }
  if (b.length < 1) return { errors: ["medicion vacia"] };

  var mascara = b[0];
  if (mascara === 0) {
    if (b.length !== 1 + N_TANQUES) {
      return { errors: ["estado: " + b.length + " bytes, se esperaban " + (1 + N_TANQUES)] };
    }
    var fallas = [];
    for (var k = 0; k < N_TANQUES; k++) {
      fallas.push("tanque " + (k + 1) + " " + (FALLAS_ESTADO[b[1 + k]] || "falla " + b[1 + k]));
    }
    return { errors: ["ningun sensor midio: " + fallas.join(", ")] };
  }
  var cantidad = 0;
  for (var t = 0; t < N_TANQUES; t++) if (mascara & (1 << t)) cantidad++;
  if ((mascara >> N_TANQUES) !== 0) {
    return { errors: ["medicion: mascara de tanques invalida 0x" + mascara.toString(16)] };
  }
  if (b.length !== 1 + 4 * cantidad) {
    return { errors: ["medicion: " + b.length + " bytes, se esperaban " + (1 + 4 * cantidad)] };
  }

  var values = {};
  var warnings = [];
  var i = 1;
  for (var n = 1; n <= N_TANQUES; n++) {
    if (!(mascara & (1 << (n - 1)))) continue;
    var dist = u16(b, i);
    var nivel = u16(b, i + 2);
    i += 4;
    if (dist === ZONA_CIEGA) {
      values["distancia_tanque" + n + "_cm"] = decimas(DIST_MIN_MM);
      warnings.push("tanque " + n + ": agua en la zona ciega (lleno), se informa " + decimas(DIST_MIN_MM) + " cm");
    } else if (dist >= DIST_MIN_MM && dist <= DIST_MAX_MM) {
      values["distancia_tanque" + n + "_cm"] = decimas(dist);
    } else {
      warnings.push("tanque " + n + ": " + (FALLAS[dist] || "fuera de rango (" + decimas(dist) + " cm)"));
      continue;   // sin distancia valida tampoco hay nivel
    }
    if (nivel <= 1000) values["nivel_tanque" + n + "_pct"] = decimas(nivel);
  }
  // Si ningun tanque dio una medicion valida, no se publica data (§3.1).
  if (Object.keys(values).length === 0) {
    return { errors: ["sin lecturas validas: " + warnings.join(", ")] };
  }
  return { data: values, warnings: warnings };
}

function decodificarReporte(b) {
  if (b.length !== 4 + 4 * N_TANQUES) {
    return { errors: ["reporte: " + b.length + " bytes, se esperaban " + (4 + 4 * N_TANQUES)] };
  }
  var config = { intervalo_s: u16(b, 2) };
  for (var t = 0; t < N_TANQUES; t++) {
    var cero = u16(b, 4 + 2 * t);
    var lleno = u16(b, 4 + 2 * N_TANQUES + 2 * t);
    config["altura_tanque" + (t + 1) + "_cm"] = cero === SIN_CALIBRAR ? null : decimas(cero);
    config["lleno_tanque" + (t + 1) + "_cm"] = lleno === SIN_CALIBRAR ? null : decimas(lleno);
  }
  var rep = { ref: b[0], aplicado: b[1] === 0, config: config };
  if (b[1] !== 0) rep.motivo = MOTIVOS[b[1]] || "rechazado_por_el_nodo";
  return { data: rep };
}

// Un valor en cm con a lo sumo un decimal, dentro de 25..600: devuelve los mm, o -1.
function cmAMm(v) {
  if (typeof v !== "number" || !isFinite(v)) return -1;
  var mm = Math.round(v * 10);
  if (Math.abs(mm - v * 10) > 1e-6) return -1;
  if (mm < DIST_MIN_MM || mm > DIST_MAX_MM) return -1;
  return mm;
}

function encodeDownlink(input) {
  var d = input.data || {};
  var ref = d.ref;
  if (typeof ref !== "number" || ref % 1 !== 0 || ref < 1 || ref > 255) {
    return { errors: ["falta ref (1-255): lo pone el lorawan-bridge"] };
  }
  if (d.command === "medir_ahora") {
    return { bytes: [0x01, ref] };
  }
  if (d.command !== "set_config") {
    return { errors: ["comando desconocido: " + d.command] };
  }

  var p = d.params || {};
  var claves = Object.keys(p);
  if (claves.length === 0) return { errors: ["set_config sin parametros"] };

  var bytes = [0x02, ref];
  var cero = {}, lleno = {};
  for (var i = 0; i < claves.length; i++) {
    var k = claves[i];
    var v = p[k];
    var m = /^(altura|cero|lleno)_tanque([1-3])_cm$/.exec(k);
    if (k === "intervalo_s") {
      if (typeof v !== "number" || v % 1 !== 0 || v < INTERVALO_MIN_S || v > INTERVALO_MAX_S) {
        return { errors: ["set_config: intervalo_s tiene que ser un entero entre " + INTERVALO_MIN_S + " y " + INTERVALO_MAX_S] };
      }
      bytes.push(0x01, (v >> 8) & 0xff, v & 0xff);
    } else if (m) {
      var mm = (v === null || v === 0) ? 0 : cmAMm(v);   // null o 0: sin altura / sin lleno
      if (mm < 0) return { errors: ["set_config: " + k + " tiene que ser un numero de 25 a 600 cm, con a lo sumo un decimal (o null para dejarlo sin definir)"] };
      var tanque = Number(m[2]);
      var esAltura = m[1] !== "lleno";   // cero_ es el nombre viejo de la altura
      if (esAltura) {
        if (cero[tanque] !== undefined) return { errors: ["set_config: altura_tanque" + tanque + "_cm y cero_tanque" + tanque + "_cm son lo mismo: usar uno solo"] };
        cero[tanque] = mm;
      } else {
        lleno[tanque] = mm;
      }
      bytes.push((esAltura ? 0x10 : 0x20) + tanque, (mm >> 8) & 0xff, mm & 0xff);
    } else {
      return { errors: ["set_config: parametro desconocido '" + k + "' (intervalo_s, altura_tanqueN_cm, lleno_tanqueN_cm)"] };
    }
  }
  // Solo el intervalo: la forma corta de 4 bytes (02 ref hi lo).
  if (claves.length === 1 && claves[0] === "intervalo_s") {
    return { bytes: [0x02, ref, bytes[3], bytes[4]] };
  }
  // Si vienen los dos de un tanque, se controla aca; si viene uno solo, lo controla el nodo
  // contra el que tiene guardado.
  for (var t = 1; t <= N_TANQUES; t++) {
    if (cero[t] && lleno[t] && cero[t] < lleno[t] + SPAN_MIN_MM) {
      return { errors: ["set_config: en el tanque " + t + " la altura tiene que ser al menos 10 cm mayor que el lleno"] };
    }
  }
  return { bytes: bytes };
}

// ChirpStack no define "module": solo existe cuando se prueba el codec con Node (tests/).
if (typeof module !== "undefined") {
  module.exports = { decodeUplink: decodeUplink, encodeDownlink: encodeDownlink };
}
