/**
 * @file uart_handler.h
 * @brief Procesador de comandos UART con autenticación de sesión
 * 
 * Capa de lógica entre el protocolo UART (parsing) y el sistema de control.
 * Implementa autenticación básica con sesiones temporales (30 min timeout).
 * 
 * @author Tomás Vovard
 * @date Diciembre 2025
 */

#ifndef UART_HANDLER_H
#define UART_HANDLER_H

#include "uart_protocol.h"

/* ========================================================================== */
/*                      FUNCIONES DE AUTENTICACIÓN                            */
/* ========================================================================== */

/**
 * @brief Intenta autenticación de administrador
 * 
 * @param pass Contraseña a validar
 * @param[out] session Sesión a activar si login exitoso
 * 
 * @return true si contraseña correcta, false en caso contrario
 * 
 * @note Contraseña hardcodeada en implementación (admin123)
 * @note Session se marca como activa con nivel USER_ADMIN
 */
bool uart_login(const char *pass, session_t *session);

/**
 * @brief Verifica validez de sesión actual
 * 
 * @param session Sesión a verificar
 * 
 * @return true si sesión activa y no expirada, false en caso contrario
 * 
 * @note Timeout: 30 minutos desde último login
 */
bool uart_session_check(session_t *session);

/**
 * @brief Cierra sesión activa
 * 
 * @param session Sesión a cerrar
 * 
 * @note Retorna sesión a nivel USER_VIEWER
 */
void uart_logout(session_t *session);

/* ========================================================================== */
/*                      PROCESAMIENTO DE COMANDOS                             */
/* ========================================================================== */

/**
 * @brief Procesa comando parseado y genera respuesta
 * 
 * @param[in] cmd Comando con parámetros y sesión asociada
 * @param[out] resp Respuesta formateada ("OK ..." o "ERROR ...")
 * 
 * Comandos soportados:
 * - PING, LOGIN, LOGOUT, USERID (sin autenticación)
 * - MEAS, MODE, LOAD, DISPMODE (viewer)
 * - ENERGY, CFG (requiere admin)
 * 
 * @note Verifica permisos antes de ejecutar comandos protegidos
 */
void uart_process_command(uart_cmd_t *cmd, uart_resp_t *resp);

#endif // UART_HANDLER_H