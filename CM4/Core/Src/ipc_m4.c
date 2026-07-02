/*
 * ipc_m4.c
 *
 *  Created on: Jun 15, 2026
 *      Author: User
 */


#include "ipc.h"
#include "stm32h7xx_hal.h"

__attribute__((section(".ipc_shared"), aligned(32)))
volatile ipc_shared_t g_ipc;


volatile uint32_t g_landing[16];   /* куда пишем значения */
static uint32_t   s_idx = 0;

void ipc_m4_init(void) {
    __HAL_RCC_HSEM_CLK_ENABLE();
    /* CM4 хочет прерывание, когда CM7 освободит семафор CMD */
    HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(IPC_HSEM_CMD));
    HAL_NVIC_SetPriority(HSEM2_IRQn, 6, 0);   /* на CM4 это HSEM2_IRQn */
    HAL_NVIC_EnableIRQ(HSEM2_IRQn);
}

void HSEM2_IRQHandler(void) {
    HAL_HSEM_IRQHandler();
}
void HAL_HSEM_FreeCallback(uint32_t mask) {
    if (mask & __HAL_HSEM_SEMID_TO_MASK(IPC_HSEM_CMD)) {
        __DMB();
        uint32_t value = g_ipc.mbox.p0;            /* приняли значение     */
        uint32_t slot  = s_idx++ & 0xF;            /* инкремент + слитно   */
        g_landing[slot] = value;                   /* записали в память    */
        __DSB();                                   /* запись дошла до RAM   */

        g_ipc.mbox.ack_d0     = (uint32_t)&g_landing[slot];  /* куда            */
        g_ipc.mbox.ack_status = IPC_ACK_OK;
        __DMB();
        g_ipc.mbox.ack_seq    = g_ipc.mbox.cmd_seq;          /* «исполнено»     */
        __DSB();
        /* звонок обратно на CM7 */
        HAL_HSEM_FastTake(IPC_HSEM_ACK);
        HAL_HSEM_Release(IPC_HSEM_ACK, 0);

        HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(IPC_HSEM_CMD));
    }
}
