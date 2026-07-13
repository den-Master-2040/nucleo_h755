/*
 * web.c
 *
 *  Created on: Jun 18, 2026
 *      Author: User
 */


#include "mongoose.h"
#include "ipc.h"
#include "web_content.h"     /* scope_html[], scope_html_len */
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include "fsk.h"
#define CHECK_NBITS   50            /* = TX_NBITS на CM4 */
#define CHECK_WINDOW  1000          /* бит на одно измерение BER */

static const uint8_t tx_pattern[CHECK_NBITS] = {
    1,0,1,1,0,0,1,0, 1,1,1,0,0,0,1,0, 0,1,1,0,1,0,0,1,
    1,0,0,0,1,1,0,1, 0,1,0,0,0,1,1,1, 0,0,1,0,1,1,0,1, 1,0
};  /* байт-в-байт тот же, что на CM4! */

typedef struct {
    volatile uint32_t windows;      /* сколько окон измерено           */
    volatile float    ber;          /* BER последнего окна             */
    volatile float    ebno_db;      /* оценка канала демодулятором     */
    volatile float    f1_est, f2_est;
    volatile int      shift;        /* найденный циклический сдвиг     */
    volatile int      inverted;
    volatile uint32_t ring_level;   /* заполненность кольца, для глаза */
} modem_check_t;
volatile modem_check_t g_chk;       /* смотреть в Live Expressions     */

static struct FSK *dem;
static COMP    dem_in[4000 + 256];
static uint8_t bit_acc[CHECK_WINDOW + 64];
static float   dc = 2048.0f;
static struct mg_mgr mgr;

/* --- отправка команды на CM4 через мейлбокс (CM7-сторона) --- */
static void ipc_send_cmd(uint32_t id, uint32_t p0, uint32_t p1) {
    g_ipc.mbox.cmd_id = id;
    g_ipc.mbox.p0 = p0;
    g_ipc.mbox.p1 = p1;
    __DMB();
    g_ipc.mbox.cmd_seq++;
    __DSB();
    HAL_HSEM_FastTake(IPC_HSEM_CMD);
    HAL_HSEM_Release(IPC_HSEM_CMD, 0);
}

/* --- разбор JSON-команды от кнопок фронта --- */
static void handle_command(struct mg_str j) {
    char *cmd = mg_json_get_str(j, "$.cmd");
    if (!cmd) return;
    if      (!strcmp(cmd, "stream_on"))   ipc_send_cmd(IPC_CMD_STREAM_ON, 0, 0);
    else if (!strcmp(cmd, "stream_off"))  ipc_send_cmd(IPC_CMD_STREAM_OFF, 0, 0);
    else if (!strcmp(cmd, "set_points"))  ipc_send_cmd(IPC_CMD_SET_POINTS,   mg_json_get_long(j, "$.n",  512), 0);
    else if (!strcmp(cmd, "set_rate"))    ipc_send_cmd(IPC_CMD_SET_ADC_RATE, mg_json_get_long(j, "$.hz", 100000), 0);
    else if (!strcmp(cmd, "set_dac"))     ipc_send_cmd(IPC_CMD_SET_DAC_FREQ, mg_json_get_long(j, "$.hz", 1000), 0);
    else if (!strcmp(cmd, "set_pwm"))     ipc_send_cmd(IPC_CMD_SET_PWM_DUTY,
                                              mg_json_get_long(j, "$.hz", 20000),
                                              mg_json_get_long(j, "$.duty", 50));
    else if (!strcmp(cmd, "reset_state")) ipc_send_cmd(IPC_CMD_RESET_STATE, 0, 0);
    /* set_trig / reboot аналогично */
    mg_free(cmd);
}

/* --- HTTP + WebSocket события --- */
static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        if (mg_match(hm->uri, mg_str("/scope"), NULL)) {

            mg_ws_upgrade(c, hm, NULL);          /* апгрейд в WebSocket */
        } else {


            //mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%.*s", (int)scope_html_len, scope_html);
            mg_printf(c, "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/html\r\n"
                         "Content-Length: %u\r\n"
                         "\r\n", (unsigned) scope_html_len);
            mg_send(c, scope_html, scope_html_len);   /* сырые байты, без форматирования */
        }
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        handle_command(wm->data);                /* JSON-команда */
    }
}

/* --- слить кадры из IPC-кольца во все WS-соединения --- */
static void pump_frames(void) {

    volatile ipc_frame_t *f;
    while ((f = ipc_ring_peek(&g_ipc.ring)) != NULL) {
        size_t wire = 32 + 2 * f->n_samples;     /* хедер + n точек */
        for (struct mg_connection *c = mgr.conns; c; c = c->next) {
            if (!c->is_websocket) continue;
            if (c->send.len > 16 * 1024) continue;   /* backpressure: дроп */
            mg_ws_send(c, (const void *) f, wire, WEBSOCKET_OP_BINARY);
        }

        ipc_ring_release(&g_ipc.ring);
    }
}

/* --- задача сервера --- */
static void web_task(void *arg) {
    (void) arg;

    mg_mgr_init(&mgr);
    struct mg_connection *l = mg_http_listen(&mgr, "http://0.0.0.0:80", ev_handler, NULL);
    if(l == NULL)__BKPT(0);
    for (;;) {
        mg_mgr_poll(&mgr, 5);    /* 5 мс → плавный поток, низкая латентность */

        pump_frames();

    }
}

void web_server_start(void) {

	BaseType_t ok = xTaskCreate(web_task, "web", 4096, NULL, tskIDLE_PRIORITY + 3, NULL);
		if (ok != pdPASS) {
			__BKPT(0);
		}
}


void modem_task(void *arg)
{
    struct FSK *dem = fsk_create(8000, 100, 2, 1200, 1400 - 1200 == 200 ? 200 : 200);
    /* ^ параметры те же, что на десктопе, разумеется просто fsk_create(8000,100,2,1200,200) */
    COMP dem_in[4000 + 2*80 + 64];
    uint8_t rx_bits[64];

    for (;;) {
        uint32_t nin = fsk_nin(dem);

        while ((&g_ipc.ring.wr - &g_ipc.ring.rd) < nin)
            vTaskDelay(pdMS_TO_TICKS(10));

        /* если .ipc_shared у тебя кэшируемая — инвалидация перед чтением */
        /* SCB_InvalidateDCache_by_Addr(...) по затронутому диапазону */

        uint32_t rd = g_ipc.ring.rd;
        for (uint32_t i = 0; i < nin; i++) {
            uint16_t raw = g_ipc.ring.buf[(rd + i) & (MODEM_RING_SZ - 1)];
            dem_in[i].real = ((float)raw - 2048.0f) / 2048.0f;  /* DC-срез + масштаб */
            dem_in[i].imag = 0.0f;
        }
        g_ipc.ring.rd = rd + nin;

        fsk_demod(dem, rx_bits, dem_in);
        /* дальше — детектор синхрослова по потоку rx_bits */
    }
}



static void evaluate(const uint8_t *bits, int n)
{
    int best = n + 1, best_s = 0, inv = 0;
    for (int s = 0; s < CHECK_NBITS; s++) {
        int err = 0;
        for (int i = 0; i < n; i++)
            err += bits[i] != tx_pattern[(i + s) % CHECK_NBITS];
        if (err     < best) { best = err;     best_s = s; inv = 0; }
        if (n - err < best) { best = n - err; best_s = s; inv = 1; }
    }
    g_chk.ber      = (float)best / n;
    g_chk.shift    = best_s;
    g_chk.inverted = inv;
    g_chk.windows++;
}

void modem_check_task(void *arg)
{
    dem = fsk_create(8000, 100, 2, 1200, 200);
    if (!dem) { for(;;) vTaskDelay(1000); }        /* heap! */
    fsk_set_freq_est_limits(dem, 900, 1700);

    int acc = 0;
    for (;;) {
        uint32_t nin = fsk_nin(dem);

        while ((g_ipc.ring.wr - g_ipc.ring.rd) < nin)
            vTaskDelay(pdMS_TO_TICKS(20));         /* 8 кГц — некуда спешить */

        uint32_t rd = g_ipc.ring.rd;
        g_chk.ring_level = g_ipc.ring.wr - rd;
        for (uint32_t i = 0; i < nin; i++) {
            float raw = (float)g_ipc.ring.buf[(rd + i) & (MODEM_RING_SZ - 1u)];
            dc += 0.0005f * (raw - dc);            /* бегущая средняя линия */
            dem_in[i].real = (raw - dc) * (1.0f / 2048.0f);
            dem_in[i].imag = 0.0f;
        }
        g_ipc.ring.rd = rd + nin;

        fsk_demod(dem, &bit_acc[acc], dem_in);
        acc += dem->Nbits;

        g_chk.ebno_db = dem->EbNodB;               /* имена полей — по fsk.h */
        g_chk.f1_est  = dem->f_est[0];
        g_chk.f2_est  = dem->f_est[1];

        if (acc >= CHECK_WINDOW) {
            evaluate(bit_acc, acc);
            acc = 0;
        }
    }
}
