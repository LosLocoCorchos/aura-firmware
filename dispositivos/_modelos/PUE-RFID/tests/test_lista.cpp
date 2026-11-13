#include "../main/lista_tarjetas.h"
#include "../../../../comun/tests/aserciones.h"

static const uint8_t A[4] = {0xAB, 0xE8, 0xB0, 0x22};
static const uint8_t B[4] = {0x4B, 0x3E, 0xC2, 0x22};
static const uint8_t C[4] = {0xCA, 0xA4, 0xEA, 0x81};
static const uint8_t S7[7] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};

int main() {
  char hex[UID_HEX_MAX];
  uint8_t uid[UID_MAX_BYTES];
  uint8_t largo = 0;

  // Formato: mayusculas, sin separadores
  uid_a_hex(A, 4, hex);
  VERIFICAR(strcmp(hex, "ABE8B022") == 0);
  uid_a_hex(S7, 7, hex);
  VERIFICAR(strcmp(hex, "04A1B2C3D4E5F6") == 0);

  // Lectura: acepta mayusculas y minusculas, y devuelve lo mismo
  VERIFICAR(hex_a_uid("ABE8B022", uid, &largo) && largo == 4 && memcmp(uid, A, 4) == 0);
  VERIFICAR(hex_a_uid("abe8b022", uid, &largo) && largo == 4 && memcmp(uid, A, 4) == 0);
  VERIFICAR(hex_a_uid("04a1b2c3d4e5f6", uid, &largo) && largo == 7);

  // UID invalidos: largo 0, 1, 2, 3, 5, 6, 8, 9, 11 bytes; impar; caracteres que no son hexa; separadores
  VERIFICAR(!hex_a_uid("", uid, &largo));
  VERIFICAR(!hex_a_uid("AB", uid, &largo));
  VERIFICAR(!hex_a_uid("ABE8B0", uid, &largo));
  VERIFICAR(!hex_a_uid("ABE8B02233", uid, &largo));
  VERIFICAR(!hex_a_uid("ABE8B0223", uid, &largo));
  VERIFICAR(!hex_a_uid("ABE8B02G", uid, &largo));
  VERIFICAR(!hex_a_uid("AB E8B02", uid, &largo));
  VERIFICAR(!hex_a_uid("AB:E8:B0:22", uid, &largo));
  VERIFICAR(!hex_a_uid("0102030405060708091011", uid, &largo));
  VERIFICAR(!hex_a_uid(NULL, uid, &largo));
  VERIFICAR(hex_a_uid("01020304050607080910", uid, &largo) && largo == 10);

  // Una tarjeta vacia (largo 0) no se agrega ni autoriza nada
  lista_t l;
  lista_vaciar(&l);
  VERIFICAR(lista_agregar(&l, A, 0) == LISTA_UID_INVALIDO);
  VERIFICAR(lista_agregar(&l, A, 5) == LISTA_UID_INVALIDO);
  VERIFICAR(lista_agregar(&l, A, 11) == LISTA_UID_INVALIDO);
  VERIFICAR(l.n == 0);
  VERIFICAR(!lista_autorizada(&l, A, 0));

  // Agregar, repetir, buscar, quitar
  VERIFICAR(lista_agregar(&l, A, 4) == LISTA_OK);
  VERIFICAR(lista_agregar(&l, A, 4) == LISTA_YA_ESTABA);
  VERIFICAR(lista_agregar(&l, S7, 7) == LISTA_OK);
  VERIFICAR(l.n == 2);
  VERIFICAR(lista_autorizada(&l, A, 4));
  VERIFICAR(lista_autorizada(&l, S7, 7));
  VERIFICAR(!lista_autorizada(&l, B, 4));
  // Mismo prefijo pero otro largo no es la misma tarjeta
  VERIFICAR(!lista_autorizada(&l, S7, 4));
  VERIFICAR(lista_quitar(&l, B, 4) == LISTA_NO_ESTABA);
  VERIFICAR(lista_quitar(&l, A, 0) == LISTA_UID_INVALIDO);
  VERIFICAR(lista_quitar(&l, A, 4) == LISTA_OK);
  VERIFICAR(!lista_autorizada(&l, A, 4));
  VERIFICAR(l.n == 1 && lista_autorizada(&l, S7, 7));

  // Lista llena
  lista_vaciar(&l);
  for (int i = 0; i < LISTA_MAX; i++) {
    uint8_t u[4] = {0x10, 0x00, (uint8_t)(i >> 8), (uint8_t)i};
    VERIFICAR(lista_agregar(&l, u, 4) == LISTA_OK);
  }
  VERIFICAR(lista_agregar(&l, A, 4) == LISTA_LLENA);
  VERIFICAR(l.n == LISTA_MAX);

  // Hash: valores que da zlib.crc32 en Python (AURA calcula lo mismo)
  char h[HASH_HEX_BYTES];
  lista_vaciar(&l);
  lista_hash(&l, h);
  VERIFICAR(strcmp(h, "00000000") == 0);
  lista_agregar(&l, A, 4);
  lista_hash(&l, h);
  VERIFICAR(strcmp(h, "ad946105") == 0);
  lista_agregar(&l, B, 4);
  lista_hash(&l, h);
  VERIFICAR(strcmp(h, "7a2dc6a5") == 0);  // "4B3EC222,ABE8B022"

  // No depende del orden de la lista ni de como se llego a ella
  lista_t m;
  lista_vaciar(&m);
  lista_agregar(&m, B, 4);
  lista_agregar(&m, A, 4);
  char h2[HASH_HEX_BYTES];
  lista_hash(&m, h2);
  VERIFICAR(strcmp(h, h2) == 0);
  lista_agregar(&m, C, 4);
  lista_quitar(&m, B, 4);       // reordena por dentro
  lista_agregar(&m, B, 4);
  lista_quitar(&m, C, 4);
  lista_hash(&m, h2);
  VERIFICAR(strcmp(h, h2) == 0);

  // Mezcla de largos: "04A1B2C3D4E5F6,ABE8B022,CAA4EA81"
  lista_vaciar(&l);
  lista_agregar(&l, C, 4);
  lista_agregar(&l, A, 4);
  lista_agregar(&l, S7, 7);
  lista_hash(&l, h);
  VERIFICAR(strcmp(h, "6885955e") == 0);

  // Cambiar la lista cambia el hash
  lista_quitar(&l, C, 4);
  lista_hash(&l, h2);
  VERIFICAR(strcmp(h, h2) != 0);

  RESUMEN();
}
