/**
 * @file nvs_config.h
 * @brief Persistencia de configuración y energía en memoria flash NVS
 * 
 * Wrapper sobre ESP-IDF NVS (Non-Volatile Storage) para guardar/cargar:
 * - Configuración del sistema de control (sys_load_cfg_t)
 * - Energía acumulada (kWh)
 * 
 * @note Requiere nvs_flash_init() antes de usar estas funciones
 * @author Tomás Vovard
 * @date Diciembre 2025
 */


#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include "app/control.h"
#include <stdbool.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

/** @brief Namespace NVS donde se almacenan los datos del sistema */
#define NVS_NAMESPACE "medidor_cfg"

/**
 * @brief Inicializa el subsistema NVS flash
 * 
 * Llama a nvs_flash_init() y maneja errores de espacio/versión
 * reseteando la flash si es necesario.
 * 
 * @note Debe llamarse UNA VEZ al inicio del sistema antes que otras funciones NVS
 */
void nvs_config_init();

/**
 * @brief Guarda configuración completa en NVS
 * @param cfg Configuración a persistir
 * @return true si exitoso, false en caso de error
 */
bool nvs_save_config(const sys_load_cfg_t *cfg);

/**
 * @brief Carga configuración desde NVS
 * @param[out] cfg Estructura donde cargar la configuración
 * @return true si hay datos guardados, false si NVS vacío o error
 */
bool nvs_load_config(sys_load_cfg_t *cfg);

/**
 * @brief Guarda energía acumulada en NVS
 * @param energy Energía en kWh
 * @return true si exitoso, false en caso de error
 */
bool nvs_save_energy(double energy);

/**
 * @brief Carga energía acumulada desde NVS
 * @return Energía en kWh, o 0.0 si no hay datos guardados
 */
double nvs_load_energy();

/**
 * @brief Resetea toda la configuración NVS a valores por defecto
 * 
 * Borra TODOS los datos del namespace "medidor_cfg".
 * 
 * @return true si exitoso, false en caso de error
 * @warning Esta operación es irreversible
 */
bool nvs_reset_default();

/**
 * @brief Verifica si NVS fue inicializado
 * @return true si nvs_config_init() fue llamado exitosamente
 */
bool nvs_is_init();

#endif // NVS_CONFIG_H