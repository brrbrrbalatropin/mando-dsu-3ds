// Protocolo DSU (cemuhook) v1001. Todo entero va en little-endian; se escribe byte a
// byte para no depender de la endianness ni del alineamiento de quien compile esto.
#include "dsu.h"

#include <string.h>

const uint8_t dsuMac[6] = { 0x00, 0x3D, 0x53, 0x00, 0x00, 0x01 };

static uint32_t tablaCrc[256];
static int tablaLista = 0;

static void prepararTablaCrc(void)
{
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++)
			c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		tablaCrc[i] = c;
	}
	tablaLista = 1;
}

uint32_t dsuCrc32(const uint8_t* datos, size_t tam)
{
	if (!tablaLista)
		prepararTablaCrc();
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < tam; i++)
		c = tablaCrc[(c ^ datos[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

static void escribirU16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }

static void escribirU32(uint8_t* p, uint32_t v)
{
	p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
}

static uint16_t leerU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t leerU32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t dsuEjeAByte(int v)
{
	// La zona muerta evita que el reposo del stick (unas pocas unidades) mueva la cámara.
	if (v > -10 && v < 10)
		v = 0;
	int r = 128 + v * 127 / 150;
	if (r < 0)   r = 0;
	if (r > 255) r = 255;
	return (uint8_t)r;
}

// Cabecera + tipo. El CRC se calcula al final, con su campo en cero.
static void escribirCabecera(uint8_t* buf, size_t tam, uint32_t idServidor, uint32_t tipo)
{
	memset(buf, 0, tam);
	memcpy(buf, "DSUS", 4);
	escribirU16(buf + 4, DSU_VERSION);
	escribirU16(buf + 6, (uint16_t)(tam - DSU_TAM_CABECERA));
	escribirU32(buf + 12, idServidor);
	escribirU32(buf + 16, tipo);
}

static void cerrarPaquete(uint8_t* buf, size_t tam)
{
	escribirU32(buf + 8, 0);
	escribirU32(buf + 8, dsuCrc32(buf, tam));
}

// Los 11 primeros bytes de la info de un slot; los repite el paquete de datos.
static void escribirInicioSlot(uint8_t* p, uint8_t slot, uint8_t bateria)
{
	p[0] = slot;
	if (slot == 0) {
		p[1] = 2;  // conectado
		p[2] = 2;  // modelo: giroscopio completo (lo que anuncia un DS4)
		p[3] = 2;  // conexión: bluetooth
		memcpy(p + 4, dsuMac, 6);
		p[10] = bateria;
	}
	// Los demás slots quedan en cero: desconectados, sin MAC ni batería.
}

size_t dsuArmarVersion(uint8_t* buf, uint32_t idServidor)
{
	escribirCabecera(buf, DSU_TAM_VERSION, idServidor, DSU_MSG_VERSION);
	escribirU16(buf + 20, DSU_VERSION);
	cerrarPaquete(buf, DSU_TAM_VERSION);
	return DSU_TAM_VERSION;
}

size_t dsuArmarInfo(uint8_t* buf, uint32_t idServidor, uint8_t slot, uint8_t bateria)
{
	escribirCabecera(buf, DSU_TAM_INFO, idServidor, DSU_MSG_INFO);
	escribirInicioSlot(buf + 20, slot, bateria);
	// byte 31 (payload 11) = 0
	cerrarPaquete(buf, DSU_TAM_INFO);
	return DSU_TAM_INFO;
}

static uint8_t pulsado(uint32_t botones, uint32_t b) { return (botones & b) ? 1 : 0; }
static uint8_t analogico(uint32_t botones, uint32_t b) { return (botones & b) ? 255 : 0; }

size_t dsuArmarDatos(uint8_t* buf, uint32_t idServidor, uint32_t numPaquete, const DsuEstado* e)
{
	escribirCabecera(buf, DSU_TAM_DATOS, idServidor, DSU_MSG_DATOS);
	uint8_t* p = buf + 20;
	const uint32_t b = e->botones;

	escribirInicioSlot(p, 0, e->bateria);
	p[11] = 1;  // conectado
	escribirU32(p + 12, numPaquete);

	p[16] = (uint8_t)((pulsado(b, DSU_BTN_IZQ) << 7) | (pulsado(b, DSU_BTN_ABAJO) << 6) |
	                  (pulsado(b, DSU_BTN_DER) << 5) | (pulsado(b, DSU_BTN_ARRIBA) << 4) |
	                  (pulsado(b, DSU_BTN_OPTIONS) << 3) | (pulsado(b, DSU_BTN_SHARE) << 0));
	p[17] = (uint8_t)((pulsado(b, DSU_BTN_Y) << 7) | (pulsado(b, DSU_BTN_B) << 6) |
	                  (pulsado(b, DSU_BTN_A) << 5) | (pulsado(b, DSU_BTN_X) << 4) |
	                  (pulsado(b, DSU_BTN_R1) << 3) | (pulsado(b, DSU_BTN_L1) << 2) |
	                  (pulsado(b, DSU_BTN_R2) << 1) | (pulsado(b, DSU_BTN_L2) << 0));
	p[18] = 0;  // HOME: no se manda (la 3DS lo usa para su menú)
	p[19] = e->tocando ? 1 : 0;
	p[20] = e->ejeLX;
	p[21] = e->ejeLY;
	p[22] = e->ejeRX;
	p[23] = e->ejeRY;

	p[24] = analogico(b, DSU_BTN_IZQ);
	p[25] = analogico(b, DSU_BTN_ABAJO);
	p[26] = analogico(b, DSU_BTN_DER);
	p[27] = analogico(b, DSU_BTN_ARRIBA);
	p[28] = analogico(b, DSU_BTN_Y);
	p[29] = analogico(b, DSU_BTN_B);
	p[30] = analogico(b, DSU_BTN_A);
	p[31] = analogico(b, DSU_BTN_X);
	p[32] = analogico(b, DSU_BTN_R1);
	p[33] = analogico(b, DSU_BTN_L1);
	p[34] = analogico(b, DSU_BTN_R2);
	p[35] = analogico(b, DSU_BTN_L2);

	if (e->tocando) {
		p[36] = 1;
		p[37] = e->idToque;
		escribirU16(p + 38, e->toqueX);
		escribirU16(p + 40, e->toqueY);
	}
	// 42..47: segundo toque en cero.

	for (int i = 0; i < 8; i++)
		p[48 + i] = (uint8_t)(e->tiempoUs >> (8 * i));

	// Acelerómetro Y = -1 g (mando en reposo boca arriba); giroscopio en cero.
	// El resto de los seis floats ya está en cero por el memset.
	const float gravedadY = -1.0f;
	uint32_t bits;
	memcpy(&bits, &gravedadY, 4);
	escribirU32(p + 60, bits);

	cerrarPaquete(buf, DSU_TAM_DATOS);
	return DSU_TAM_DATOS;
}

int dsuValidarEntrante(const uint8_t* buf, size_t tam, uint32_t* tipo)
{
	if (tam < 20 || memcmp(buf, "DSUC", 4) != 0)
		return -1;
	size_t longitud = leerU16(buf + 6);
	// La longitud incluye el tipo (4 bytes); si dice más de lo que llegó, está cortado.
	if (longitud < 4 || DSU_TAM_CABECERA + longitud > tam)
		return -1;
	*tipo = leerU32(buf + 16);
	return (int)(longitud - 4);
}
