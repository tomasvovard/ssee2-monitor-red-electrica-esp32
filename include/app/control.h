/**
 * @file control.h
 * @brief Sistema de control automático de cargas con protección por sobrecorriente y tensión
 * 
 * Este módulo implementa el núcleo del sistema de gestión de cargas mediante dos
 * máquinas de estados finitas (FSM) cooperativas:
 * 
 * - **FSM Global**: Protección por sobrecorriente (Imax) con histéresis y detección
 *   de fallas reiterativas. Controla la habilitación general de todas las cargas.
 * 
 * - **FSM Individual**: Protección por tensión fuera de rango (Vmin/Vmax) con
 *   histéresis independiente para cada carga. Respeta prioridades y auto-reposición.
 * 
 * El sistema opera en dos modos:
 * - AUTO: Control mediante FSMs con protecciones activas
 * - MANUAL: Control directo vía comandos UART/MQTT (protecciones deshabilitadas)
 * 
 * @note La configuración persiste en NVS flash y se carga al inicio
 * @warning El acceso concurrente está protegido por control_mutex interno
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef CONTROL_H
#define CONTROL_H

#include "app/measure.h"
#include "system_config.h"

/* ========================================================================== */
/*                      PARÁMETROS POR DEFECTO                                */
/* ========================================================================== */

/**
 * @defgroup control_defaults Valores por defecto de configuración
 * @{
 */

/** @brief Corriente máxima RMS admisible por defecto [A]
 *  @note Este valor se carga al inicializar si no hay configuración en NVS 
 *  @warning Este valor no debe superar los 5A, limitación del sensor ACS712 */
#define DEFAULT_IMAX 5.0f

/** @brief Tensión RMS mínima admisible por defecto [V]
 *  @note Valor -1 deshabilita la protección de subtensión */
#define DEFAULT_VMIN 200

/** @brief Tensión RMS máxima admisible por defecto [V]
 *  @note Valor -1 deshabilita la protección de sobretensión */
#define DEFAULT_VMAX 250

/** @brief Estado por defecto de auto-reposición
 *  @note true: la carga se reconecta automáticamente tras recuperación de falla */
#define DEFAULT_AUTO_REC true

/** @} */ // end of control_defaults

/* ========================================================================== */
/*                      PARÁMETROS DE HISTÉRESIS                              */
/* ========================================================================== */

/**
 * @defgroup control_hysteresis Bandas de histéresis para prevenir chattering
 * @{
 */

/** @brief Banda de histéresis para protección de sobrecorriente [%]
 * 
 * La protección de corriente opera con dos umbrales:
 * - Disparo: Imax
 * - Rearme: Imax × (1 - IMAX_HYST_PRC/100)
 * 
 */
#define IMAX_HYST_PRC 10.0f

/** @brief Banda de histéresis para protección de tensión [%]
 * 
 * Similar a corriente, pero con umbral más ajustado debido a menor
 * variabilidad de la red eléctrica.
 * 
 */
#define VRANGE_HYST_PRC 5.0f

/** @} */ // end of control_hysteresis

/* ========================================================================== */
/*                      PARÁMETROS DE PROTECCIÓN                              */
/* ========================================================================== */

/**
 * @defgroup control_protection Parámetros de protección contra fallas reiterativas
 * @{
 */

/** @brief Número máximo de fallas de corriente consecutivas antes de bloqueo permanente
 * 
 * Ventana de detección definida por CONTROL_REPET_I_RST_MS:
 * - 1 falla → Se permite recuperación automática
 * - 2 fallas en <timeout → Bloqueo permanente (requiere intervención manual)
 * 
 * Esto previene ciclos infinitos de ON/OFF en caso de sobrecarga persistente.
 * 
 * @warning Valores <2 pueden causar bloqueos por transitorios de arranque normales
 * 
 */
#define MAX_FAIL_I 2

/** @} */ // end of control_protection

/* ========================================================================== */
/*                      ENUMERACIONES Y ESTRUCTURAS                           */
/* ========================================================================== */

/**
 * @brief Modos de operación del sistema de control
 */
typedef enum {
    CTRL_MODE_AUTO = 0, /**< Control automático mediante FSMs con protecciones activas */
    CTRL_MODE_MAN       /**< Control manual directo vía UART/MQTT (protecciones deshabilitadas) */
} ctrl_mode_t;

/**
 * @brief Configuración de protección y comportamiento de una carga individual
 */
typedef struct {
    int16_t v_min;      /**< Tensión RMS mínima admisible [V]. Usar -1 para deshabilitar */
    int16_t v_max;      /**< Tensión RMS máxima admisible [V]. Usar -1 para deshabilitar */
    bool auto_rec;      /**< true: reconexión automática tras recuperación de falla */
    uint8_t priority;   /**< Prioridad de desconexión: 0=última en desconectar*/
} load_cfg_t;

/**
 * @brief Configuración global del sistema de control de cargas
 * 
 * Esta estructura se persiste en NVS flash y define el comportamiento
 * completo del sistema de protección.
 */
typedef struct {
    float imax;                 /**< Corriente máxima RMS admisible total [A] */
    load_cfg_t load[NUM_LOADS]; /**< Configuración individual de cada carga */
} sys_load_cfg_t; 

/**
 * @brief Estados de la FSM global (protección por sobrecorriente)
 *
 */
typedef enum {
    CONTROL_GLOBAL_OK = 0,  /**< Operación normal - cargas habilitadas */
    CONTROL_GLOBAL_FAIL_I,  /**< Sobrecorriente detectada - todas las cargas desconectadas */
    CONTROL_GLOBAL_REC,     /**< Período de recuperación - esperando estabilización */
    CONTROL_GLOBAL_MAN_REC  /**< Bloqueo por fallas reiterativas - requiere reset manual */
} control_global_fsm_t;

/**
 * @brief Estados de la FSM individual (protección por tensión de cada carga)
 */
typedef enum {
    CONTROL_INDIV_ON = 0,   /**< Carga conectada - tensión dentro de rango */
    CONTROL_INDIV_FAIL_V,   /**< Tensión fuera de rango - carga desconectada */
    CONTROL_INDIV_OFF       /**< Carga desconectada - esperando auto-reposición o comando manual */
} control_indiv_fsm_t;

/* ========================================================================== */
/*                      FUNCIONES PÚBLICAS                                    */
/* ========================================================================== */

/**
 * @defgroup control_init Inicialización y reset
 * @{
 */

/**
 * @brief Inicializa el módulo de control y carga configuración desde NVS
 * 
 * - Crea el mutex de protección concurrente
 * - Inicializa FSMs en estado seguro (todas las cargas OFF)
 * - Intenta cargar configuración desde NVS
 * - Si no hay config guardada, usa valores DEFAULT_*
 * 
 * @note Debe llamarse antes de crear task_control()
 */
void control_init();

/**
 * @brief Resetea el sistema a configuración por defecto sin borrar NVS
 * 
 * - Restaura valores DEFAULT_*
 * - Reinicializa ambas FSMs
 * - Desconecta todas las cargas
 * - NO borra la flash NVS (usar nvs_reset_default() para eso)
 * 
 * @note Útil para recuperación tras fallas o cambios de configuración conflictivos
 */
void control_reset();

/** @} */ // end of control_init

/**
 * @defgroup control_mode Control de modo de operación
 * @{
 */

/**
 * @brief Cambia el modo de operación del sistema
 * 
 * Al cambiar de MANUAL a AUTO:
 * - Se reinicializan las FSMs
 * - Las cargas parten desde OFF y el sistema evalúa protecciones
 * 
 * Al cambiar de AUTO a MANUAL:
 * - Las protecciones se deshabilitan
 * - El estado actual de las cargas se mantiene
 * - Control pasa 100% a comandos externos
 * 
 * @param mode Nuevo modo de operación
 * @note Thread-safe (protegido por mutex)
 */
void control_set_mode(ctrl_mode_t mode);

/**
 * @brief Obtiene el modo de operación actual
 * @return Modo actual (AUTO o MANUAL)
 * @note Thread-safe
 */
ctrl_mode_t control_get_mode();

/** @} */ // end of control_mode

/**
 * @defgroup control_manual Control manual de cargas
 * @{
 */

/**
 * @brief Establece el estado de una carga en modo MANUAL
 * 
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @param on true=conectar, false=desconectar
 * @return true si exitoso, false si id inválido
 * 
 * @warning Esta función NO verifica protecciones. Solo válida en modo MANUAL.
 * @warning En modo AUTO, los cambios manuales serán sobreescritos por las FSMs
 * @note Thread-safe y actualiza el hardware GPIO inmediatamente
 */
bool control_set_load_state(uint8_t id, bool on);

/**
 * @brief Obtiene el estado actual de una carga
 * 
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @param[out] on Puntero donde almacenar el estado (true=ON, false=OFF)
 * @return true si exitoso, false si id inválido o puntero NULL
 * @note Thread-safe - lee el estado de software (puede diferir del hardware si hay falla GPIO)
 */
bool control_get_load_state(uint8_t id, bool *on);

/**
 * @brief Verifica integridad hardware-software de todas las cargas
 * 
 * Lee el estado real de los GPIO y lo compara con el estado de software.
 * Si detecta inconsistencias, intenta resincronizar y registra errores.
 * 
 */
void control_check_outputs_integrity();

/** @} */ // end of control_manual

/**
 * @defgroup control_config Configuración de protecciones
 * @{
 */

/**
 * @brief Obtiene copia de la configuración completa del sistema
 * 
 * @param[out] out Puntero a estructura donde copiar la configuración
 * @return true si exitoso, false si puntero NULL
 * @note Thread-safe - devuelve snapshot atómico de la configuración
 */
bool control_get_cfg(sys_load_cfg_t *out);

/**
 * @brief Establece tensión RMS mínima admisible para una carga
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @param v_min Tensión mínima [V], o -1 para deshabilitar protección
 * @return true si exitoso, false si id inválido
 */
bool control_set_load_vmin(uint8_t id, int16_t v_min);

/**
 * @brief Establece tensión RMS máxima admisible para una carga
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @param v_max Tensión máxima [V], o -1 para deshabilitar protección
 * @return true si exitoso, false si id inválido
 */
bool control_set_load_vmax(uint8_t id, int16_t v_max);

/**
 * @brief Habilita/deshabilita auto-reposición para una carga
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @param en true=auto-reposición activa, false=reposición manual
 * @return true si exitoso, false si id inválido
 * @note Con auto_rec=false, la carga requiere comando manual para reconectar
 */
bool control_set_load_auto_rec(uint8_t id, bool en);

/**
 * @brief Establece prioridad de desconexión de una carga
 * 
 * En caso de sobrecorriente, las cargas se desconectan en orden ascendente
 * de prioridad (primero las de mayor número).
 * 
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @param pr Prioridad [0=máxima, 255=mínima]
 * @return true si exitoso, false si id inválido
 * 
 */
bool control_set_load_priority(uint8_t id, uint8_t pr);

/**
 * @brief Establece corriente RMS máxima admisible del sistema
 * @param imax Corriente máxima [A]
 * @return true (siempre exitoso por ahora)
 * @note Valor típico: 5.0A para aplicaciones residenciales monofásicas
 */
bool control_set_imax(float imax);

/**
 * @brief Obtiene tensión RMS mínima configurada de una carga
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @return Tensión mínima [V], o -1 si id inválido o protección deshabilitada
 */
int16_t control_get_v_min(uint8_t id);

/**
 * @brief Obtiene tensión RMS máxima configurada de una carga
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @return Tensión máxima [V], o -1 si id inválido o protección deshabilitada
 */
int16_t control_get_v_max(uint8_t id);

/** @} */ // end of control_config

/**
 * @defgroup control_nvs Persistencia en memoria flash
 * @{
 */

/**
 * @brief Guarda configuración actual en memoria flash NVS
 * 
 * Persiste toda la estructura sys_load_cfg_t en NVS para sobrevivir
 * a reinicios y pérdidas de alimentación.
 * 
 * @return true si guardado exitoso, false en caso de error de escritura
 * @note La energía acumulada debe guardarse por separado con nvs_save_energy()
 */
bool control_save_to_nvs();

/**
 * @brief Carga configuración desde memoria flash NVS
 * 
 * Si no hay configuración guardada o hay error de lectura, mantiene
 * la configuración actual sin modificar.
 * 
 * @return true si carga exitosa, false si no hay config guardada o error
 * @note Llamar después de control_init() para restaurar última configuración
 */
bool control_load_from_nvs();

/** @} */ // end of control_nvs

/**
 * @defgroup control_fsm Máquinas de estados finitas
 * @{
 */

/**
 * @brief Inicializa la FSM global en estado seguro
 * 
 * - Estado inicial: CONTROL_GLOBAL_OK
 * - Contador de fallas: 0
 * - Timers desactivados
 * 
 * @note Llamada automáticamente por control_init() y al cambiar a modo AUTO
 */
void control_global_fsm_init();

/**
 * @brief Inicializa la FSM individual de una carga en estado seguro
 * 
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @note Estado inicial depende del estado actual de la carga (ON/OFF)
 */
void control_indiv_fsm_init(uint8_t id);

/**
 * @brief Ejecuta un ciclo de la FSM global (protección por sobrecorriente)
 * 
 * Implementa máquina de estados con:
 * - Histéresis de 10% en corriente (IMAX_HYST_PRC)
 * - Detección de fallas reiterativas con ventana temporal
 * - Timer de recuperación (CONTROL_REC_I_TIME_MS)
 * - Bloqueo permanente tras MAX_FAIL_I fallas consecutivas
 * 
 * @param I Corriente RMS medida en Amperes
 * @return true si las cargas pueden estar activas, false si deben desconectarse
 * 
 * @note Debe llamarse cada TASK_PERIOD_CONTROL_MS desde task_control()
 * @warning El acceso a variables globales está protegido internamente por mutex
 * 
 * @see control_global_fsm_t para diagrama de estados
 */
bool control_global_fsm(float I);

/**
 * @brief Ejecuta un ciclo de la FSM individual (protección por tensión)
 * 
 * Implementa máquina de estados con:
 * - Histéresis de 5% en tensión (VRANGE_HYST_PRC)
 * - Auto-reposición con timer configurable (CONTROL_REC_V_TIME_MS)
 * - Respeto de prioridades para desconexión ordenada
 * 
 * @param id Identificador de carga [0, NUM_LOADS-1]
 * @param vrms Tensión RMS medida en Volts
 * @return true si la carga puede estar activa, false si debe desconectarse
 * 
 * @pre id debe ser válido (< NUM_LOADS)
 * @note Respeta configuración auto_rec y prioridad de sys_load_cfg_t
 * @note Se ejecuta DESPUÉS de control_global_fsm() (AND lógico de ambas FSMs)
 * 
 * @see control_indiv_fsm_t para diagrama de estados
 */
bool control_indiv_fsm(uint8_t id, int16_t vrms);

/** @} */ // end of control_fsm

/**
 * @defgroup control_task Tarea de FreeRTOS
 * @{
 */

/**
 * @brief Tarea principal del sistema de control
 * 
 * Responsabilidades:
 * - Lectura periódica del estado de medición (tensión/corriente)
 * - Ejecución de FSM global (protección sobrecorriente)
 * - Ejecución de FSM individual para cada carga (protección tensión)
 * - Actualización de GPIO de cargas respetando prioridades
 * - Sincronización de estado con módulo state.h
 * 
 * Período: TASK_PERIOD_CONTROL_MS 
 * Prioridad: TASK_PRIORITY_CONTROL
 * Stack: TASK_STACK_CONTROL
 * 
 * @param pvParameters Parámetro estándar de FreeRTOS (no usado)
 * 
 * @note Esta tarea solo opera en modo AUTO. En modo MANUAL está idle.
 */
void task_control(void *pvParameters);

#endif
