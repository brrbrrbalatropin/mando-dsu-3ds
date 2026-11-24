# Mando DSU para New 3DS

Homebrew que convierte una New 3DS en un mando para Dolphin (u otro emulador con cliente DSU)
por WiFi. La 3DS hace de **servidor DSU** (protocolo cemuhook, v1001) en el puerto UDP **26760**;
el emulador se conecta a la IP que muestra la pantalla de arriba.

Solo se mandan los botones (no hay video), así que el retraso es el del WiFi.

## Compilar
Requiere devkitPro (devkitARM + libctru). Desde el shell MSYS2 de devkitPro:

```
make
```

Sale `mando-dsu.3dsx`; se copia a `sdmc:/3ds/` y se abre desde el Homebrew Launcher.

## Usar con Dolphin
1. Abrir la app en la 3DS (con WiFi). Arriba se ve la IP.
2. Dolphin → Controladores → *Fuentes de entrada alternativas* → activar **DSU Client** y agregar
   un servidor con esa IP y el puerto 26760.
3. Puerto 1 → Mando estándar → Configurar → dispositivo `DSUClient/0/...` y asignar botones.

Para salir de la app: **START + SELECT a la vez** (START solo llega al emulador).

## Mapeo
| 3DS | DSU |
|---|---|
| A, B, X, Y | A, B, X, Y |
| L, R | L1, R1 |
| ZL, ZR | L2, R2 |
| START, SELECT | Options, Share |
| Cruceta | D-Pad |
| Circle Pad | Stick izquierdo |
| C-Stick | Stick derecho |
| Pantalla táctil | Toque 1 |

## Probar sin emulador
`python prueba_dsu.py <IP de la 3DS> [segundos]` hace de cliente DSU: comprueba magia, longitud y
CRC de cada respuesta, la compara byte a byte con un armado de referencia y muestra botones y sticks.
`python prueba_dsu.py --autoprueba` prueba solo la referencia, sin consola.

## Archivos
- `source/dsu.c`, `dsu.h`: el protocolo (CRC32, armado y validación de paquetes), sin libctru.
- `source/main.c`: lectura del mando, red y pantalla de la 3DS.
- `prueba_dsu.py`: cliente de prueba en Python (solo biblioteca estándar).

Referencia del protocolo: https://github.com/v1993/cemuhook-protocol
