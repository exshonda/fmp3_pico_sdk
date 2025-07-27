/*
 * シリアルインタフェースドライバのターゲット依存部（非TECS版専用）
 *
 * $Id: target_serial.h 289 2021-08-05 14:44:10Z ertl-komori $
 */

#include <stdint.h>
#include <stdio.h>
#include "t_stddef.h"
#include "target_serial.h"
#include "target_syssvc.h"
#include "pico/stdio.h"

struct sio_port_control_block
{
    void (*handle)(void*);         /* 割込みハンドラ */
    intptr_t exinf;
    bool_t rdy_snd;                 /* 送信可能コールバック */
    uint8_t snd_buf[256];           /* 送信バッファ */
    uint32_t snd_wpos;              /* 送信バッファ書き込み位置 */
    uint32_t snd_rpos;              /* 送信バッファ読み込み位置 */
    bool_t rdy_rcv;                 /* 受信通知コールバック */
    uint8_t rcv_buf[256];           /* 受信バッファ */
    uint32_t rcv_wpos;              /* 受信バッファ書き込み位置 */
    uint32_t rcv_rpos;              /* 受信バッファ読み込み位置 */
};

/*
 *  SIOポート管理ブロックのエリア
 */
static SIOPCB siopcb_table[TNUM_PORT] = {
    {NULL, 0, false, {0}, 0, 0, false, {0}, 0, 0},
};

/*
 *  SIOポートIDから管理ブロックを取り出すためのマクロ
 */
#define INDEX_SIOP(siopid)	((uint_t)((siopid) - 1))
#define get_siopcb(siopid)	(&(siopcb_table[INDEX_SIOP(siopid)]))

/*
 * SIOドライバの初期化
 */
void sio_initialize(intptr_t exinf)
{
    for (uint_t i = 0; i < TNUM_PORT; i++)
    {
        siopcb_table[i].exinf = exinf;
        siopcb_table[i].handle = NULL;
    }
}

/*
 * SIOドライバの終了処理
 */
void sio_terminate(intptr_t exinf)
{
	uint_t	i;
	SIOPCB	*p_siopcb;

	for (i = 0; i < TNUM_PORT; i++) {
		p_siopcb = &(siopcb_table[i]);
		if (p_siopcb->handle) {
			/*
			 *  オープンされているSIOポートのクローズ
			 */
			sio_cls_por(&(siopcb_table[i]));
		}
	}
}

/*
 * SIOポートのオープン
 */
SIOPCB *sio_opn_por(ID siopid, intptr_t exinf)
{
    SIOPCB *p_siopcb = NULL;
    if (siopid > TNUM_PORT) {
        return p_siopcb;
    }

    p_siopcb = get_siopcb(siopid);
    /* すでにオープンされている場合はNULLを返す */
    if (p_siopcb->handle != NULL) {
        return NULL;
    }

    p_siopcb->exinf = exinf;
    p_siopcb->handle = sio_handler;
    p_siopcb->rcv_wpos = 0;
    p_siopcb->rcv_rpos = 0;

    stdio_set_chars_available_callback(p_siopcb->handle, p_siopcb); // 標準入出力のコールバックを設定

    return p_siopcb;
}

/*
 * SIOポートのクローズ
 */
void sio_cls_por(SIOPCB *p_siopcb)
{
    stdio_set_chars_available_callback(NULL, NULL); // 標準入出力のコールバックを設定

    p_siopcb->handle = NULL;
}

/*
 * SIOポートへの文字送信
 */
bool_t sio_snd_chr(SIOPCB *p_siopcb, char ch)
{
    p_siopcb->snd_buf[p_siopcb->snd_wpos] = ch; // 送信バッファに文字を格納
    if (putchar_raw(p_siopcb->snd_buf[p_siopcb->snd_wpos]) == 1) {
        p_siopcb->snd_wpos = (p_siopcb->snd_wpos + 1) % sizeof(p_siopcb->snd_buf); // 書き込み位置を更新
        return true; // 送信成功
    }
    return false; // 送信失敗
}

/*
 * SIOポートからの文字受信
 */
int_t sio_rcv_chr(SIOPCB *p_siopcb)
{
    uint8_t ch;
    if (p_siopcb->rcv_wpos != p_siopcb->rcv_rpos) // 受信バッファにデータがある場合
    {
        ch = p_siopcb->rcv_buf[p_siopcb->rcv_rpos];
        p_siopcb->rcv_rpos = (p_siopcb->rcv_rpos + 1) % sizeof(p_siopcb->rcv_buf); // 読み込み位置を更新
        return ch;
    }
    return -1; // 受信失敗
}

/*
 * SIOポートからのコールバックの許可
 */
void sio_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
    switch (cbrtn) {
    case SIO_RDY_SND:
        p_siopcb->rdy_snd = true;
        break;
    case SIO_RDY_RCV:
        p_siopcb->rdy_rcv = true;
        break;
    default:
        break;
    }
}

/*
 * SIOポートからのコールバックの禁止
 */
void sio_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
    switch (cbrtn) {
    case SIO_RDY_SND:
        p_siopcb->rdy_snd = false;
        break;
    case SIO_RDY_RCV:
        p_siopcb->rdy_rcv = false;
        break;
    default:
        break;
    }
}

/*
 * SIOポートへの文字出力
 */
void target_fput_log(char c)
{
    if (c == '\n') {
        putc('\r', stdout);
    }
    putc(c, stdout);
}

void sio_handler(void *ptr)
{
    SIOPCB *p_siopcb = NULL;
    for (uint_t i = 0; i < TNUM_PORT; i++) {
        if (siopcb_table[i].handle == sio_handler) {
            p_siopcb = &siopcb_table[i];
            break;
        }
    }

    /*
     *  該当するポートが見つからない場合は何もしない
     */
    if (p_siopcb == NULL) {
        return;
    }

    /*
     *  受信データをバッファに格納する．
     */
    p_siopcb->rcv_wpos = (p_siopcb->rcv_wpos + 1) % sizeof(p_siopcb->rcv_buf);

    /*
     *  受信割込み開始
     */
    p_siopcb->rcv_buf[p_siopcb->rcv_wpos] = stdio_getchar_timeout_us(0);

    /*
     *  受信通知コールバックルーチンを呼び出す．
     */
    if (p_siopcb->rdy_rcv) {
        sio_irdy_rcv(p_siopcb->exinf);
    }
}