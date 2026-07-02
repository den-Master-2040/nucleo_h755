/*
 * ipc_m7.c
 *
 *  Created on: Jun 15, 2026
 *      Author: Danil Zabrodin
 */

#include "ipc.h"
#include "stm32h7xx_hal.h"

volatile ipc_shared_t g_ipc __attribute__((section(".ipc_shared"), aligned(32)));

static volatile bool s_ack_ready = false;

/* вызвать один раз при старте */
void ipc_m7_init(void) {
    __HAL_RCC_HSEM_CLK_ENABLE();
    /* CM7 хочет прерывание, когда CM4 освободит семафор ACK */
    HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(IPC_HSEM_ACK));
    HAL_NVIC_SetPriority(HSEM1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(HSEM1_IRQn);
    g_ipc.ring.head = 0;
    g_ipc.ring.tail = 0;
    g_ipc.ring.drops = 0;
    __DSB();
}

/* отправить значение, дёрнуть CM4 */
void ipc_m7_send_value(uint32_t value) {
    s_ack_ready = false;
    g_ipc.mbox.p0 = value;
    __DMB();                              /* данные раньше публикации seq */
    g_ipc.mbox.cmd_seq++;                 /* публикуем команду           */
    __DSB();                              /* всё ушло до звонка           */
    /* «звонок» CM4: захватить и сразу отпустить семафор CMD */
    HAL_HSEM_FastTake(IPC_HSEM_CMD);      /* (или Take с нашим MasterID)  */
    HAL_HSEM_Release(IPC_HSEM_CMD, 0);
}

/* ISR: CM4 освободил ACK */
void HSEM1_IRQHandler(void) {
    HAL_HSEM_IRQHandler();                /* очистит флаг, вызовет callback ниже */
}
void HAL_HSEM_FreeCallback(uint32_t mask) {
    if (mask & __HAL_HSEM_SEMID_TO_MASK(IPC_HSEM_ACK)) {
        if (g_ipc.mbox.ack_seq == g_ipc.mbox.cmd_seq)   /* наш ответ */
            s_ack_ready = true;
        /* заново подписаться на следующий ACK */
        HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(IPC_HSEM_ACK));
    }
}

/* опрос из TCP-задачи: дождаться ответа */
bool ipc_m7_wait_ack(uint32_t *out_addr, uint32_t *out_status, uint32_t timeout_ms) {
    uint32_t t0 = HAL_GetTick();
    while (!s_ack_ready) {
        if (HAL_GetTick() - t0 > timeout_ms) return false;
        /* под FreeRTOS лучше vTaskDelay(1) или семафор вместо busy-wait */
    }
    *out_addr   = g_ipc.mbox.ack_d0;
    *out_status = g_ipc.mbox.ack_status;
    return true;
}
