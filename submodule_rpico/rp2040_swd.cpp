#include "core_web/FSWebServerLib.h"

#include <cstring>
#include <Arduino.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

#include "submodule_rpico.h"
#include "rp2040_swd.h"
#include "rp_swd.h"
#include "rp2040_algo.h"

// ============================================================
// Низкий уровень RP2040 (SWD-DP + AHB-AP + Thumb-блоб в SRAM)
// ============================================================

static bool ap_write_reg(uint8_t reg, uint32_t value) {
	return rp_swd_AP_Write(reg, value);
}

// AP-чтение: AP_Read постит чтение, фактическое значение — в RDBUFF.
static bool ap_read_reg(uint8_t reg, uint32_t &value) {
	uint32_t dummy = 0;
	if (!rp_swd_AP_Read(reg, dummy)) return false;
	return rp_swd_DP_Read(DP_RDBUFF, value);
}

static bool set_core_reg(uint8_t regsel, uint32_t value) {
	if (!rp2040_write_word(DBG_CRDR, value)) return false;
	if (!rp2040_write_word(DBG_CRSR, DCRSR_REGWnR | regsel)) return false;
	for (int i = 0; i < 100; i++) {
		uint32_t dh = 0;
		if (!rp2040_read_word(DBG_HCSR, dh)) return false;
		if (dh & S_REGRDY) return true;
		delay(1);
	}
	return false;
}

// Запуск точки входа блоба: PC/SP + resume, ожидание BKPT (S_HALT).
static bool run_blob(uint32_t entry_ofs, uint32_t timeout_ms) {
	if (!rp2040_halt()) return false;
	if (!set_core_reg(DCRSR_REGSEL_SP, RPICO_SRAM_STACK)) return false;
	if (!set_core_reg(DCRSR_REGSEL_PC, RPICO_SRAM_BLOB + entry_ofs)) return false;
	if (!rp2040_resume()) return false;

	uint32_t t0 = millis();
	for (;;) {
		uint32_t dh = 0;
		if (!rp2040_read_word(DBG_HCSR, dh)) return false;
		if (dh & S_LOCKUP) {
			DEBUGLOGRPICO("run_blob: target locked up (entry +0x%03x)\r\n", entry_ofs);
			rp2040_halt();
			return false;
		}
		if (dh & S_HALT) return true;
		if ((millis() - t0) > timeout_ms) {
			DEBUGLOGRPICO("run_blob: timeout %u ms (entry +0x%03x)\r\n", timeout_ms, entry_ofs);
			rp2040_halt();
			return false;
		}
#if defined(ESP32)
		esp_task_wdt_reset();
#endif
		delay(1);
	}
}

uint32_t rp2040_read_dpidr() {
	uint8_t clk = rp_swd_get_clk_pin();
	uint8_t data = rp_swd_get_data_pin();

	rp_swd_gpio_init();
	uint32_t idcode = rp_swd_init();
	if (idcode == 0) {
		// Частая причина ACK=7 — перепутанные CLK/DATA. Пробуем оба порядка.
		DEBUGLOGRPICO("probe: no answer (CLK=%u DATA=%u, ack=%u), trying swapped\r\n",
			clk, data, rp_swd_last_ack);
		rp_swd_set_pins(data, clk);
		rp_swd_gpio_init();
		idcode = rp_swd_init();
	}
	if (idcode == 0) {
		rp_swd_set_pins(clk, data);
		rp_swd_gpio_init();
		DEBUGLOGRPICO("probe: no answer, pulse RST and retry\r\n");
		rp_swd_rst_pulse(10);
		delay(5);
		idcode = rp_swd_init();
	}
	return idcode;
}

uint32_t rp2040_connect() {
	uint8_t clk = rp_swd_get_clk_pin();
	uint8_t data = rp_swd_get_data_pin();

	rp_swd_gpio_init();
	DEBUGLOGRPICO("connect: CLK=%u DATA=%u, SWDIO idle=%d, RST pin=%d\r\n",
		clk, data, rp_swd_swdio_level(), (int)RPICO_RST_PIN);

	// Сброс перед подключением НЕ делаем: после RUN reset debug-порт RP2040
	// какое-то время не отвечает (проверено на HIL). SWD работает и на
	// работающей прошивке.
	uint32_t idcode = 0;
	for (uint8_t attempt = 0; attempt < 3 && idcode == 0; attempt++) {
		if (attempt > 0) {
			delay(20);
			DEBUGLOGRPICO("connect: retry %u\r\n", attempt + 1);
		}
		idcode = rp_swd_init();
	}
	if (idcode == 0) {
		DEBUGLOGRPICO("connect: no answer, trying swapped CLK/DATA\r\n");
		rp_swd_set_pins(data, clk);
		rp_swd_gpio_init();
		idcode = rp_swd_init();
	}
	if (idcode == 0) {
		rp_swd_set_pins(clk, data);
		rp_swd_gpio_init();
		rp_swd_rst_pulse(10);
		delay(100);
		idcode = rp_swd_init();
	}
	if (idcode == 0) return 0;

	// Power-up запросы DP (как в SWD-модуле для STM32)
	rp_swd_DP_Write(DP_ABORT, 0x0000001EUL);
	rp_swd_DP_Write(DP_CTRLSTAT, 0x50000000UL);
	uint32_t ctrlstat = 0;
	rp_swd_DP_Read(DP_CTRLSTAT, ctrlstat);
	DEBUGLOGRPICO("connect: CTRL/STAT=0x%08x\r\n", ctrlstat);

	// Явно выбираем AP0 и bank 0. ВАЖНО: DP_SELECT только пишется —
	// чтение адреса 0x08 на DPv2 это RESEND (не отвечает, срывает SWD).
	rp_swd_DP_Write(DP_SELECT, 0);
	DEBUGLOGRPICO("connect: DP SELECT <- 0\r\n");

	// Сброс залипших ошибок AP (STICKYERR) перед доступом к памяти
	rp_swd_DP_Write(DP_ABORT, 0x0000001EUL);

	bool csw_written = ap_write_reg(AP_CSW, RP2040_AP_CSW);
	uint32_t csw = 0;
	ap_read_reg(AP_CSW, csw);
	uint32_t rom14 = 0;
	rp2040_read_word(0x14, rom14);
	DEBUGLOGRPICO("connect: CSW write=%d, CSW=0x%08x, ROM[0x14]=0x%08x, ack=%u\r\n",
		(int)csw_written, csw, rom14, rp_swd_last_ack);

	if (!csw_written) {
		DEBUGLOGRPICO("connect: AP CSW write failed\r\n");
		return 0;
	}
	DEBUGLOGRPICO("connect: AP CSW=0x%08x (want 0x%08x)\r\n", csw, (uint32_t)RP2040_AP_CSW);
	return idcode;
}

bool rp2040_is_halted() {
	uint32_t dh = 0;
	if (!rp2040_read_word(DBG_HCSR, dh)) return false;
	return (dh & S_HALT) != 0;
}

bool rp2040_halt() {
	uint32_t before = 0;
	bool rb = rp2040_read_word(DBG_HCSR, before);
	uint8_t ack_before = rp_swd_last_ack;

	bool wr = rp2040_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT);
	uint8_t ack_wr = rp_swd_last_ack;

	for (int i = 0; i < 200; i++) {
		if (rp2040_is_halted()) {
			DEBUGLOGRPICO("halt: ok after %d ms (before=0x%08x rb=%d wr=%d ack=%u/%u)\r\n",
				i, before, (int)rb, (int)wr, ack_before, ack_wr);
			return true;
		}
		delay(1);
	}

	uint32_t after = 0;
	rp2040_read_word(DBG_HCSR, after);
	DEBUGLOGRPICO("halt: FAILED (before=0x%08x rb=%d wr=%d ack=%u/%u, after=0x%08x)\r\n",
		before, (int)rb, (int)wr, ack_before, ack_wr, after);
	return false;
}

bool rp2040_resume() {
	return rp2040_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN);
}

bool rp2040_disable_irqs() {
	for (uint8_t i = 0; i < 8; i++) {
		if (!rp2040_write_word(NVIC_ICER0 + i * 4UL, 0xFFFFFFFFUL)) return false;
	}
	return true;
}

bool rp2040_read_word(uint32_t addr, uint32_t &value) {
	if (!ap_write_reg(AP_TAR, addr)) return false;
	uint32_t dummy = 0;
	if (!rp_swd_AP_Read(AP_DRW, dummy)) return false;
	return rp_swd_DP_Read(DP_RDBUFF, value);
}

bool rp2040_write_word(uint32_t addr, uint32_t value) {
	if (!ap_write_reg(AP_TAR, addr)) return false;
	if (!rp_swd_AP_Write(AP_DRW, value)) return false;
	uint32_t dummy = 0;
	rp_swd_DP_Read(DP_RDBUFF, dummy);
	return true;
}

bool rp2040_write_block(uint32_t addr, const uint8_t *data, uint32_t len) {
	if (len == 0) return true;
	if (data == nullptr) return false;
	if ((addr & 3UL) || (len & 3UL)) return false;

	if (!ap_write_reg(AP_CSW, RP2040_AP_CSW)) return false;
	if (!ap_write_reg(AP_TAR, addr)) return false;

	for (uint32_t i = 0; i < len; i += 4) {
		uint32_t v = (uint32_t)data[i]         |
		             ((uint32_t)data[i + 1] << 8)  |
		             ((uint32_t)data[i + 2] << 16) |
		             ((uint32_t)data[i + 3] << 24);
		if (!rp_swd_AP_Write(AP_DRW, v)) return false;
		uint32_t dummy = 0;
		if (!rp_swd_DP_Read(DP_RDBUFF, dummy)) return false;
	}
	return true;
}

bool rp2040_read_flash(uint32_t abs_addr, uint8_t *buf, uint32_t len) {
	if (buf == nullptr) return false;
	if (abs_addr & 3UL) return false;
	for (uint32_t off = 0; off < len; off += 4) {
		uint32_t v = 0;
		if (!rp2040_read_word(abs_addr + off, v)) return false;
		for (uint32_t b = 0; b < 4 && (off + b) < len; b++) {
			buf[off + b] = (uint8_t)((v >> (8 * b)) & 0xFF);
		}
	}
	return true;
}

bool rp2040_load_algo() {
	uint8_t buf[512];
	uint32_t n = (RP2040_ALGO_SIZE + 3UL) & ~3UL;
	if (n > sizeof(buf)) return false;
	memset(buf, 0, n);
	memcpy(buf, rp2040_algo_blob, RP2040_ALGO_SIZE);
	if (!rp2040_write_block(RPICO_SRAM_BLOB, buf, n)) {
		DEBUGLOGRPICO("load_algo: write failed\r\n");
		return false;
	}
	DEBUGLOGRPICO("load_algo: %u bytes at 0x%08x (erase +0x%03x, prog +0x%03x)\r\n",
		n, (uint32_t)RPICO_SRAM_BLOB, (uint32_t)RP2040_ALGO_ERASE_OFS,
		(uint32_t)RP2040_ALGO_PROG_OFS);
	return true;
}

bool rp2040_resolve_rom(uint32_t fns[RP2040_ROM_FUNC_COUNT]) {
	uint32_t w = 0;
	if (!rp2040_read_word(RP2040_ROM_FUNC_TABLE_PTR, w)) return false;
	uint32_t func_table = w & 0xFFFFUL;
	if (!rp2040_read_word(RP2040_ROM_LOOKUP_FN_PTR, w)) return false;
	uint32_t lookup_fn = w & 0xFFFFUL;
	if (func_table == 0 || lookup_fn == 0) {
		DEBUGLOGRPICO("resolve_rom: ROM table pointers not found (0x%04x/0x%04x)\r\n",
			func_table, lookup_fn);
		return false;
	}

	static const uint32_t codes[RP2040_ROM_FUNC_COUNT] = {
		RP2040_ROM_CODE_IF, RP2040_ROM_CODE_EX, RP2040_ROM_CODE_RE,
		RP2040_ROM_CODE_RP, RP2040_ROM_CODE_FC, RP2040_ROM_CODE_CX
	};

	uint32_t lp[RPL_PARAM_WORDS];
	memset(lp, 0, sizeof(lp));
	lp[RPL_PARAM_TABLE] = func_table;
	lp[RPL_PARAM_LOOKUP] = lookup_fn | 1UL;
	for (uint8_t i = 0; i < RP2040_ROM_FUNC_COUNT; i++) {
		lp[RPL_PARAM_CODES + i] = codes[i];
	}
	if (!rp2040_write_block(RPICO_SRAM_LPARAMS, (const uint8_t *)lp, sizeof(lp))) {
		DEBUGLOGRPICO("resolve_rom: params write failed\r\n");
		return false;
	}
	if (!run_blob(RP2040_ALGO_LOOKUP_OFS, 1000)) {
		DEBUGLOGRPICO("resolve_rom: lookup run failed\r\n");
		return false;
	}

	uint32_t status = 0;
	if (!rp2040_read_word(RPICO_SRAM_LPARAMS + RPL_PARAM_STATUS * 4UL, status) || status != 1) {
		DEBUGLOGRPICO("resolve_rom: lookup status=%u\r\n", status);
		return false;
	}

	uint32_t params[RP_PARAM_WORDS];
	memset(params, 0, sizeof(params));
	for (uint8_t i = 0; i < RP2040_ROM_FUNC_COUNT; i++) {
		uint32_t addr = 0;
		if (!rp2040_read_word(RPICO_SRAM_LPARAMS + (RPL_PARAM_RESULTS + i) * 4UL, addr)) return false;
		if (addr == 0) {
			DEBUGLOGRPICO("resolve_rom: code 0x%04x not found\r\n", codes[i]);
			return false;
		}
		fns[i] = addr | 1UL;
		params[i] = fns[i];
		DEBUGLOGRPICO("ROM fn 0x%04x -> 0x%08x\r\n", codes[i], addr & ~1UL);
	}

	if (!rp2040_write_block(RPICO_SRAM_PARAMS, (const uint8_t *)params, sizeof(params))) {
		DEBUGLOGRPICO("resolve_rom: fn params write failed\r\n");
		return false;
	}
	return true;
}

bool rp2040_erase(uint32_t flash_offset, uint32_t len) {
	if (len == 0) return true;
	if (!rp2040_write_word(RPICO_SRAM_PARAMS + RP_PARAM_OFFSET * 4UL, flash_offset)) return false;
	if (!rp2040_write_word(RPICO_SRAM_PARAMS + RP_PARAM_LEN * 4UL, len)) return false;
	if (!rp2040_write_word(RPICO_SRAM_PARAMS + RP_PARAM_STATUS * 4UL, 0)) return false;
	if (!run_blob(RP2040_ALGO_ERASE_OFS, 3000)) return false;

	uint32_t status = 0;
	if (!rp2040_read_word(RPICO_SRAM_PARAMS + RP_PARAM_STATUS * 4UL, status)) return false;
	if (status != 1) {
		DEBUGLOGRPICO("erase: status=%u offset=0x%08x len=%u\r\n", status, flash_offset, len);
		return false;
	}
	return true;
}

bool rp2040_program(uint32_t flash_offset, uint32_t len, uint32_t sram_data_addr) {
	if (len == 0) return true;
	if (!rp2040_write_word(RPICO_SRAM_PARAMS + RP_PARAM_OFFSET * 4UL, flash_offset)) return false;
	if (!rp2040_write_word(RPICO_SRAM_PARAMS + RP_PARAM_LEN * 4UL, len)) return false;
	if (!rp2040_write_word(RPICO_SRAM_PARAMS + RP_PARAM_DATA * 4UL, sram_data_addr)) return false;
	if (!rp2040_write_word(RPICO_SRAM_PARAMS + RP_PARAM_STATUS * 4UL, 0)) return false;
	if (!rp2040_write_word(RPICO_SRAM_PARAMS + RP_PARAM_PAGES_DONE * 4UL, 0)) return false;
	if (!run_blob(RP2040_ALGO_PROG_OFS, 5000)) return false;

	uint32_t status = 0;
	if (!rp2040_read_word(RPICO_SRAM_PARAMS + RP_PARAM_STATUS * 4UL, status)) return false;
	if (status != 1) {
		uint32_t pages = 0;
		rp2040_read_word(RPICO_SRAM_PARAMS + RP_PARAM_PAGES_DONE * 4UL, pages);
		DEBUGLOGRPICO("program: status=%u pages=%u offset=0x%08x len=%u\r\n",
			status, pages, flash_offset, len);
		return false;
	}
	return true;
}

void rp2040_reset_target() {
	if (!rp2040_write_word(NVIC_AIRCR, VECTKEY | SYSRESETREQ)) {
		DEBUGLOGRPICO("reset: AIRCR write failed, trying watchdog\r\n");
		rp2040_write_word(RP2040_WATCHDOG_CTRL, RP2040_WD_TRIGGER);
	}
	DEBUGLOGRPICO("reset: target reset requested\r\n");
}
