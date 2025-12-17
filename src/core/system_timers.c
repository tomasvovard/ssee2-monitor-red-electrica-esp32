#include "core/system_timers.h"

void timer_start(sys_timer_t *timer, uint32_t tout_ms){
    timer->start_tick = xTaskGetTickCount();
    timer->timeout_ms = tout_ms;
    timer->active = true;
}

bool timer_expired(sys_timer_t *timer){
    if(!timer->active) return false;
    TickType_t lapse = xTaskGetTickCount() - timer->start_tick;
    return (pdTICKS_TO_MS(lapse) >= timer->timeout_ms);
}

void timer_stop(sys_timer_t *timer){
    timer->active = false;
}