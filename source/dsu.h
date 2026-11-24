// Protocolo DSU (cemuhook), versión 1001: armado y validación de paquetes.
// Sin dependencias de libctru a propósito: así se puede compilar y probar en el PC
// con cualquier gcc y comparar byte a byte con prueba_dsu.py.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define DSU_PUERTO        26760
#define DSU_VERSION       1001

#define DSU_MSG_VERSION   0x100000u
#define DSU_MSG_INFO      0x100001u
#define DSU_MSG_DATOS     0x100002u

#define DSU_TAM_CABECERA  16
#define DSU_TAM_VERSION   22
#define DSU_TAM_INFO      32
#define DSU_TAM_DATOS     100

// Botones en términos DSU (no de la 3DS): main.c hace el mapeo.
enum {
	DSU_BTN_IZQ     = 1u << 0,
	DSU_BTN_ABAJO   = 1u << 1,
	DSU_BTN_DER     = 1u << 2,
	DSU_BTN_ARRIBA  = 1u << 3,
	DSU_BTN_OPTIONS = 1u << 4,
	DSU_BTN_SHARE   = 1u << 5,
	DSU_BTN_A       = 1u << 6,
	DSU_BTN_B       = 1u << 7,
	DSU_BTN_X       = 1u << 8,
	DSU_BTN_Y       = 1u << 9,
	DSU_BTN_L1      = 1u << 10,
	DSU_BTN_R1      = 1u << 11,
	DSU_BTN_L2      = 1u << 12,
	DSU_BTN_R2      = 1u << 13,
};

typedef struct {
	uint32_t botones;        // máscara DSU_BTN_*
	uint8_t  ejeLX, ejeLY;   // ya convertidos a 0..255 (128 centro, 255 = derecha/arriba)
	uint8_t  ejeRX, ejeRY;
	uint8_t  tocando;        // 0/1
	uint8_t  idToque;        // cambia en cada toque nuevo
	uint16_t toqueX, toqueY; // píxeles de la pantalla de abajo
	uint8_t  bateria;        // código DSU (0x01..0x05, 0xEE cargando)
	uint64_t tiempoUs;       // marca de tiempo del movimiento
} DsuEstado;

// CRC32 estándar de zlib (polinomio reflejado 0xEDB88320).
uint32_t dsuCrc32(const uint8_t* datos, size_t tam);

// Convierte un valor de stick de la 3DS (~±156) a byte DSU, con zona muerta.
uint8_t dsuEjeAByte(int v);

// Cada función escribe el paquete completo (con CRC) en buf y devuelve su tamaño.
size_t dsuArmarVersion(uint8_t* buf, uint32_t idServidor);
size_t dsuArmarInfo(uint8_t* buf, uint32_t idServidor, uint8_t slot, uint8_t bateria);
size_t dsuArmarDatos(uint8_t* buf, uint32_t idServidor, uint32_t numPaquete, const DsuEstado* e);

// Valida un paquete entrante del cliente (magia DSUC, tamaños). Devuelve el tamaño del
// payload (desde el byte 20) y escribe el tipo, o -1 si hay que descartarlo.
int dsuValidarEntrante(const uint8_t* buf, size_t tam, uint32_t* tipo);

// MAC fija que anunciamos para el slot 0.
extern const uint8_t dsuMac[6];
