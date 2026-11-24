// Mando DSU: la New 3DS como mando de Dolphin por WiFi.
//
// Dolphin habla el protocolo DSU (el de cemuhook) como CLIENTE: manda peticiones UDP a la
// IP de un servidor y este le responde con el estado del mando ~60 veces por segundo.
// Aquí la 3DS es ese servidor (puerto 26760). El armado de paquetes vive en dsu.c, sin
// libctru, para poder probarlo en el PC; este archivo solo lee la consola y la red.
//
// En Dolphin: Controles > Alternate Input Sources > DSU Client, añadir la IP que se ve
// arriba y el puerto 26760; luego elegir el dispositivo "DSUClient/0/..." en el mando.

#include <3ds.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dsu.h"

#define SOC_TAM_BUFFER   0x100000
#define TIEMPO_CLIENTE   5000   // ms: sin peticiones durante más tiempo, el cliente se da por ido
#define CADA_TEXTO       15     // frames entre refrescos de la pantalla (~4 por segundo)
#define CADA_BATERIA     120    // frames entre lecturas de batería (~2 s)

static u32* bufferSoc = NULL;
static int  sock = -1;

// El único cliente que atendemos: el último que pidió datos.
static struct sockaddr_in cliente;
static int  hayCliente = 0;
static u64  ultimaPeticion = 0;  // osGetTime() de la última petición de datos

static u32 idServidor;
static u32 numPaquete = 0;
static u32 enviados = 0;
static u32 erroresEnvio = 0;
static u8  bateria = 0;
static int ptmuListo = 0;

static int salirPulsado(void)
{
	// Hace falta START y SELECT A LA VEZ: START solo es un botón más para Dolphin.
	u32 h = hidKeysHeld();
	return (h & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT);
}

// Para cuando algo falla al arrancar: deja leer el error y espera la salida.
static void esperarSalida(void)
{
	printf("\n\nSTART+SELECT juntos: salir\n");
	while (aptMainLoop()) {
		gspWaitForVBlank();
		hidScanInput();
		if (salirPulsado())
			break;
	}
}

static void leerBateria(void)
{
	if (!ptmuListo) {
		bateria = 0;
		return;
	}
	u8 nivel = 0, cargando = 0;
	if (R_FAILED(PTMU_GetBatteryLevel(&nivel)))
		nivel = 0;
	if (R_FAILED(PTMU_GetBatteryChargeState(&cargando)))
		cargando = 0;
	if (cargando)
		bateria = 0xEE;
	else
		bateria = nivel <= 1 ? 0x01 : (nivel >= 5 ? 0x05 : nivel);
}

static void enviar(const struct sockaddr_in* destino, const u8* buf, size_t tam)
{
	if (sendto(sock, buf, tam, 0, (const struct sockaddr*)destino, sizeof(*destino)) < 0)
		erroresEnvio++;
	else
		enviados++;
}

// Responde a un paquete del cliente. Ya validado: payload apunta al byte 20.
static void atenderPaquete(const struct sockaddr_in* origen, u32 tipo, const u8* payload, int tamPayload)
{
	u8 resp[DSU_TAM_DATOS];

	switch (tipo) {
	case DSU_MSG_VERSION:
		enviar(origen, resp, dsuArmarVersion(resp, idServidor));
		break;

	case DSU_MSG_INFO: {
		if (tamPayload < 4)
			return;
		s32 n = (s32)(payload[0] | (payload[1] << 8) | (payload[2] << 16) | ((u32)payload[3] << 24));
		if (n < 0)
			return;
		if (n > 4)
			n = 4;
		if (n > tamPayload - 4)
			n = tamPayload - 4;
		// Un paquete por slot pedido, como espera Dolphin.
		for (int i = 0; i < n; i++)
			enviar(origen, resp, dsuArmarInfo(resp, idServidor, payload[4 + i], bateria));
		break;
	}

	case DSU_MSG_DATOS: {
		if (tamPayload < 8)
			return;
		u8 flags = payload[0];
		u8 slot = payload[1];
		int porMac = memcmp(payload + 2, dsuMac, 6) == 0;
		// flags 0 = "todos los mandos"; bit0 = por slot; bit1 = por MAC. Solo existe el slot 0.
		int pideOtro = (flags & 1) && slot != 0 && !((flags & 2) && porMac);
		if (pideOtro)
			return;
		cliente = *origen;
		hayCliente = 1;
		ultimaPeticion = osGetTime();
		break;
	}

	default:
		break;
	}
}

static void leerRed(void)
{
	u8 buf[256];
	for (;;) {
		struct sockaddr_in origen;
		socklen_t tamOrigen = sizeof(origen);
		ssize_t r = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&origen, &tamOrigen);
		if (r < 0)
			break;  // EWOULDBLOCK: no queda nada pendiente
		u32 tipo;
		int tamPayload = dsuValidarEntrante(buf, (size_t)r, &tipo);
		if (tamPayload >= 0)
			atenderPaquete(&origen, tipo, buf + 20, tamPayload);
	}
}

// Lee la 3DS y lo traduce a términos DSU.
static void leerMando(DsuEstado* e, circlePosition* cp, circlePosition* cs, touchPosition* tp)
{
	static int tocabaAntes = 0;
	static u8 idToque = 0;

	u32 h = hidKeysHeld();
	hidCircleRead(cp);
	hidCstickRead(cs);  // = irrstCstickRead; hidInit ya inicia IRRST en la New 3DS
	hidTouchRead(tp);

	memset(e, 0, sizeof(*e));
	static const struct { u32 tecla; u32 dsu; } mapa[] = {
		{ KEY_A, DSU_BTN_A }, { KEY_B, DSU_BTN_B }, { KEY_X, DSU_BTN_X }, { KEY_Y, DSU_BTN_Y },
		{ KEY_L, DSU_BTN_L1 }, { KEY_R, DSU_BTN_R1 }, { KEY_ZL, DSU_BTN_L2 }, { KEY_ZR, DSU_BTN_R2 },
		{ KEY_START, DSU_BTN_OPTIONS }, { KEY_SELECT, DSU_BTN_SHARE },
		// Solo la cruceta: KEY_UP/KEY_DOWN... incluyen también el Circle Pad.
		{ KEY_DLEFT, DSU_BTN_IZQ }, { KEY_DDOWN, DSU_BTN_ABAJO },
		{ KEY_DRIGHT, DSU_BTN_DER }, { KEY_DUP, DSU_BTN_ARRIBA },
	};
	for (size_t i = 0; i < sizeof(mapa) / sizeof(mapa[0]); i++)
		if (h & mapa[i].tecla)
			e->botones |= mapa[i].dsu;

	// En la 3DS dy > 0 es arriba, igual que en DSU: no se invierte.
	e->ejeLX = dsuEjeAByte(cp->dx);
	e->ejeLY = dsuEjeAByte(cp->dy);
	e->ejeRX = dsuEjeAByte(cs->dx);
	e->ejeRY = dsuEjeAByte(cs->dy);

	int toca = (h & KEY_TOUCH) != 0;
	if (toca && !tocabaAntes)
		idToque++;  // un id nuevo por cada toque, para que el cliente distinga arrastres
	tocabaAntes = toca;
	e->tocando = toca;
	e->idToque = idToque;
	e->toqueX = tp->px;
	e->toqueY = tp->py;

	e->bateria = bateria;
	e->tiempoUs = (u64)(svcGetSystemTick() / CPU_TICKS_PER_USEC);
}

static void dibujarTexto(const DsuEstado* e, const circlePosition* cp,
                         const circlePosition* cs, const touchPosition* tp, int conectado)
{
	// Se reescriben solo las líneas que cambian; \x1b[K borra el resto de cada línea.
	if (conectado)
		printf("\x1b[6;1HEstado: conectado a %s:%u\x1b[K", inet_ntoa(cliente.sin_addr),
		       ntohs(cliente.sin_port));
	else
		printf("\x1b[6;1HEstado: esperando a Dolphin\x1b[K");
	printf("\x1b[7;1HPaquetes enviados: %lu  (errores: %lu)\x1b[K",
	       (unsigned long)enviados, (unsigned long)erroresEnvio);
	printf("\x1b[8;1HBateria DSU: 0x%02X\x1b[K", bateria);

	static const struct { u32 b; const char* nombre; } nombres[] = {
		{ DSU_BTN_A, "A" }, { DSU_BTN_B, "B" }, { DSU_BTN_X, "X" }, { DSU_BTN_Y, "Y" },
		{ DSU_BTN_L1, "L" }, { DSU_BTN_R1, "R" }, { DSU_BTN_L2, "ZL" }, { DSU_BTN_R2, "ZR" },
		{ DSU_BTN_OPTIONS, "START" }, { DSU_BTN_SHARE, "SELECT" },
		{ DSU_BTN_IZQ, "<" }, { DSU_BTN_ABAJO, "v" }, { DSU_BTN_DER, ">" }, { DSU_BTN_ARRIBA, "^" },
	};
	printf("\x1b[10;1HBotones:");
	for (size_t i = 0; i < sizeof(nombres) / sizeof(nombres[0]); i++)
		if (e->botones & nombres[i].b)
			printf(" %s", nombres[i].nombre);
	printf("\x1b[K");
	printf("\x1b[11;1HCircle Pad: %4d %4d -> %3u %3u\x1b[K", cp->dx, cp->dy, e->ejeLX, e->ejeLY);
	printf("\x1b[12;1HC-Stick:    %4d %4d -> %3u %3u\x1b[K", cs->dx, cs->dy, e->ejeRX, e->ejeRY);
	if (e->tocando)
		printf("\x1b[13;1HToque: %3u %3u (id %u)\x1b[K", tp->px, tp->py, e->idToque);
	else
		printf("\x1b[13;1HToque: -\x1b[K");
}

int main(void)
{
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	printf("\x1b[1;1HMando DSU - la 3DS como mando de Dolphin\n");

	ptmuListo = R_SUCCEEDED(ptmuInit());

	bufferSoc = (u32*)memalign(0x1000, SOC_TAM_BUFFER);
	if (!bufferSoc) {
		printf("\nSin memoria para el bufer de red.");
		esperarSalida();
		goto fin;
	}
	Result res = socInit(bufferSoc, SOC_TAM_BUFFER);
	if (R_FAILED(res)) {
		printf("\nsocInit fallo: 0x%08lX", (unsigned long)res);
		free(bufferSoc);
		bufferSoc = NULL;
		esperarSalida();
		goto fin;
	}

	struct in_addr ipPropia;
	ipPropia.s_addr = (in_addr_t)gethostid();
	if (ipPropia.s_addr == 0) {
		printf("\nSin WiFi: conecta la 3DS a la red y vuelve a abrir.");
		esperarSalida();
		goto fin;
	}
	char ip[20];
	snprintf(ip, sizeof(ip), "%s", inet_ntoa(ipPropia));

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		printf("\nsocket() fallo: errno %d", errno);
		esperarSalida();
		goto fin;
	}
	struct sockaddr_in dir;
	memset(&dir, 0, sizeof(dir));
	dir.sin_family = AF_INET;
	dir.sin_port = htons(DSU_PUERTO);
	dir.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr*)&dir, sizeof(dir)) < 0) {
		printf("\nbind() fallo: errno %d", errno);
		esperarSalida();
		goto fin;
	}
	// No bloqueante: el bucle no puede quedarse esperando a la red, debe ir a 60 Hz.
	fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

	// Id aleatorio por arranque: así el cliente nota si el servidor se reinició.
	idServidor = (u32)(svcGetSystemTick() ^ (osGetTime() << 16));

	printf("\x1b[3;1HIP: %s   Puerto: %d", ip, DSU_PUERTO);
	printf("\x1b[4;1HSTART+SELECT juntos: salir");
	leerBateria();

	u32 frame = 0;
	DsuEstado estado;
	circlePosition cp, cs;
	touchPosition tp;
	u8 paquete[DSU_TAM_DATOS];

	while (aptMainLoop()) {
		gspWaitForVBlank();
		hidScanInput();
		if (salirPulsado())
			break;

		if (frame % CADA_BATERIA == 0)
			leerBateria();

		leerRed();
		leerMando(&estado, &cp, &cs, &tp);

		int conectado = hayCliente && (osGetTime() - ultimaPeticion) < TIEMPO_CLIENTE;
		if (conectado)
			enviar(&cliente, paquete, dsuArmarDatos(paquete, idServidor, numPaquete++, &estado));

		if (frame % CADA_TEXTO == 0)
			dibujarTexto(&estado, &cp, &cs, &tp, conectado);
		frame++;
	}

fin:
	if (sock >= 0)
		close(sock);
	if (bufferSoc) {
		socExit();
		free(bufferSoc);
	}
	if (ptmuListo)
		ptmuExit();
	gfxExit();
	return 0;
}
