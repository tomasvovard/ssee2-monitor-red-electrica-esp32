# ssee2-monitor-red-electrica-esp32
Montior de red eléctrica con control de cargas basado en ESP32 con freeRTOS, con comunicación UART y MQTT

## Funcionalidades principales
- Muestreo de tensión y corriente a 20 kHz
- Cálculo sobre ventana de 4000 muestras (10 períodos a 50 Hz):
  - Vrms, Irms
  - Potencia activa (P), aparente (S)
  - Factor de potencia (FP)
  - Energía acumulada
- Control de cargas:
  - Modo MANUAL (accionamiento directo)
  - Modo AUTO (protección por sobrecorriente y tensión fuera de rango)
- Interfaz y comunicaciones:
  - Protocolo UART con comandos de diagnóstico, medición, modo, cargas y configuración (con login ADMIN)
  - Publicación/operación IoT mediante MQTT (broker Mosquitto) e interfaz Node-RED
  - Visualización local en display I2C

## Hardware
- ESP32 (placa de desarrollo)
- Medición de tensión: amplificador diferencial de alta impedancia, atenuación ~250 V/V, salida centrada en 1.65 V (ADC)
- Medición de corriente: ACS712 5A, sensibilidad 185 mV/A, salida centrada en 2.5 V

## Software
- FreeRTOS con tareas para adquisición, control de cargas, UART, MQTT y display I2C
- Estado global del sistema (mediciones/cargas/fallas) protegido con mutex
- Transmisión UART mediante colas para evitar problemas de concurrencia

## Compilación / ejecución
Este proyecto se desarrolló en VSCode con la extensión PlatformIO. 

## Autor
Tomás Vovard
