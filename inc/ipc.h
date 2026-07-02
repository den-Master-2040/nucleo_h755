/*
 * ipc.h — ОБЩИЙ заголовок для CM7 и CM4 (H745/H747).
 *
 * ЭТОТ ФАЙЛ ОБЯЗАН БЫТЬ БАЙТ-В-БАЙТ ОДИНАКОВ НА ОБОИХ ЯДРАХ.
 * Один физический экземпляр g_ipc лежит в разделяемом регионе SRAM4 (D3),
 * который оба линкер-скрипта кладут по одному адресу (секция .ipc_shared),
 * а MPU помечает как non-cacheable (для v1 — весь регион; см. заметки внизу).
 *
 * Два независимых канала:
 *   ring    — данные, CM4 -> CM7 (кадры АЦП), free-running с дропом
 *   mailbox — команды, CM7 -> CM4 + ack обратно
 * Сигналинг «готово» делается HSEM-ом ОТДЕЛЬНО, вне этого файла.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/* __DMB обычно приходит из CMSIS (core_cm7.h/core_cm4.h). Фоллбэк на всякий. */
//#ifndef __DMB
  //#define __DMB(void) __asm volatile ("dmb 0xF" ::: "memory")
//#endif

/* ---- конфигурация ------------------------------------------------------- */
#define IPC_FRAME_MAX_SAMPLES  1024u   /* потолок точек в кадре              */
#define IPC_RING_FRAMES        8u      /* ДОЛЖНО быть степенью двойки        */

/* флаги кадра */
#define IPC_FLAG_TRIG_VALID    (1u << 0)  /* trigger_offset валиден          */
#define IPC_FLAG_SIGNAL        (1u << 1)  /* сигнал есть (не тишина)         */
#define IPC_FLAG_CLIPPED       (1u << 2)  /* упёрлись в 0 или 4095           */

/* в ipc.h, подальше от boot-семафора 0 */
#define IPC_HSEM_CMD   4u   /* было 0 */
#define IPC_HSEM_ACK   5u   /* было 1 */

/* команды CM7 -> CM4 */
enum {
    IPC_CMD_NONE = 0,
    IPC_CMD_STREAM_ON,      /* включить free-running publishing            */
    IPC_CMD_STREAM_OFF,     /* выключить                                   */
    IPC_CMD_ONESHOT,        /* отдать ровно один кадр                      */
    IPC_CMD_SET_POINTS,     /* p0 = n_samples (<= MAX)                     */
    IPC_CMD_SET_ADC_RATE,   /* p0 = частота дискретизации, Гц              */
    IPC_CMD_SET_DAC_FREQ,   /* p0 = частота синуса на ЦАП, Гц              */
    IPC_CMD_SET_PWM_DUTY,   /* p0 = скважность, ‰ (0..1000)                */
    IPC_CMD_SET_TRIG_MODE,  /* p0 = 0 свободно / 1 по восходящему фронту   */
    IPC_CMD_RESET_STATE,    /* переинициализировать периферию и индексы    */
};

/* статусы ack */
enum {
    IPC_ACK_OK = 0,
    IPC_ACK_ERR_PARAM,      /* недопустимый параметр                       */
    IPC_ACK_ERR_STATE,      /* нельзя в текущем состоянии                  */
};

/* ---- кадр АЦП ----------------------------------------------------------- */
/* Натуральное выравнивание полей: НЕ пакуем. Хедер = ровно 32 байта.       */
typedef struct {
    uint32_t seq;             /* 0  номер кадра                            */
    uint16_t n_samples;       /* 4  валидных точек                         */
    uint16_t flags;           /* 6  IPC_FLAG_*                             */
    uint32_t sample_rate_hz;  /* 8  частота дискретизации этого кадра      */
    int32_t  trigger_offset;  /* 12 индекс начала периода, -1 если нет     */
    uint16_t dc_level;        /* 16 измеренная средняя линия               */
    uint16_t v_min;           /* 18                                        */
    uint16_t v_max;           /* 20                                        */
    uint16_t v_pp;            /* 22 размах                                 */
    float    rms;             /* 24 СКЗ                                    */
    uint32_t crossings;       /* 28 восходящих переходов через dc_level    */
    /* --- хедер 32 байта --- */
    uint16_t samples[IPC_FRAME_MAX_SAMPLES]; /* сырьё, uint16 LE           */
} ipc_frame_t;

/* ---- SPSC-кольцо: CM4 пишет (head), CM7 читает (tail) ------------------- */
typedef struct {
    volatile uint32_t head;   /* двигает ТОЛЬКО производитель CM4          */
    volatile uint32_t tail;   /* двигает ТОЛЬКО потребитель CM7            */
    volatile uint32_t drops;  /* счётчик дропов, пишет ТОЛЬКО CM4          */
    uint32_t _pad;            /* до 16 Б; струк ниже выровнен на 32        */
    ipc_frame_t frames[IPC_RING_FRAMES];
} ipc_ring_t;

/* ---- мейлбокс команд CM7 -> CM4, ack обратно ---------------------------- */
typedef struct {
    volatile uint32_t cmd_seq;   /* CM7 инкрементит при новой команде      */
    uint32_t cmd_id;             /* IPC_CMD_*                              */
    uint32_t p0, p1, p2, p3;     /* параметры                              */
    volatile uint32_t ack_seq;   /* CM4: ack_seq = cmd_seq, когда исполнил */
    uint32_t ack_status;         /* IPC_ACK_*                              */
    uint32_t ack_d0, ack_d1;     /* опциональные данные ответа             */
} ipc_mailbox_t;

/* ---- всё, что лежит в разделяемом регионе ------------------------------- */
typedef struct {
    ipc_mailbox_t mbox;
    ipc_ring_t    ring;
} ipc_shared_t;

/* Экземпляр определяется в ipc.c КАЖДОГО ядра с привязкой к секции:
 *   __attribute__((section(".ipc_shared"), aligned(32)))
 *   volatile ipc_shared_t g_ipc;
 * Оба .ld кладут .ipc_shared по одному адресу в SRAM4.            */
extern volatile ipc_shared_t g_ipc;

/* ---- помощники кольца (одинаковый код на обоих ядрах) ------------------- */
/* Корректны при non-cacheable shared-регионе. Если позже сделаешь payload
 * кэшируемым — добавь invalidate перед чтением сэмплов на CM7 (см. внизу). */

static inline uint32_t ipc_ring_count(const volatile ipc_ring_t* r) {
    return r->head - r->tail;                 /* unsigned wrap корректен    */
}
static inline bool ipc_ring_full(const volatile ipc_ring_t* r) {
    return ipc_ring_count(r) >= IPC_RING_FRAMES;
}
static inline bool ipc_ring_empty(const volatile ipc_ring_t* r) {
    return r->head == r->tail;
}

/* CM4: взять слот под заполнение (NULL = полно, кадр дропаем) */
static inline volatile ipc_frame_t* ipc_ring_acquire(volatile ipc_ring_t* r) {
    if (ipc_ring_full(r)) { r->drops++; return NULL; }
    return &r->frames[r->head & (IPC_RING_FRAMES - 1u)];
}
/* CM4: опубликовать заполненный слот. DMB гарантирует, что данные кадра
 * уже записаны, прежде чем потребитель увидит новый head. */
static inline void ipc_ring_commit(volatile ipc_ring_t* r) {
    //__DMB();
    r->head++;
}

/* CM7: посмотреть старейший кадр (NULL = пусто) */
static inline volatile ipc_frame_t* ipc_ring_peek(volatile ipc_ring_t* r) {
    if (ipc_ring_empty(r)) return NULL;
    //__DMB();                                  /* index прочитан до данных   */
    return &r->frames[r->tail & (IPC_RING_FRAMES - 1u)];
}
/* CM7: освободить слот после обработки */
static inline void ipc_ring_release(volatile ipc_ring_t* r) {
    //__DMB();
    r->tail++;
}

/* ---- статические проверки раскладки (ловят дрейф между сборками) -------- */
_Static_assert(sizeof(ipc_frame_t) == 32u + 2u * IPC_FRAME_MAX_SAMPLES,
               "ipc_frame_t: неожиданный размер — проверь паддинг");
_Static_assert(offsetof(ipc_frame_t, samples) == 32u,
               "ipc_frame_t: хедер должен быть ровно 32 байта");
_Static_assert((IPC_RING_FRAMES & (IPC_RING_FRAMES - 1u)) == 0u,
               "IPC_RING_FRAMES должно быть степенью двойки");
_Static_assert((sizeof(ipc_frame_t) % 32u) == 0u,
               "размер кадра должен быть кратен 32 (под кэш-линию)");
