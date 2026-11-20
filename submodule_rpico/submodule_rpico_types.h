#ifndef _SUBMODULE_RPICO_TYPES_h
#define _SUBMODULE_RPICO_TYPES_h

// ============================================================
// submodule_rpico_types.h — типы, define'ы и константы RP2040.
// Реализация: submodule_rpico.cpp (шаблонный блок),
// submodule_rpico_engine.cpp (поиск чипа, статусы),
// prog_rpico.cpp (автомат прошивки), rp2040_swd.cpp (низкий уровень).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#define RPICO_CFG_JSON        "/rpico_cfg.json"

// RP2040
#define RP2040_DPIDR          0x0BC12477UL
#define RP2040_TARGETSEL      0x01002927UL   // SWDv2 multi-drop: core 0
#define RP2040_FLASH_BASE     0x10000000UL
#define RP2040_FLASH_SIZE     0x00200000UL   // 2 МБ (Raspberry Pi Pico)
#define RP2040_SECTOR_SIZE    4096UL         // стирание: 4 КБ сектор
#define RP2040_PAGE_SIZE      256UL          // программирование: 256 Б страница

// RST (RUN) цели: -1 = не используется, иначе GPIO ESP32
#ifndef RPICO_RST_PIN
#define RPICO_RST_PIN         (-1)
#endif

// ROM-функции bootrom (rom_table_lookup codes)
#define RP2040_ROM_CODE_IF    0x4649UL       // 'I','F' connect_internal_flash
#define RP2040_ROM_CODE_EX    0x5845UL       // 'E','X' flash_exit_xip
#define RP2040_ROM_CODE_RE    0x4552UL       // 'R','E' flash_range_erase
#define RP2040_ROM_CODE_RP    0x5052UL       // 'R','P' flash_range_program
#define RP2040_ROM_CODE_FC    0x4346UL       // 'F','C' flash_flush_cache
#define RP2040_ROM_CODE_CX    0x5843UL       // 'C','X' flash_enter_cmd_xip
#define RP2040_ROM_FUNC_COUNT 6

// Смещения в ROM для поиска таблицы функций и rom_table_lookup
#define RP2040_ROM_FUNC_TABLE_PTR   0x14UL
#define RP2040_ROM_LOOKUP_FN_PTR    0x18UL

// Раскладка SRAM для блоба (должна совпадать с tools/rp2040_algo/rp2040_algo.s)
#define RPICO_SRAM_BLOB       0x20000200UL   // блоб, <= 512 байт
#define RPICO_SRAM_PARAMS     0x20000400UL   // параметры блоба
#define RPICO_SRAM_LPARAMS    0x20000480UL   // параметры lookup
#define RPICO_SRAM_DATA       0x20001000UL   // буфер данных (4 КБ)
#define RPICO_SRAM_DATA_SIZE  4096UL
#define RPICO_SRAM_STACK      0x20040000UL   // вершина стека (растёт вниз)

// Слова params основного блоба
#define RP_PARAM_FN_IF        0
#define RP_PARAM_FN_EX        1
#define RP_PARAM_FN_RE        2
#define RP_PARAM_FN_RP        3
#define RP_PARAM_FN_FC        4
#define RP_PARAM_FN_CX        5
#define RP_PARAM_OFFSET       6
#define RP_PARAM_LEN          7
#define RP_PARAM_DATA         8
#define RP_PARAM_STATUS       9
#define RP_PARAM_PAGES_DONE   10
#define RP_PARAM_WORDS        11

// Слова lookup params
#define RPL_PARAM_TABLE       0
#define RPL_PARAM_LOOKUP      1
#define RPL_PARAM_CODES       2
#define RPL_PARAM_RESULTS     8
#define RPL_PARAM_STATUS      14
#define RPL_PARAM_WORDS       15

// Кооперативная прошивка
#define RPICO_CHUNK_SIZE      4096UL         // один сектор за шаг
#define RPICO_DATA_SUBSTEP    512UL          // порция передачи данных в SRAM

// Конечный автомат прошивки
enum RpFlashState {
	RP_FLASH_IDLE = 0,
	RP_FLASH_INIT,
	RP_FLASH_WRITE,
	RP_FLASH_VERIFY,
	RP_FLASH_DONE
};

// Конфигурация чипа (из /rpico_cfg.json)
typedef struct {
	uint32_t idcode;
	String   name;
	String   family;
	uint32_t flash_size;
	uint32_t flash_start;
	uint32_t sector_size;
	uint32_t page_size;
} RpChipConfig_t;

#endif // _SUBMODULE_RPICO_TYPES_h
