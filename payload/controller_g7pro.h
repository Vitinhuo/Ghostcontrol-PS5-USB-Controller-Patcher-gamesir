/* SPDX-License-Identifier: GPL-3.0-or-later
 * controller_g7pro.h -- GameSir G7 Pro (USB HID) para o Ghost-Control
 *
 * Header-only (tudo static): incluir em gc_main.c DEPOIS da definicao de
 * gp_log() e de gc_types.h. Nao precisa mexer no Makefile.
 *
 * STATUS: o layout do relatorio HID USB do G7 Pro NAO esta verificado.
 *   - G7_CAPTURE = 1 (padrao): so registra no log o que muda a cada botao.
 *                              Nada e injetado no PS5.
 *   - G7_CAPTURE = 0         : usa o parser, que depende da tabela abaixo
 *                              preenchida com os dados da captura.
 *
 * Fatos conhecidos (um unico dmesg de Linux, nao confirmado no PS5):
 *   VID 0x3537 (GameSir), PID 0x1022 "GameSir-G7 Pro"
 *   interface 0 = HID Gamepad, interface 1 = HID Keyboard + Mouse
 */
#ifndef CONTROLLER_G7PRO_H
#define CONTROLLER_G7PRO_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#define VID_GAMESIR   0x3537u
#define PID_G7PRO     0x1022u   /* CONFIRMAR no log: "scan: ... VID=.. PID=.." */

#ifndef G7_CAPTURE
#define G7_CAPTURE    1
#endif

/* Reconhece o G7 Pro. Em modo captura aceita QUALQUER PID da GameSir, para
 * descobrir o PID real sem precisar recompilar; depois da captura o PID
 * exato passa a ser exigido. */
static inline int g7pro_is_supported_vidpid(uint16_t vid, uint16_t pid) {
    if (vid != VID_GAMESIR) return 0;
#if G7_CAPTURE
    (void)pid;
    return 1;
#else
    return pid == PID_G7PRO;
#endif
}

/* ======================================================================
 * Captura: compara cada pacote com o de repouso e loga so o que mudou.
 *
 * Uso: com o controle conectado, NAO toque em nada nos primeiros 3 s
 * (linha "G7 baseline ready"). Depois aperte UM botao por vez, solte,
 * e mova cada stick e gatilho ate o fim do curso.
 * Bytes que variam sozinhos em repouso (contadores) sao ignorados.
 * ====================================================================== */
#define G7_BASELINE_MS  3000
#define G7_MAX_LEN      64
#define G7_DIFF_MAX     24      /* limite de bytes por linha (LOG_MAX = 480) */

static void g7_capture_packet(const uint8_t *b, uint32_t len) {
    static uint8_t  idle[G7_MAX_LEN], last[G7_MAX_LEN];
    static uint8_t  noisy[G7_MAX_LEN];
    static uint32_t idle_len = 0;
    static int      state = 0;                 /* 0 = nenhum, 1 = baseline, 2 = pronto */
    static struct timeval t0;
    static uint32_t npk = 0;

    if (len > G7_MAX_LEN) len = G7_MAX_LEN;
    npk++;

    if (state == 0) {
        memcpy(idle, b, len); memcpy(last, b, len);
        memset(noisy, 0, sizeof(noisy));
        idle_len = len;
        gettimeofday(&t0, NULL);
        state = 1;
        gp_log("G7 first packet len=%u (mantenha as maos longe por 3s)\n", len);
        return;
    }

    if (state == 1) {
        struct timeval now; gettimeofday(&now, NULL);
        long ms = (now.tv_sec - t0.tv_sec) * 1000L + (now.tv_usec - t0.tv_usec) / 1000L;
        uint32_t n = len < idle_len ? len : idle_len;
        for (uint32_t i = 0; i < n; i++)
            if (b[i] != idle[i]) noisy[i] = 1;
        if (ms < G7_BASELINE_MS) return;

        char hex[3 * G7_MAX_LEN + 1]; int p = 0;
        for (uint32_t i = 0; i < idle_len; i++)
            p += snprintf(hex + p, sizeof(hex) - p, "%02x ", idle[i]);
        gp_log("G7 idle len=%u packets=%u: %s\n", idle_len, npk, hex);

        char nz[160]; int q = 0;
        for (uint32_t i = 0; i < idle_len && q < (int)sizeof(nz) - 8; i++)
            if (noisy[i]) q += snprintf(nz + q, sizeof(nz) - q, "[%u]", i);
        gp_log("G7 baseline ready. bytes ruidosos ignorados: %s\n", q ? nz : "nenhum");
        state = 2;
        return;
    }

    if (len != idle_len) {
        gp_log("G7 packet len=%u (repouso=%u)\n", len, idle_len);
        if (len > idle_len) len = idle_len;
    }
    if (memcmp(last, b, len) == 0) return;     /* nada mudou */
    memcpy(last, b, len);

    char line[G7_DIFF_MAX * 16 + 8]; int p = 0, shown = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (noisy[i] || b[i] == idle[i]) continue;
        if (shown++ >= G7_DIFF_MAX) break;
        p += snprintf(line + p, sizeof(line) - p, " [%u]=%02x^%02x", i, b[i], b[i] ^ idle[i]);
    }
    if (shown) gp_log("G7 diff:%s\n", line);
    else       gp_log("G7 de volta ao repouso\n");
}

/* ======================================================================
 * Parser (so compilado com G7_CAPTURE = 0).
 * Preencha com os dados da captura. -1 = ainda nao mapeado.
 * ====================================================================== */
#if !G7_CAPTURE

/* 1 se o byte 0 for um report ID (no Bluetooth e 0x07), senao 0. */
#define G7_RID_OFF     0

/* D-pad como hat switch (0..7, valor > 7 = neutro). */
#define G7_HAT_BYTE   (-1)
#define G7_HAT_SHIFT   0            /* 0 = nibble baixo, 4 = nibble alto */

/* Sticks: offset do byte (ou do byte baixo, se int16 little-endian). */
#define G7_LX_OFF     (-1)
#define G7_LY_OFF     (-1)
#define G7_RX_OFF     (-1)
#define G7_RY_OFF     (-1)
#define G7_STICK_S16   0            /* 1 = int16 LE; 0 = uint8 */
#define G7_INVERT_Y    0            /* 1 = inverte o eixo Y dos dois sticks */

/* Gatilhos analogicos (uint8). */
#define G7_LT_OFF     (-1)
#define G7_RT_OFF     (-1)

typedef struct { int8_t byte; uint8_t mask; uint32_t sce; } g7_btn_t;

static const g7_btn_t g7_btns[] = {
    /* byte, mask, botao PS5 -- preencher */
    { -1, 0x00, SCE_PAD_BUTTON_CROSS     },  /* A  */
    { -1, 0x00, SCE_PAD_BUTTON_CIRCLE    },  /* B  */
    { -1, 0x00, SCE_PAD_BUTTON_SQUARE    },  /* X  */
    { -1, 0x00, SCE_PAD_BUTTON_TRIANGLE  },  /* Y  */
    { -1, 0x00, SCE_PAD_BUTTON_L1        },  /* LB */
    { -1, 0x00, SCE_PAD_BUTTON_R1        },  /* RB */
    { -1, 0x00, SCE_PAD_BUTTON_L3        },
    { -1, 0x00, SCE_PAD_BUTTON_R3        },
    { -1, 0x00, SCE_PAD_BUTTON_OPTIONS   },  /* Menu */
    /* Se L2/R2 vierem como bit (alem do analogico), mapear aqui: */
    { -1, 0x00, SCE_PAD_BUTTON_L2        },
    { -1, 0x00, SCE_PAD_BUTTON_R2        },
    /* View/Home: confira no gc_types.h os valores de CREATE e TOUCH_PAD.
     * O guia lista CREATE e PS com o mesmo valor (0x00010000): conflito. */
    { -1, 0x00, SCE_PAD_BUTTON_TOUCH_PAD },
};

static inline uint8_t g7_s16_to_u8(int16_t v) { return (uint8_t)((v + 32768) >> 8); }

static uint8_t g7_axis(const uint8_t *p, int off, int is_s16) {
    uint8_t v;
    if (off < 0) return 128;                      /* nao mapeado = centro */
    if (is_s16) v = g7_s16_to_u8((int16_t)(p[off] | (p[off + 1] << 8)));
    else        v = p[off];
    return v;
}

static int g7_parse(const uint8_t *buf, uint32_t len, ScePadData *o) {
    const uint8_t *p = buf + G7_RID_OFF;
    uint32_t btn = 0;
    if (len <= (uint32_t)G7_RID_OFF) return 0;

    for (unsigned i = 0; i < sizeof(g7_btns) / sizeof(g7_btns[0]); i++)
        if (g7_btns[i].byte >= 0 && g7_btns[i].mask &&
            (p[g7_btns[i].byte] & g7_btns[i].mask))
            btn |= g7_btns[i].sce;

#if G7_HAT_BYTE >= 0
    {
        uint8_t hat = (p[G7_HAT_BYTE] >> G7_HAT_SHIFT) & 0x0F;
        /* 0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW */
        static const uint32_t m[8] = {
            SCE_PAD_BUTTON_UP,
            SCE_PAD_BUTTON_UP   | SCE_PAD_BUTTON_RIGHT,
            SCE_PAD_BUTTON_RIGHT,
            SCE_PAD_BUTTON_DOWN | SCE_PAD_BUTTON_RIGHT,
            SCE_PAD_BUTTON_DOWN,
            SCE_PAD_BUTTON_DOWN | SCE_PAD_BUTTON_LEFT,
            SCE_PAD_BUTTON_LEFT,
            SCE_PAD_BUTTON_UP   | SCE_PAD_BUTTON_LEFT,
        };
        if (hat < 8) btn |= m[hat];
    }
#endif

    o->leftStick.x  = g7_axis(p, G7_LX_OFF, G7_STICK_S16);
    o->leftStick.y  = g7_axis(p, G7_LY_OFF, G7_STICK_S16);
    o->rightStick.x = g7_axis(p, G7_RX_OFF, G7_STICK_S16);
    o->rightStick.y = g7_axis(p, G7_RY_OFF, G7_STICK_S16);
#if G7_INVERT_Y
    o->leftStick.y  = (uint8_t)(255u - o->leftStick.y);
    o->rightStick.y = (uint8_t)(255u - o->rightStick.y);
#endif

    o->analogButtons.l2 = (G7_LT_OFF >= 0) ? p[G7_LT_OFF] : 0;
    o->analogButtons.r2 = (G7_RT_OFF >= 0) ? p[G7_RT_OFF] : 0;
    if (o->analogButtons.l2 > 128) btn |= SCE_PAD_BUTTON_L2;
    if (o->analogButtons.r2 > 128) btn |= SCE_PAD_BUTTON_R2;

    o->buttons   = btn;
    o->connected = 1;
    o->quat.w    = 1.0f;
    return 1;
}
#endif /* !G7_CAPTURE */

/* Ponto de entrada chamado pelo loop de leitura. Retorna > 0 se o pacote
 * deve ser injetado no PS5. Em modo captura nunca injeta. */
static int g7pro_handle_packet(int fd, struct usb_fs_endpoint *eps,
                               const uint8_t *buf, uint32_t len, ScePadData *pad) {
    (void)fd; (void)eps;
#if G7_CAPTURE
    (void)pad;
    g7_capture_packet(buf, len);
    return 0;
#else
    return g7_parse(buf, len, pad);
#endif
}

#endif /* CONTROLLER_G7PRO_H */
