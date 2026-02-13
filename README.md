# ESP32-S3 USB MIDI Host / Custom Controller for Zoom G6

![Target](https://img.shields.io/badge/Platform-ESP32--S3-orange?style=for-the-badge)
![App](https://img.shields.io/badge/Device-Zoom%20G6-black?style=for-the-badge)
![Mode](https://img.shields.io/badge/Mode-USB%20Host-red?style=for-the-badge)

## 📋 Descripción Técnica
Este sistema es un controlador de alto rendimiento que actúa como **USB Host** para gestionar la arquitectura de memoria de la pedalera **Zoom G6**. El firmware ha sido desarrollado bajo requerimientos técnicos específicos para eliminar la latencia de navegación manual, permitiendo el acceso directo y conmutación instantánea entre los bancos de usuario finales: **Z** y **AA**.

## 🏗 Arquitectura de Control (Custom Requirements)
El firmware implementa un mapeo de memoria rígido y optimizado para ejecución en vivo:

### 1. Gestión de Bancos y Parches (Hardcoded Logic)
La lógica está diseñada para alternar entre los bancos extremos del sistema mediante ráfagas sincronizadas de 12-bytes (3 paquetes USB MIDI de 4-bytes cada uno):

| Botón Físico | Banco Objetivo | LSB Value (Hex) | Patch MIDI (PC) |
| :--- | :--- | :--- | :--- |
| **B1 - B4** | Banco Z | `0x19` | 00 - 03 |
| **B5 - B8** | Banco AA | `0x1A` | 00 - 03 |



### 2. Feedback Visual y UI
* **Secuencia de Boot (Failsafe):** 5s silencio → Barrido Azul → 5 ciclos Arcoíris → 5 ráfagas Moradas (Confirmación visual de inicialización de periféricos).
* **Estado Activo:** Iluminación Verde de alta intensidad `(0, 200, 0)` para el LED del parche seleccionado.
* **Modo Standby:** Tras 8 minutos de inactividad, se activa un ciclo de arcoíris dinámico de bajo brillo para indicación de sistema "Alive" y protección de componentes.

## 🔧 Configuración Crítica del Hardware y Entorno

### A. Gestión de Puertos USB-C
El ESP32-S3 dispone habitualmente de dos puertos USB-C. Para este proyecto:
1.  **Puerto UART/USB:** Se utiliza para la programación, monitoreo serie (`idf.py monitor`), y su posterior alimentacion. 
2.  **Puerto USB-OTG (Nativo):** Es el puerto donde se conecta la **Zoom G6**. Internamente, el S3 utiliza este puerto para el stack de USB Host. No es necesario cablear pines externos, pero el firmware utiliza el periférico nativo asociado a GPIO 19/20 de forma interna.

### B. Modificación del Buffer de Transferencia (SDKConfig)
Para procesar ráfagas MIDI complejas (Bank MSB + LSB + PC) sin pérdida de paquetes:
1.  Ejecutar `idf.py menuconfig`.
2.  Navegar a **Component config** -> **USB Host Stack**.
3.  Ajustar **Config Descriptor Buffer** a `2048`.

> [!IMPORTANT]
> Este ajuste es vital para prevenir desbordamientos de buffer y disparos accidentales del Task Watchdog (WDT).

## 🔌 Asignación de Periféricos (Pinout)
| Periférico | Conexión / GPIO |
| :--- | :--- |
| **Puerto USB-C Nativo** | Conexión directa a Zoom G6 (Modo Host) |
| **LED Strip (WS2812B)** | `GPIO 39` |
| **Botones (Input Pull-up)** | `GPIO 6, 7, 8, 9, 10, 11, 12, 13` |



## 🛡 Estabilidad y Concurrencia
* **Arquitectura Multicore:** Core 0 dedicado exclusivamente a la gestión de eventos USB/MIDI; Core 1 dedicado a la lectura de sensores (GPIO) y renderizado de LEDs.
* **Debounce:** Filtro de software de 250ms para evitar falsos disparos por ruido mecánico.
* **Hot-Plug:** Gestión automática de conexión y desconexión de la pedalera sin necesidad de reiniciar el controlador.

---
> [!NOTE]
> Este firmware es una solución a medida para los bancos Z/AA. Cualquier expansión a otros bancos requiere la modificación de la tabla de constantes en `class_driver.c`.