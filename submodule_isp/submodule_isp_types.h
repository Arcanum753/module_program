#ifndef _SUBMODULE_ISP_TYPES_h
#define _SUBMODULE_ISP_TYPES_h

// ============================================================
// submodule_isp_types.h — типы, структуры и define'ы ISP-субмодуля.
// Реализация: submodule_isp.cpp (шаблон) и
// submodule_isp_engine.cpp (поиск чипа, статус).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#define AVRISP_CFG_JSON   "/avrisp_cfg.json"

typedef struct {
    String   signature;
    String   name;
    uint32_t flash_size;
    uint32_t page_size;
} ChipConfigAvr_t;

#endif // _SUBMODULE_ISP_TYPES_h
