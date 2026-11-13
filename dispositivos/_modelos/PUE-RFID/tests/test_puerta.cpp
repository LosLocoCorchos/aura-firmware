#include "../main/puerta_logica.h"
#include "../../../../comun/tests/aserciones.h"
#include <vector>
#include <string>

static const uint8_t A[4] = {0xAB, 0xE8, 0xB0, 0x22};
static const uint8_t B[4] = {0x4B, 0x3E, 0xC2, 0x22};

// Saca todas las acciones pendientes, en orden
static std::vector<accion_t> vaciar(puerta_t &p) {
  std::vector<accion_t> v;
  accion_t a;
  while (puerta_sacar(&p, &a)) v.push_back(a);
  return v;
}

static int contar(const std::vector<accion_t> &v, accion_tipo_t tipo) {
  int n = 0;
  for (auto &a : v) n += a.tipo == tipo;
  return n;
}

static bool hay(const std::vector<accion_t> &v, accion_tipo_t tipo, const char *texto) {
  for (auto &a : v)
    if (a.tipo == tipo && strcmp(a.texto, texto) == 0) return true;
  return false;
}

static bool hay_sonido(const std::vector<accion_t> &v, sonido_t s) {
  for (auto &a : v)
    if (a.tipo == ACC_SONIDO && a.valor == s) return true;
  return false;
}

static bool hay_aviso(const std::vector<accion_t> &v, aviso_t m) {
  for (auto &a : v)
    if (a.tipo == ACC_AVISO && a.valor == m) return true;
  return false;
}

static int indice(const std::vector<accion_t> &v, accion_tipo_t tipo, const char *texto) {
  for (size_t i = 0; i < v.size(); i++)
    if (v[i].tipo == tipo && strcmp(v[i].texto, texto) == 0) return (int)i;
  return -1;
}

static puerta_t nueva(bool armada = false, bool abierta = false) {
  puerta_t p;
  puerta_iniciar(&p, 0, armada, abierta, PULSO_DEFECTO_MS, AVISO_DEFECTO_S);
  return p;
}

int main() {
  // ---- U1: llavero autorizado: pulso, pitido de OK y registro; al abrirse la puerta se corta ----
  {
    puerta_t p = nueva();
    puerta_tarjeta(&p, 1000, A, 4, true);
    auto v = vaciar(p);
    VERIFICAR(contar(v, ACC_PULSO) == 1);
    VERIFICAR(hay(v, ACC_EVENTO, "acceso"));
    VERIFICAR(hay_sonido(v, SON_OK));
    VERIFICAR(hay_aviso(v, AVISO_PITIDOS_PULSO));
    for (auto &a : v)
      if (a.tipo == ACC_PULSO) VERIFICAR(a.valor == PULSO_DEFECTO_MS);
    for (auto &a : v)
      if (a.tipo == ACC_EVENTO) VERIFICAR(a.uid_largo == 4 && memcmp(a.uid, A, 4) == 0);

    puerta_sensor(&p, 2000, true);
    v = vaciar(p);
    VERIFICAR(contar(v, ACC_CORTAR_PULSO) == 1);
    VERIFICAR(hay(v, ACC_EVENTO, "apertura"));
    puerta_tick(&p, 2010);
    VERIFICAR(vaciar(p).empty());            // el pulso ya no esta: no se corta dos veces
  }

  // ---- U1: si nadie abre, traba al vencer el pulso ----
  {
    puerta_t p = nueva();
    puerta_tarjeta(&p, 1000, A, 4, true);
    vaciar(p);
    puerta_tick(&p, 1000 + PULSO_DEFECTO_MS - 1);
    VERIFICAR(contar(vaciar(p), ACC_CORTAR_PULSO) == 0);
    puerta_tick(&p, 1000 + PULSO_DEFECTO_MS);
    VERIFICAR(contar(vaciar(p), ACC_CORTAR_PULSO) == 1);
  }

  // ---- U2: llavero desconocido: rechazo con el UID ----
  {
    puerta_t p = nueva();
    puerta_tarjeta(&p, 1000, B, 4, false);
    auto v = vaciar(p);
    VERIFICAR(hay(v, ACC_EVENTO, "rechazo"));
    VERIFICAR(hay_sonido(v, SON_RECHAZO));
    VERIFICAR(contar(v, ACC_PULSO) == 0);
    for (auto &a : v)
      if (a.tipo == ACC_EVENTO) VERIFICAR(a.uid_largo == 4 && memcmp(a.uid, B, 4) == 0);
  }

  // UID de largo invalido: se ignora
  {
    puerta_t p = nueva();
    puerta_tarjeta(&p, 1000, A, 0, true);
    puerta_tarjeta(&p, 1000, A, 5, true);
    VERIFICAR(vaciar(p).empty());
  }

  // ---- U3: la misma tarjeta apoyada cuenta una sola vez ----
  {
    puerta_t p = nueva();
    puerta_tarjeta(&p, 1000, A, 4, true);
    puerta_tarjeta(&p, 1100, A, 4, true);
    puerta_tarjeta(&p, 1500, A, 4, true);
    auto v = vaciar(p);
    VERIFICAR(contar(v, ACC_PULSO) == 1);
    VERIFICAR(contar(v, ACC_EVENTO) == 1);
  }

  // ---- U4: muchos rechazos: frena las lecturas y avisa ----
  {
    puerta_t p = nueva();
    for (int i = 0; i < RECHAZOS_PARA_BLOQUEO; i++) {
      uint8_t u[4] = {0x01, 0x02, 0x03, (uint8_t)i};     // distintas, para que el filtro de repeticion no las junte
      puerta_tarjeta(&p, 1000 + i * 1000, u, 4, false);
    }
    auto v = vaciar(p);
    VERIFICAR(hay(v, ACC_ALERTA, "intentos_rechazados"));
    VERIFICAR(p.bloqueado);
    // Bloqueado: ni siquiera un llavero bueno abre
    puerta_tarjeta(&p, 10000, A, 4, true);
    VERIFICAR(vaciar(p).empty());
    // Pasado el bloqueo, vuelve a andar
    puerta_tarjeta(&p, 6000 + BLOQUEO_MS + 100, A, 4, true);
    v = vaciar(p);
    VERIFICAR(contar(v, ACC_PULSO) == 1);
    VERIFICAR(!p.bloqueado);
  }
  // Pocos rechazos espaciados no bloquean
  {
    puerta_t p = nueva();
    for (int i = 0; i < 10; i++) {
      uint8_t u[4] = {0x01, 0x02, 0x03, (uint8_t)i};
      puerta_tarjeta(&p, i * 20000LL, u, 4, false);
    }
    VERIFICAR(!p.bloqueado);
    VERIFICAR(!hay(vaciar(p), ACC_ALERTA, "intentos_rechazados"));
  }

  // ---- U5: salida por el picaporte con el sistema desarmado: apertura sin acceso, sin alarma ----
  {
    puerta_t p = nueva();
    puerta_sensor(&p, 1000, true);
    auto v = vaciar(p);
    VERIFICAR(hay(v, ACC_EVENTO, "apertura"));
    VERIFICAR(!hay(v, ACC_ALERTA, "intrusion"));
    VERIFICAR(!hay_aviso(v, AVISO_INTRUSION));
    VERIFICAR(contar(v, ACC_ALERTA) == 0);
  }

  // ---- U6: puerta abierta de mas: aviso local con tope de 2 min, alerta, y al cerrarse otra info ----
  {
    puerta_t p = nueva();
    puerta_sensor(&p, 1000, true);
    vaciar(p);
    puerta_tick(&p, 1000 + AVISO_DEFECTO_S * 1000 - 1);
    VERIFICAR(vaciar(p).empty());
    int64_t t0 = 1000 + AVISO_DEFECTO_S * 1000;
    puerta_tick(&p, t0);
    auto v = vaciar(p);
    VERIFICAR(hay_aviso(v, AVISO_PUERTA_ABIERTA));
    VERIFICAR(hay(v, ACC_ALERTA, "puerta_abierta"));
    for (auto &a : v)
      if (a.tipo == ACC_AVISO) VERIFICAR(a.hasta_ms - a.desde_ms == TOPE_AVISO_MS);
    puerta_tick(&p, t0 + 5000);
    VERIFICAR(vaciar(p).empty());                    // una sola alerta
    puerta_tick(&p, t0 + TOPE_AVISO_MS);
    v = vaciar(p);
    VERIFICAR(hay_aviso(v, AVISO_NADA));             // tope de 2 min: calla
    puerta_sensor(&p, t0 + TOPE_AVISO_MS + 1000, false);
    v = vaciar(p);
    VERIFICAR(hay(v, ACC_EVENTO, "cierre"));
    VERIFICAR(hay(v, ACC_ALERTA, "puerta_cerrada"));
  }
  // Cerrar antes del aviso: no hay aviso ni alerta
  {
    puerta_t p = nueva();
    puerta_sensor(&p, 1000, true);
    puerta_sensor(&p, 5000, false);
    puerta_tick(&p, 1000 + AVISO_DEFECTO_S * 1000 + 1);
    auto v = vaciar(p);
    VERIFICAR(!hay_aviso(v, AVISO_PUERTA_ABIERTA));
    VERIFICAR(contar(v, ACC_ALERTA) == 0);
  }
  // Pasar un llavero con la puerta abierta posterga el aviso
  {
    puerta_t p = nueva();
    puerta_sensor(&p, 0, true);
    vaciar(p);
    puerta_tarjeta(&p, 10000, A, 4, true);
    auto v = vaciar(p);
    VERIFICAR(contar(v, ACC_PULSO) == 0);            // abierta: no tiene sentido energizar
    puerta_tick(&p, AVISO_DEFECTO_S * 1000 + 1);
    VERIFICAR(!hay_aviso(vaciar(p), AVISO_PUERTA_ABIERTA));
    puerta_tick(&p, 10000 + AVISO_DEFECTO_S * 1000);
    VERIFICAR(hay_aviso(vaciar(p), AVISO_PUERTA_ABIERTA));
  }

  // ---- U7: armar con dos pasadas en <= 3 s y la puerta cerrada; la segunda corta el pulso ----
  {
    puerta_t p = nueva();
    puerta_tarjeta(&p, 1000, A, 4, true);
    vaciar(p);
    puerta_tarjeta(&p, 1000 + DOBLE_PASADA_MS - 100, A, 4, true);
    auto v = vaciar(p);
    VERIFICAR(p.armada);
    VERIFICAR(contar(v, ACC_CORTAR_PULSO) == 1);
    VERIFICAR(hay_sonido(v, SON_ARMADO));
    VERIFICAR(hay(v, ACC_EVENTO, "armado"));
    bool guardo = false;
    for (auto &a : v)
      if (a.tipo == ACC_GUARDAR_ARMADA && a.valor == 1) guardo = true;
    VERIFICAR(guardo);
    VERIFICAR(contar(v, ACC_PULSO) == 0);
    VERIFICAR(puerta_latido_s(&p) == LATIDO_ARMADA_S);
  }
  // Pasadas separadas por mas de 3 s no arman: cada una es un acceso
  {
    puerta_t p = nueva();
    puerta_tarjeta(&p, 1000, A, 4, true);
    puerta_tarjeta(&p, 1000 + DOBLE_PASADA_MS + 1, A, 4, true);
    VERIFICAR(!p.armada);
    VERIFICAR(contar(vaciar(p), ACC_PULSO) == 2);
  }

  // ---- U8: armar con la puerta abierta se rechaza con un pitido de error ----
  {
    puerta_t p = nueva();
    puerta_tarjeta(&p, 1000, A, 4, true);
    puerta_sensor(&p, 1500, true);
    vaciar(p);
    puerta_tarjeta(&p, 2500, A, 4, true);
    auto v = vaciar(p);
    VERIFICAR(!p.armada);
    VERIFICAR(hay_sonido(v, SON_ERROR));
    VERIFICAR(!hay(v, ACC_EVENTO, "armado"));
  }

  // ---- U9: una pasada desarma y abre; la tercera corrige un armado sin querer ----
  {
    puerta_t p = nueva();
    puerta_tarjeta(&p, 1000, A, 4, true);
    puerta_tarjeta(&p, 2000, A, 4, true);       // armo (la 2da pasada, sacando y volviendo a apoyar el llavero)
    vaciar(p);
    VERIFICAR(p.armada);
    puerta_tarjeta(&p, 3000, A, 4, true);       // tercera pasada
    auto v = vaciar(p);
    VERIFICAR(!p.armada);
    VERIFICAR(hay(v, ACC_EVENTO, "desarmado"));
    VERIFICAR(contar(v, ACC_PULSO) == 1);       // y abre
    bool guardo = false;
    for (auto &a : v)
      if (a.tipo == ACC_GUARDAR_ARMADA && a.valor == 0) guardo = true;
    VERIFICAR(guardo);
    VERIFICAR(puerta_latido_s(&p) == LATIDO_DESARMADA_S);
  }

  // ---- U10: intrusion. La alerta es LO PRIMERO que sale ----
  {
    puerta_t p = nueva(true, false);
    VERIFICAR(vaciar(p).empty());
    puerta_sensor(&p, 5000, true);
    auto v = vaciar(p);
    VERIFICAR(!v.empty());
    VERIFICAR(v[0].tipo == ACC_ALERTA);
    VERIFICAR(strcmp(v[0].texto, "intrusion") == 0);
    VERIFICAR(v[0].severidad == SEV_CRITICAL);
    VERIFICAR(indice(v, ACC_EVENTO, "intrusion") > 0);
    VERIFICAR(hay_aviso(v, AVISO_INTRUSION));
    for (auto &a : v)
      if (a.tipo == ACC_AVISO) VERIFICAR(a.hasta_ms - a.desde_ms == TOPE_AVISO_MS);
    // Tope de 2 min en el sonido
    puerta_tick(&p, 5000 + TOPE_AVISO_MS - 1);
    VERIFICAR(vaciar(p).empty());
    puerta_tick(&p, 5000 + TOPE_AVISO_MS);
    VERIFICAR(hay_aviso(vaciar(p), AVISO_NADA));
    // Al cerrarse: alerta info
    puerta_sensor(&p, 5000 + TOPE_AVISO_MS + 10000, false);
    v = vaciar(p);
    VERIFICAR(hay(v, ACC_ALERTA, "puerta_cerrada"));
    VERIFICAR(p.armada);                             // sigue armada: otra apertura es otra intrusion
    puerta_sensor(&p, 300000, true);
    VERIFICAR(hay(vaciar(p), ACC_ALERTA, "intrusion"));
  }
  // Llavero con la sirena sonando: corta la sirena y desarma
  {
    puerta_t p = nueva(true, false);
    puerta_sensor(&p, 1000, true);
    vaciar(p);
    puerta_tarjeta(&p, 4000, A, 4, true);
    auto v = vaciar(p);
    VERIFICAR(!p.armada);
    VERIFICAR(hay_aviso(v, AVISO_NADA));
    VERIFICAR(hay(v, ACC_EVENTO, "desarmado"));
    VERIFICAR(contar(v, ACC_PULSO) == 0);            // la puerta ya esta abierta
    VERIFICAR(!p.intrusion);
  }
  // Con la puerta armada, entrar con llavero NO es intrusion
  {
    puerta_t p = nueva(true, false);
    puerta_tarjeta(&p, 1000, A, 4, true);
    puerta_sensor(&p, 2000, true);
    auto v = vaciar(p);
    VERIFICAR(!hay(v, ACC_ALERTA, "intrusion"));
    VERIFICAR(!hay_aviso(v, AVISO_INTRUSION));
  }

  // ---- U11: arranque armada y con la puerta abierta: alerta propia, sin sonido ----
  {
    puerta_t p = nueva(true, true);
    auto v = vaciar(p);
    VERIFICAR(hay(v, ACC_ALERTA, "arranque_puerta_abierta"));
    VERIFICAR(contar(v, ACC_SONIDO) == 0);
    VERIFICAR(contar(v, ACC_AVISO) == 0);
    puerta_tick(&p, 600000);
    VERIFICAR(vaciar(p).empty());
  }

  // ---- A6: abrir remoto desarma y abre ----
  {
    puerta_t p = nueva(true, false);
    puerta_abrir_remoto(&p, 1000);
    auto v = vaciar(p);
    VERIFICAR(!p.armada);
    VERIFICAR(contar(v, ACC_PULSO) == 1);
    VERIFICAR(hay(v, ACC_EVENTO, "desarmado"));
    for (auto &a : v)
      if (a.tipo == ACC_EVENTO) VERIFICAR(a.de_la_app && a.uid_largo == 0);
    // y no cuenta para la doble pasada
    puerta_tarjeta(&p, 1500, A, 4, true);
    vaciar(p);
    VERIFICAR(!p.armada);
  }

  // ---- F4: pulsos repetidos: el limite por minuto frena y avisa ----
  {
    puerta_t p = nueva();
    p.pulso_ms = 10000;
    uint8_t u[4] = {0, 0, 0, 0};
    int pulsos = 0, errores = 0;
    for (int i = 0; i < 10; i++) {
      u[3] = (uint8_t)i;
      puerta_tarjeta(&p, 1000 + i * 4000, u, 4, true);
      auto v = vaciar(p);
      pulsos += contar(v, ACC_PULSO);
      errores += hay_sonido(v, SON_ERROR);
      puerta_tick(&p, 1000 + i * 4000 + 3900);
      vaciar(p);
      puerta_tarjeta(&p, 1000 + i * 4000 + 100, B, 4, false);   // un rechazo en el medio, para que no sea la doble pasada
      vaciar(p);
      p.doble_activa = false;
    }
    VERIFICAR(pulsos >= 1);
    VERIFICAR(errores >= 1);
  }

  // ---- F7/F9: sensor trabado en "abierta" por horas ----
  {
    puerta_t p = nueva();
    puerta_sensor(&p, 1000, true);
    VERIFICAR(!puerta_sensor_sospechoso(&p, 1000 + SENSOR_SOSPECHOSO_MS - 1));
    VERIFICAR(puerta_sensor_sospechoso(&p, 1000 + SENSOR_SOSPECHOSO_MS));
    puerta_sensor(&p, 2000 + SENSOR_SOSPECHOSO_MS, false);
    VERIFICAR(!puerta_sensor_sospechoso(&p, 3000 + SENSOR_SOSPECHOSO_MS));
  }

  // ---- Sin rollover: todo anda igual a los 100 dias de uptime ----
  {
    int64_t t0 = 100LL * 24 * 3600 * 1000;
    puerta_t p;
    puerta_iniciar(&p, t0, false, false, PULSO_DEFECTO_MS, AVISO_DEFECTO_S);
    puerta_tarjeta(&p, t0 + 1000, A, 4, true);
    VERIFICAR(contar(vaciar(p), ACC_PULSO) == 1);
    puerta_tick(&p, t0 + 1000 + PULSO_DEFECTO_MS);
    VERIFICAR(contar(vaciar(p), ACC_CORTAR_PULSO) == 1);
    puerta_sensor(&p, t0 + 20000, true);
    vaciar(p);
    puerta_tick(&p, t0 + 20000 + AVISO_DEFECTO_S * 1000);
    VERIFICAR(hay_aviso(vaciar(p), AVISO_PUERTA_ABIERTA));
  }

  // ---- Vigencia de la orden remota: sin hora, vencida o lejana, se rechaza ----
  {
    int64_t ahora = 1800000000LL;
    VERIFICAR(orden_vigente(true, ahora + 60, ahora) == ORDEN_OK);
    VERIFICAR(orden_vigente(true, ahora + ORDEN_VIGENCIA_MAX_S, ahora) == ORDEN_OK);
    VERIFICAR(orden_vigente(false, 0, ahora) == ORDEN_SIN_VENCIMIENTO);
    VERIFICAR(orden_vigente(true, ahora, ahora) == ORDEN_VENCIDA);
    VERIFICAR(orden_vigente(true, ahora - 1, ahora) == ORDEN_VENCIDA);
    VERIFICAR(orden_vigente(true, ahora + ORDEN_VIGENCIA_MAX_S + 1, ahora) == ORDEN_VENCIMIENTO_LEJANO);
    // Sin hora valida (la placa reinicio sin raiz): no puede saber si esta vencida
    VERIFICAR(orden_vigente(true, 100, 50) == ORDEN_SIN_HORA);
    VERIFICAR(orden_vigente(true, ahora + 60, 0) == ORDEN_SIN_HORA);
  }

  // ---- Review: un pulso rechazado por el limite no es un acceso ni abre la doble pasada ----
  {
    puerta_t p = nueva();
    int64_t t = 0;
    for (int i = 0; i < 4; i++) {            // cuatro pulsos de 5 s agotan los 20 s por minuto
      puerta_tarjeta(&p, t, A, 4, true);
      VERIFICAR(contar(vaciar(p), ACC_PULSO) == 1);
      puerta_tick(&p, t + 5000);
      vaciar(p);
      t += 6000;
    }
    puerta_tarjeta(&p, t, A, 4, true);       // rechazada por el limite
    auto v = vaciar(p);
    VERIFICAR(contar(v, ACC_PULSO) == 0);
    VERIFICAR(!hay(v, ACC_EVENTO, "acceso"));
    VERIFICAR(!hay_sonido(v, SON_OK));
    VERIFICAR(hay_sonido(v, SON_ERROR));
    VERIFICAR(hay(v, ACC_ALERTA, "limite_pulsos"));
    VERIFICAR(!(p.doble_activa && (t - p.doble_hasta_ms) < 0));   // la ventana no quedo abierta
    puerta_tarjeta(&p, t + 1000, A, 4, true);  // dentro de los 3 s: no es "segunda pasada", no arma
    v = vaciar(p);
    VERIFICAR(!p.armada);
    VERIFICAR(!hay(v, ACC_EVENTO, "armado"));
    VERIFICAR(!hay(v, ACC_EVENTO, "acceso"));
  }

  // ---- Review: un pedido rechazado con el pulso en curso no deja el rele colgado ----
  {
    puerta_t p = nueva();
    p.pulso_ms = 10000;
    puerta_tarjeta(&p, 0, A, 4, true);
    vaciar(p);
    puerta_tick(&p, 10000);
    VERIFICAR(contar(vaciar(p), ACC_CORTAR_PULSO) == 1);
    puerta_tarjeta(&p, 11000, A, 4, true);   // pulso en curso hasta 21000
    VERIFICAR(contar(vaciar(p), ACC_PULSO) == 1);
    puerta_tarjeta(&p, 20600, A, 4, true);   // 19600 ms usados: se rechaza
    auto v = vaciar(p);
    VERIFICAR(hay(v, ACC_ALERTA, "limite_pulsos"));
    VERIFICAR(p.cerradura.energizada);       // el pulso en curso sigue siendo el que era
    VERIFICAR(contar(v, ACC_CORTAR_PULSO) == 0);
    puerta_tick(&p, 20999);
    VERIFICAR(contar(vaciar(p), ACC_CORTAR_PULSO) == 0);
    puerta_tick(&p, 21000);                  // y se corta cuando vence, no se queda energizado
    VERIFICAR(contar(vaciar(p), ACC_CORTAR_PULSO) == 1);
    VERIFICAR(!p.cerradura.energizada);
  }

  // La cola de acciones no se desborda ni se corrompe
  {
    puerta_t p = nueva();
    for (int i = 0; i < 50; i++) {
      uint8_t u[4] = {0x09, 0x09, 0x09, (uint8_t)i};
      puerta_tarjeta(&p, i * 100000LL, u, 4, false);
    }
    auto v = vaciar(p);
    VERIFICAR((int)v.size() <= COLA_ACCIONES);
    VERIFICAR(p.perdidas > 0);
  }

  RESUMEN();
}
