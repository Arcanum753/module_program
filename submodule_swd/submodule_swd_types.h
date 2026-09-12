#ifndef _SUBMODULE_SWD_TYPES_h
#define _SUBMODULE_SWD_TYPES_h

// ============================================================
// submodule_swd_types.h — типы, структуры и define'ы SWD-субмодуля.
// Реализация: submodule_swd.cpp (шаблон) и
// submodule_swd_engine.cpp (поиск чипа, статус, программирование).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#define SWD_CFG_JSON      "/swd_cfg.json"

#define DEFAULT_FLASH_START_ADDR    0x08000000
#define DEFAULT_PAGE_SIZE           1024
#define DEFAULT_WORD_SIZE           2
#define DEFAULT_CSW_VALUE           0xa2000002

typedef struct {
    uint32_t idcode;
    String   name;
    String   family;
    uint32_t flash_size;
    uint32_t flash_start;
    uint32_t page_size;
    uint32_t word_size;
    uint32_t csw_value;
} ChipConfig_t;

#endif // _SUBMODULE_SWD_TYPES_h
