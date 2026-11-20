#ifndef _RPICO_DEBUG_CM_H
#define _RPICO_DEBUG_CM_H

// ============================================================
// rp2040_debug_cm.h — константы ARM DAP / Cortex-M0+ для RP2040.
// Копия подмножества submodule_swd/debug_cm.h (отдельный guard,
// чтобы заголовки не конфликтовали при совместной сборке).
// ============================================================

// Debug Port (DP) регистры
#define DP_IDCODE        0x00U
#define DP_ABORT         0x00U
#define DP_CTRLSTAT      0x04U
#define DP_SELECT        0x08U
#define DP_RDBUFF        0x0CU

// Access Port (AP) регистры (AHB-AP)
#define AP_CSW           0x00U
#define AP_TAR           0x04U
#define AP_DRW           0x0CU
#define AP_IDR           0xFCU

// AP Control and Status Word
#define CSW_SIZE         0x00000007U
#define CSW_SIZE8        0x00000000U
#define CSW_SIZE16       0x00000001U
#define CSW_SIZE32       0x00000002U
#define CSW_ADDRINC      0x00000030U
#define CSW_NADDRINC     0x00000000U
#define CSW_SADDRINC     0x00000010U
#define CSW_HPROT        0x02000000U
#define CSW_MSTRDBG      0x20000000U
#define CSW_RESERVED     0x01000000U

// RP2040 AHB-AP: 32-bit, single auto-increment, debug master
#define RP2040_AP_CSW    (CSW_RESERVED | CSW_MSTRDBG | CSW_HPROT | CSW_SADDRINC | CSW_SIZE32)

// Core debug регистры (Cortex-M0+)
#define DBG_HCSR         0xE000EDF0U
#define DBG_CRSR         0xE000EDF4U
#define DBG_CRDR         0xE000EDF8U
#define DBG_EMCR         0xE000EDFCU
#define NVIC_ICER0       0xE000E180U
#define NVIC_AIRCR       0xE000ED0CU
#define NVIC_DFSR        0xE000ED30U

// Debug Halting Control and Status Register
#define DBGKEY           0xA05F0000U
#define C_DEBUGEN        0x00000001U
#define C_HALT           0x00000002U
#define C_STEP           0x00000004U
#define S_REGRDY         0x00010000U
#define S_HALT           0x00020000U
#define S_SLEEP          0x00040000U
#define S_LOCKUP         0x00080000U

// DCRSR: REGWnR + REGSEL
#define DCRSR_REGWnR     0x00010000U
#define DCRSR_REGSEL_SP  13U
#define DCRSR_REGSEL_LR  14U
#define DCRSR_REGSEL_PC  15U

// AIRCR
#define VECTKEY          0x05FA0000U
#define SYSRESETREQ      0x00000004U

// RP2040 watchdog (fallback-сброс)
#define RP2040_WATCHDOG_CTRL  0x40058004U
#define RP2040_WD_TRIGGER     0x80000000U

#endif // _RPICO_DEBUG_CM_H
