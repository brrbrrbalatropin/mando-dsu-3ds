"""Cliente DSU de prueba para el homebrew "Mando DSU" (05-mando-dsu).

Hace de Dolphin: pide versión, info de los slots 0..3 y datos al servidor de la 3DS,
verifica cada respuesta (magia, longitud, CRC32) y durante 10 s imprime los botones y
sticks que llegan. Re-envía la petición de datos cada segundo (el servidor deja de
mandar a los 5 s sin peticiones, igual que con Dolphin).

Además lleva una implementación de referencia del paquete de datos armada byte a byte
según la tabla del protocolo: cada paquete de datos recibido se decodifica, se vuelve a
armar con la referencia y se compara byte a byte con lo que mandó la 3DS. Si el C pone
un campo en un offset equivocado, aparece como diferencia.

Uso:
    python prueba_dsu.py <IP de la 3DS> [segundos]
    python prueba_dsu.py --autoprueba        (sin consola: prueba la referencia sola)
"""

import random
import socket
import struct
import sys
import time
import zlib

PUERTO = 26760
VERSION = 1001
MSG_VERSION = 0x100000
MSG_INFO = 0x100001
MSG_DATOS = 0x100002
MAC = bytes([0x00, 0x3D, 0x53, 0x00, 0x00, 0x01])

# Orden de los bits en los dos bytes de botones (del bit 7 al bit 0).
BITS_16 = ["Izq", "Abajo", "Der", "Arriba", "Options", "R3", "L3", "Share"]
BITS_17 = ["Y", "B", "A", "X", "R1", "L1", "R2", "L2"]
# Botones con valor analógico (payload 24..35), en ese orden.
ANALOGICOS = ["Izq", "Abajo", "Der", "Arriba", "Y", "B", "A", "X", "R1", "L1", "R2", "L2"]


def crc32(datos: bytes) -> int:
    return zlib.crc32(datos) & 0xFFFFFFFF


def cerrar_paquete(magia: bytes, id_emisor: int, tipo: int, payload: bytes) -> bytes:
    cuerpo = struct.pack("<I", tipo) + payload
    cab = magia + struct.pack("<HHII", VERSION, len(cuerpo), 0, id_emisor)
    paquete = bytearray(cab + cuerpo)
    struct.pack_into("<I", paquete, 8, crc32(bytes(paquete)))
    return bytes(paquete)


def verificar(paquete: bytes):
    """Devuelve (tipo, payload, id_servidor) o lanza ValueError con el motivo."""
    if len(paquete) < 20:
        raise ValueError(f"paquete corto ({len(paquete)} bytes)")
    if paquete[:4] != b"DSUS":
        raise ValueError(f"magia {paquete[:4]!r}, se esperaba b'DSUS'")
    version, longitud, crc, id_servidor = struct.unpack_from("<HHII", paquete, 4)
    if version != VERSION:
        raise ValueError(f"versión {version}")
    if 16 + longitud != len(paquete):
        raise ValueError(f"longitud {longitud} + 16 != {len(paquete)} bytes recibidos")
    sin_crc = bytearray(paquete)
    struct.pack_into("<I", sin_crc, 8, 0)
    if crc32(bytes(sin_crc)) != crc:
        raise ValueError(f"CRC {crc:08X} != calculado {crc32(bytes(sin_crc)):08X}")
    (tipo,) = struct.unpack_from("<I", paquete, 16)
    return tipo, paquete[20:], id_servidor


# ---------------------------------------------------------------------------
# Referencia del paquete de datos (100 bytes), según la tabla de la tarea.
# ---------------------------------------------------------------------------
def armar_datos_referencia(id_servidor, num_paquete, botones, lx, ly, rx, ry,
                           toque=None, bateria=0x05, tiempo_us=0):
    """botones: conjunto de nombres de BITS_16/BITS_17. toque: (id, x, y) o None."""
    p = bytearray(80)
    p[0] = 0                     # slot
    p[1] = 2                     # conectado
    p[2] = 2                     # modelo
    p[3] = 2                     # conexión bluetooth
    p[4:10] = MAC
    p[10] = bateria
    p[11] = 1                    # conectado
    struct.pack_into("<I", p, 12, num_paquete)
    p[16] = sum(1 << (7 - i) for i, n in enumerate(BITS_16) if n in botones)
    p[17] = sum(1 << (7 - i) for i, n in enumerate(BITS_17) if n in botones)
    p[18] = 0                    # HOME
    p[19] = 1 if toque else 0
    p[20], p[21], p[22], p[23] = lx, ly, rx, ry
    for i, n in enumerate(ANALOGICOS):
        p[24 + i] = 255 if n in botones else 0
    if toque:
        id_toque, x, y = toque
        p[36] = 1
        p[37] = id_toque
        struct.pack_into("<HH", p, 38, x, y)
    # 42..47: segundo toque en cero.
    struct.pack_into("<Q", p, 48, tiempo_us)
    struct.pack_into("<6f", p, 56, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0)
    return cerrar_paquete(b"DSUS", id_servidor, MSG_DATOS, bytes(p))


def decodificar_datos(payload: bytes) -> dict:
    botones = {n for i, n in enumerate(BITS_16) if payload[16] & (1 << (7 - i))}
    botones |= {n for i, n in enumerate(BITS_17) if payload[17] & (1 << (7 - i))}
    toque = None
    if payload[36]:
        x, y = struct.unpack_from("<HH", payload, 38)
        toque = (payload[37], x, y)
    return {
        "num": struct.unpack_from("<I", payload, 12)[0],
        "botones": botones,
        "lx": payload[20], "ly": payload[21], "rx": payload[22], "ry": payload[23],
        "toque": toque,
        "bateria": payload[10],
        "tiempo_us": struct.unpack_from("<Q", payload, 48)[0],
    }


def comparar_con_referencia(paquete: bytes, id_servidor: int):
    """Reconstruye el paquete con la referencia; devuelve la lista de offsets distintos."""
    d = decodificar_datos(paquete[20:])
    ref = armar_datos_referencia(id_servidor, d["num"], d["botones"], d["lx"], d["ly"],
                                 d["rx"], d["ry"], d["toque"], d["bateria"], d["tiempo_us"])
    return [i for i in range(len(ref)) if i >= len(paquete) or ref[i] != paquete[i]]


def texto_datos(d: dict) -> str:
    b = " ".join(sorted(d["botones"])) or "-"
    t = f" toque id{d['toque'][0]} ({d['toque'][1]},{d['toque'][2]})" if d["toque"] else ""
    return (f"#{d['num']:6d} izq=({d['lx']:3d},{d['ly']:3d}) der=({d['rx']:3d},{d['ry']:3d})"
            f" bat=0x{d['bateria']:02X} botones: {b}{t}")


def autoprueba() -> int:
    """Sin consola: arma paquetes de referencia y comprueba que se decodifican igual."""
    casos = [
        (set(), 128, 128, 128, 128, None),
        ({"A", "Izq", "Share", "L2"}, 0, 255, 200, 10, (3, 319, 239)),
        (set(BITS_16) | set(BITS_17), 255, 0, 0, 255, (255, 0, 0)),
    ]
    fallos = 0
    for n, (bot, lx, ly, rx, ry, toque) in enumerate(casos):
        pkt = armar_datos_referencia(0x12345678, n, bot, lx, ly, rx, ry, toque, 0xEE, 123456789)
        tipo, payload, ids = verificar(pkt)
        assert len(pkt) == 100 and tipo == MSG_DATOS and ids == 0x12345678
        d = decodificar_datos(payload)
        ok = (d["botones"] == bot and (d["lx"], d["ly"], d["rx"], d["ry"]) == (lx, ly, rx, ry)
              and d["toque"] == toque and not comparar_con_referencia(pkt, ids))
        print(f"caso {n}: {'OK' if ok else 'FALLO'}  {texto_datos(d)}")
        fallos += not ok
    # Comprobación de offsets clave de la tabla.
    pkt = armar_datos_referencia(1, 0, {"Izq", "Y"}, 1, 2, 3, 4, (9, 0x0102, 0x0304))
    assert pkt[20 + 16] == 0x80 and pkt[20 + 17] == 0x80
    assert pkt[40:44] == bytes([1, 2, 3, 4])
    assert pkt[56:62] == bytes([1, 9, 0x02, 0x01, 0x04, 0x03])
    assert struct.unpack_from("<f", pkt, 80)[0] == -1.0
    print("offsets de la tabla: OK")
    return 1 if fallos else 0


def main() -> int:
    if len(sys.argv) >= 2 and sys.argv[1] == "--autoprueba":
        return autoprueba()
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    ip = sys.argv[1]
    duracion = float(sys.argv[2]) if len(sys.argv) >= 3 else 10.0
    destino = (ip, PUERTO)
    id_cliente = random.getrandbits(32)

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0.1)

    pedir_version = cerrar_paquete(b"DSUC", id_cliente, MSG_VERSION, b"")
    pedir_info = cerrar_paquete(b"DSUC", id_cliente, MSG_INFO, struct.pack("<i", 4) + bytes([0, 1, 2, 3]))
    # flags 0 = todos los mandos; slot 0; MAC en ceros.
    pedir_datos = cerrar_paquete(b"DSUC", id_cliente, MSG_DATOS, bytes([0, 0]) + bytes(6))

    s.sendto(pedir_version, destino)
    s.sendto(pedir_info, destino)
    s.sendto(pedir_datos, destino)
    ultima_peticion = time.monotonic()

    inicio = time.monotonic()
    ultimo_print = 0.0
    cuenta = {"version": 0, "info": 0, "datos": 0, "errores": 0, "difs": 0}
    ultimo_num = None
    perdidos = 0

    while time.monotonic() - inicio < duracion:
        ahora = time.monotonic()
        if ahora - ultima_peticion >= 1.0:
            s.sendto(pedir_datos, destino)
            ultima_peticion = ahora
        try:
            paquete, origen = s.recvfrom(2048)
        except socket.timeout:
            continue
        except ConnectionResetError:
            # Windows avisa con ICMP "puerto inalcanzable" si aún no hay servidor.
            continue
        try:
            tipo, payload, id_servidor = verificar(paquete)
        except ValueError as e:
            cuenta["errores"] += 1
            print(f"[error] {e}")
            continue

        if tipo == MSG_VERSION:
            cuenta["version"] += 1
            ok = len(paquete) == 22
            print(f"[version] {struct.unpack_from('<H', payload)[0]} "
                  f"({len(paquete)} bytes{'' if ok else ', se esperaban 22'})")
        elif tipo == MSG_INFO:
            cuenta["info"] += 1
            mac = ":".join(f"{b:02X}" for b in payload[4:10])
            ok = len(paquete) == 32
            print(f"[info] slot {payload[0]} estado {payload[1]} modelo {payload[2]} "
                  f"conexion {payload[3]} mac {mac} bateria 0x{payload[10]:02X}"
                  f"{'' if ok else f' ({len(paquete)} bytes, se esperaban 32)'}")
        elif tipo == MSG_DATOS:
            cuenta["datos"] += 1
            if len(paquete) != 100:
                cuenta["errores"] += 1
                print(f"[error] datos de {len(paquete)} bytes, se esperaban 100")
                continue
            difs = comparar_con_referencia(paquete, id_servidor)
            if difs:
                cuenta["difs"] += 1
                if cuenta["difs"] <= 5:
                    print(f"[dif] offsets distintos de la referencia: {difs[:16]}")
            d = decodificar_datos(payload)
            if ultimo_num is not None and d["num"] > ultimo_num + 1:
                perdidos += d["num"] - ultimo_num - 1
            ultimo_num = d["num"]
            if ahora - ultimo_print >= 0.25:
                print(texto_datos(d))
                ultimo_print = ahora
        else:
            print(f"[?] tipo 0x{tipo:06X}")

    seg = time.monotonic() - inicio
    print(f"\nResumen ({seg:.1f} s): version {cuenta['version']}, info {cuenta['info']}, "
          f"datos {cuenta['datos']} ({cuenta['datos'] / seg:.1f}/s), perdidos {perdidos}, "
          f"errores {cuenta['errores']}, distintos de la referencia {cuenta['difs']}")
    ok = (cuenta["version"] >= 1 and cuenta["info"] == 4 and cuenta["datos"] > 0
          and cuenta["errores"] == 0 and cuenta["difs"] == 0)
    print("RESULTADO:", "OK" if ok else "FALLO")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
