#ifndef _RP_SWD_H
#define _RP_SWD_H

// ============================================================
// rp_swd.h — bit-bang SWD PHY для submodule_rpico.
// Копия слоя submodule_swd/swd.cpp с префиксом rp_ (символы не
// конфликтуют, если когда-нибудь собрать swd и rpico вместе).
// ============================================================

#include <Arduino.h>
#include "rp2040_debug_cm.h"

// Задержка полутакта SWD, мкс. 0 = без delayMicroseconds (только nop).
extern uint8_t rp_swdDelay;

// Адаптивный счётчик повторов (5..15)
extern uint8_t rp_swdRetryCount;

// Последний ACK-код SWD (для диагностики: 1=OK, 7=нет ответа)
extern uint8_t rp_swd_last_ack;

// Дополнительный такт turnaround перед чтением ACK (диагностика).
// Стандарт SWD — 1 такт (0 = выключено).
extern uint8_t rp_swd_extra_turnaround;

void rp_swd_set_delay(uint8_t delay);
void rp_swd_reset_retry_count();
void rp_swd_update_retry(bool success);

// Пины SWD (по умолчанию из SWDPIN_CLK/SWDPIN_DATA)
void rp_swd_set_pins(uint8_t clk_pin, uint8_t data_pin);
uint8_t rp_swd_get_clk_pin();
uint8_t rp_swd_get_data_pin();

void rp_swd_gpio_init();
uint32_t rp_swd_init();

// SWDv2 multi-drop: запись TARGETSEL специальной последовательностью
// (без ожидания ACK, с двумя turnaround и 8 idle-тактов). RP2040 — DPv2.
bool rp_swd_targetsel(uint32_t targetsel);

// Последовательности перевода DP в SWD (нужны и для диагностики)
void rp_swd_line_reset_swd();
void rp_swd_dormant_to_swd();

// RST (RUN) цели: активный уровень LOW. При RPICO_RST_PIN < 0 — no-op.
void rp_swd_rst_init();
void rp_swd_rst_assert();
void rp_swd_rst_release();
void rp_swd_rst_pulse(uint32_t low_ms);

// Уровень SWDIO в режиме входа с подтяжкой (диагностика)
int rp_swd_swdio_level();

// Диагностика линий: чтение своих выходов и SWDIO с pull-down/pull-up.
void rp_swd_pin_levels(int &clk_low, int &clk_high, int &data_pull_down, int &data_pull_up,
                       int &clk_pull_down, int &clk_pull_up,
                       int &clk_float_after_high, int &data_float_after_low,
                       int &clk_float_after_low);

bool rp_swd_AP_Write(unsigned addr, uint32_t data);
bool rp_swd_AP_Read(unsigned addr, uint32_t &data);
bool rp_swd_DP_Write(unsigned addr, uint32_t data);
bool rp_swd_DP_Read(unsigned addr, uint32_t &data);

// Однократные операции без ретраев (для неблокирующей проверки чипа)
bool rp_swd_DP_Read_once(unsigned addr, uint32_t &data);
bool rp_swd_AP_Read_once(unsigned addr, uint32_t &data);

bool rp_swd_transfer(unsigned port_address, bool APorDP, bool RorW, uint32_t &data);
bool rp_swd_calculate_parity(uint32_t in_data);
void rp_swd_write(uint32_t data, uint8_t bits);
uint32_t rp_swd_read(uint8_t bits);
void rp_swd_turn(bool WorR);

#endif // _RP_SWD_H
