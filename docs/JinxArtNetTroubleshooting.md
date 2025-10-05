# Compatibilidad Art-Net con Jinx!

Este documento resume los requisitos más relevantes del protocolo Art-Net y los puntos que Jinx! comprueba cuando descubre nodos en la red. Úsalo como lista de verificación rápida al depurar un nodo PixelEtherLED que responda a paquetes **ArtPoll** pero no aparezca en el software.

## Paquetes ArtPoll y ArtPollReply

- El controlador envía **ArtPoll** (UDP/6454) y los nodos responden con **ArtPollReply** **por unicast**. El broadcast del *reply* está explícitamente prohibido por la especificación.
- `OpCode` para ArtPoll = `0x2000`; ArtPollReply = `0x2100` (little-endian).
- El paquete siempre comienza con la cadena `"Art-Net\0"`.
- Puerto UDP fijo: origen y destino `0x1936` (6454).
- Longitud mínima aceptada por Jinx! para un ArtPollReply: **207 bytes**. Cualquier respuesta más corta suele descartarse.

## Dirección de universos (Art-Net 3/4)

- El **Port-Address** combina `Net` (7 bits), `SubNet` (4 bits) y `Universe` (4 bits).
- Jinx! trabaja con este esquema, así que asegúrate de que `NetSwitch`, `SubSwitch`, `SwOut[]` y `SwIn[]` sean coherentes con los universos reales del nodo.

## Lista de comprobación para que Jinx! detecte el nodo

1. **Interfaz de red correcta en Jinx!**  
   Selecciona manualmente la NIC que esté en la subred del nodo (por ejemplo 192.168.0.x). El modo automático no siempre escucha en todas las tarjetas.
2. **Firewall de Windows**  
   Permite tráfico UDP 6454 de entrada y salida para Jinx!. Un firewall bloqueando el puerto evitará que la aplicación reciba el ArtPollReply aunque Wireshark lo capture.
3. **Net / SubNet / Universe**  
   Configura en Jinx! los mismos valores que reporta el nodo. Si usas `Net > 0` o `SubNet != 0`, mapea los universos exactos en la pestaña de salida.
4. **PortTypes / NumPorts**  
   `NumPortsLo` debe ser mayor a 0 y cada puerto habilitado necesita `PortTypes[i]` configurado con el bit de salida (`0x80`) más el tipo de protocolo (DMX = `0x00`). Valores en cero hacen que muchos controladores ignoren el puerto.
5. **ShortName / LongName / Style**  
   Cadena terminada en `NULL` y `Style = 0x00 (StNode)`.

## Buenas prácticas en el firmware del nodo

- Copia el nombre corto/largo con terminación nula (`\0`).
- Establece `BindIndex` en `1` para el primer adaptador físico.
- Usa `GoodOutput[i] = 0x80` para marcar salidas activas y `GoodInput[i] = 0x00` si no hay entradas.
- Mantén `Status1` y `Status2` con valores válidos (por ejemplo `0xD0` y `0x00`) a menos que necesites reportar condiciones especiales.
- El campo `NodeReport` puede seguir el formato `"#0001 [ok] ..."` para compatibilidad con consolas.

## Depuración adicional

- Verifica con Wireshark que el nodo responde por unicast a la IP del controlador y no a broadcast.
- Si el nodo usa Art-Net 4, mantén los campos heredados (Art-Net 3) inicializados correctamente para conservar la compatibilidad con Jinx!.

