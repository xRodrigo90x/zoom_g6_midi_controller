# ESP32-S3 USB MIDI Host para Zoom G6

Este proyecto convierte un **ESP32-S3** en un controlador MIDI USB Host capaz de gestionar cambios de parches y bancos en una pedalera **Zoom G6**. A diferencia de los controladores estándar, este código implementa la secuencia específica de comandos necesaria para navegar por la estructura compleja de bancos de Zoom.

## 🚀 Características
- **USB Host Mode:** Utiliza el stack nativo de USB del ESP32-S3 para comunicarse directamente con la Zoom G6 sin necesidad de un PC.
- **Navegación por Bancos:** Implementa selección de banco mediante **Bank LSB (CC 32)**, permitiendo acceso a todos los bancos (A-Z).
- **Control MIDI de 12 Bytes:** Envío sincronizado de mensajes Bank MSB, Bank LSB y Program Change en una sola ráfaga USB.

## 🛠 Requisitos de Hardware
- **ESP32-S3** (probado en placas con conector USB nativo).
- **Zoom G6** conectada mediante cable USB al puerto USB-OTG del ESP32.
- Alimentación adecuada para el bus USB (5V).

## 💻 Detalles Técnicos
El proyecto utiliza el componente `usb_host` de **ESP-IDF** para detectar la interfaz MIDI de la Zoom G6 (usualmente la interfaz 4). 

### Secuencia de Comando MIDI
Para lograr cambios de parche en bancos lejanos (ej. Banco Z, Patch 1), el sistema envía una ráfaga de 12 bytes estructurada en paquetes USB MIDI de 4 bytes:

1. **Bank MSB (CC 0):** `0B B0 00 00`
2. **Bank LSB (CC 32):** `0B B0 20 19` (donde `19` hex es el banco 25/Z)
3. **Program Change:** `0C C0 00 00` (Patch 1)

## 📂 Estructura del Proyecto
- `main/`: Código fuente principal.
  - `class_driver.c`: Manejo del stack USB Host y lógica de envío MIDI.
- `sdkconfig`: Configuración del proyecto para ESP-IDF v5.x.

## 📝 Próximos Pasos
- [ ] Implementar botones físicos (GPIO) para navegación Up/Down.
- [ ] Crear una capa de abstracción para nombres de bancos.
- [ ] Añadir soporte para lectura de mensajes (MIDI IN) como Tap Tempo.

---
*Desarrollado con ESP-IDF y mucha depuración con MIDI-OX.*