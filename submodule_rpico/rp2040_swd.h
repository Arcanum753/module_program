#ifndef _RP2040_SWD_H
#define _RP2040_SWD_H

// ============================================================
// rp2040_swd.h — низкий уровень RP2040 по SWD.
//
// Подключение DP, power-up, halt/resume, доступ к регистрам ядра
// (DCRSR/DCRDR), запись/чтение памяти через AHB-AP, резолв
// ROM-функций bootrom и запуск Thumb-блоба в SRAM.
// ============================================================

#include <Arduino.h>
#include "submodule_rpico_types.h"

// Однократное чтение DPIDR (без halt); 0 — чип не отвечает.
uint32_t rp2040_read_dpidr();

// Подключение: line reset + power-up DP. Возвращает DPIDR (0 — нет связи).
uint32_t rp2040_connect();

bool rp2040_is_halted();
bool rp2040_halt();
bool rp2040_resume();
bool rp2040_disable_irqs();

// Чтение/запись слова памяти через AHB-AP.
bool rp2040_read_word(uint32_t addr, uint32_t &value);
bool rp2040_write_word(uint32_t addr, uint32_t value);

// Блочная запись байтов в память (адрес и длина должны быть кратны 4).
bool rp2040_write_block(uint32_t addr, const uint8_t *data, uint32_t len);

// Загрузить блоб в SRAM и разрешить ROM-функции.
bool rp2040_load_algo();
bool rp2040_resolve_rom(uint32_t fns[RP2040_ROM_FUNC_COUNT]);

// Одна операция стирания/программирования через блоб.
bool rp2040_erase(uint32_t flash_offset, uint32_t len);
bool rp2040_program(uint32_t flash_offset, uint32_t len, uint32_t sram_data_addr);

// Чтение flash через XIP (адрес абсолютный, например 0x10000000 + offset).
bool rp2040_read_flash(uint32_t abs_addr, uint8_t *buf, uint32_t len);

// Программный сброс цели (AIRCR.SYSRESETREQ, fallback — watchdog).
void rp2040_reset_target();

#endif // _RP2040_SWD_H
