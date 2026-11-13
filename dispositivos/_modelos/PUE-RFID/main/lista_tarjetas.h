#pragma once
/*
 * Lista de llaveros autorizados de una puerta (C puro, sin ESP-IDF: se prueba en la compu).
 *
 * La administra AURA con tarjeta_agregar / tarjeta_quitar. No hay tarjeta maestra: una lista
 * que se puede cambiar en la puerta deja de coincidir con la de AURA (ADR-009, decision 3).
 *
 * El UID se escribe SIEMPRE igual, en la puerta y en el lector de banco: hexadecimal en
 * MAYUSCULAS, sin separadores, de 4, 7 o 10 bytes ("ABE8B022"). Este header lo usan los dos.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UID_MAX_BYTES 10
#define UID_HEX_MAX (2 * UID_MAX_BYTES + 1)
#define LISTA_MAX 32
#define HASH_HEX_BYTES 9               /* 8 digitos hexa y el cero final */

typedef struct {
    uint8_t largo;
    uint8_t uid[UID_MAX_BYTES];
} tarjeta_t;

typedef struct {
    uint8_t n;
    tarjeta_t t[LISTA_MAX];
} lista_t;

typedef enum {
    LISTA_OK = 0,                      /* agregada o quitada */
    LISTA_YA_ESTABA,
    LISTA_NO_ESTABA,
    LISTA_LLENA,
    LISTA_UID_INVALIDO
} lista_resultado_t;

/* Un UID de MIFARE mide 4, 7 o 10 bytes. Cualquier otro largo (0 incluido) no es una tarjeta. */
static inline bool uid_largo_valido(size_t largo)
{
    return largo == 4 || largo == 7 || largo == 10;
}

/* Escribe el UID como hexa en mayusculas. out necesita UID_HEX_MAX bytes. */
static inline void uid_a_hex(const uint8_t *uid, size_t largo, char *out)
{
    static const char digitos[] = "0123456789ABCDEF";
    for (size_t i = 0; i < largo; i++) {
        out[2 * i] = digitos[uid[i] >> 4];
        out[2 * i + 1] = digitos[uid[i] & 0x0F];
    }
    out[2 * largo] = '\0';
}

static inline int hex_digito(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Lee un UID en hexa (acepta mayusculas y minusculas, nada de separadores). false si no es un
   UID valido: caracteres que no son hexa, cantidad impar o largo que no es 4, 7 ni 10 bytes. */
static inline bool hex_a_uid(const char *hex, uint8_t *uid, uint8_t *largo)
{
    if (hex == NULL) return false;
    size_t n = strlen(hex);
    if (n % 2 != 0 || !uid_largo_valido(n / 2)) return false;
    for (size_t i = 0; i < n / 2; i++) {
        int alto = hex_digito(hex[2 * i]);
        int bajo = hex_digito(hex[2 * i + 1]);
        if (alto < 0 || bajo < 0) return false;
        uid[i] = (uint8_t)((alto << 4) | bajo);
    }
    *largo = (uint8_t)(n / 2);
    return true;
}

static inline void lista_vaciar(lista_t *l)
{
    memset(l, 0, sizeof(*l));
}

static inline int lista_buscar(const lista_t *l, const uint8_t *uid, size_t largo)
{
    for (int i = 0; i < l->n; i++) {
        if (l->t[i].largo == largo && memcmp(l->t[i].uid, uid, largo) == 0) return i;
    }
    return -1;
}

static inline bool lista_autorizada(const lista_t *l, const uint8_t *uid, size_t largo)
{
    return uid_largo_valido(largo) && lista_buscar(l, uid, largo) >= 0;
}

static inline lista_resultado_t lista_agregar(lista_t *l, const uint8_t *uid, size_t largo)
{
    if (!uid_largo_valido(largo)) return LISTA_UID_INVALIDO;
    if (lista_buscar(l, uid, largo) >= 0) return LISTA_YA_ESTABA;
    if (l->n >= LISTA_MAX) return LISTA_LLENA;
    l->t[l->n].largo = (uint8_t)largo;
    memset(l->t[l->n].uid, 0, UID_MAX_BYTES);
    memcpy(l->t[l->n].uid, uid, largo);
    l->n++;
    return LISTA_OK;
}

static inline lista_resultado_t lista_quitar(lista_t *l, const uint8_t *uid, size_t largo)
{
    if (!uid_largo_valido(largo)) return LISTA_UID_INVALIDO;
    int i = lista_buscar(l, uid, largo);
    if (i < 0) return LISTA_NO_ESTABA;
    l->t[i] = l->t[l->n - 1];          /* el orden no importa: el hash no depende de el */
    l->n--;
    memset(&l->t[l->n], 0, sizeof(tarjeta_t));
    return LISTA_OK;
}

/* ---------- Hash de la lista ----------
 * Es lo que la puerta informa en su status para que AURA vea si la lista coincide con la suya.
 * Tiene que dar lo mismo en AURA, asi que NO depende del orden de la lista:
 *
 *   1. cada UID en hexa mayusculas ("ABE8B022"),
 *   2. ordenados de menor a mayor como texto,
 *   3. unidos con "," (sin coma al final),
 *   4. CRC-32 IEEE (el de zlib.crc32 en Python) de ese texto, en 8 digitos hexa MINUSCULAS.
 *
 * La lista vacia da "00000000". Detecta cambios y desfasajes, no es una firma: los UID no son
 * secretos. (Formato a confirmar con el contrato v4.5.) */
static inline uint32_t crc32_acumular(uint32_t crc, const uint8_t *datos, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc ^= datos[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return crc;
}

static inline void lista_hash(const lista_t *l, char out[HASH_HEX_BYTES])
{
    char hex[LISTA_MAX][UID_HEX_MAX];
    int orden[LISTA_MAX];
    for (int i = 0; i < l->n; i++) {
        uid_a_hex(l->t[i].uid, l->t[i].largo, hex[i]);
        orden[i] = i;
    }
    for (int i = 1; i < l->n; i++) {   /* insercion: son 32 como mucho */
        int x = orden[i];
        int j = i - 1;
        while (j >= 0 && strcmp(hex[orden[j]], hex[x]) > 0) {
            orden[j + 1] = orden[j];
            j--;
        }
        orden[j + 1] = x;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (int i = 0; i < l->n; i++) {
        if (i > 0) crc = crc32_acumular(crc, (const uint8_t *)",", 1);
        crc = crc32_acumular(crc, (const uint8_t *)hex[orden[i]], strlen(hex[orden[i]]));
    }
    crc ^= 0xFFFFFFFFu;
    static const char digitos[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i] = digitos[(crc >> (28 - 4 * i)) & 0x0F];
    }
    out[8] = '\0';
}
