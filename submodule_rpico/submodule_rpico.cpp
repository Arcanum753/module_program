#include "core_web/FSWebServerLib.h"

#include <cstddef>
#include <cstring>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <LittleFS.h>
#endif

#include "../module_prog/module_prog.h"
#include "submodule_rpico.h"
#include "prog_rpico.h"
#include "rp_swd.h"
#include "rp2040_swd.h"
#include "../module_prog/format_bin.h"

// ============================================================
// Глобальные объекты субмодуля
// ============================================================

Class_SubRpico progRpico(0);
ESP_PROGRPICO rpicoProg;

Class_SubRpico::Class_SubRpico(uint8_t in) : Class_ProgBase(in) {
}

// ============================================================
// chipSpecificInit()
// ============================================================

bool Class_SubRpico::chipSpecificInit() {
	rp_swd_gpio_init();
	rpicoProg.setFs(_fs);
	_ensureChipCfgFile();
	return true;
}

// ============================================================
// Веб-обработчики
// ============================================================

void Class_SubRpico::web_CheckChipStatus(AsyncWebServerRequest *request) {
	DEBUGLOGRPICO("%s\r\n", __FUNCTION__);

	if (_progRunning || rpicoProg.isFlashBusy()) {
		request->send(200, "text/plain", "status|busy|div\n");
		return;
	}

	uint32_t idcode = rp2040_read_dpidr();
	onChipCheckComplete((idcode == RP2040_DPIDR) ? idcode : 0);
	request->send(200, "text/plain", "status|checking|div\n");
}

void Class_SubRpico::web_FileUpload2Chip(AsyncWebServerRequest *request) {
	if (_fs == nullptr) {
		return request->send(500, "text/plain", "FS not initialized");
	}
	if (request->args() == 0) {
		return request->send(500, "text/plain", "BAD ARGS");
	}
	if (rpicoProg.isFlashBusy() || _progRunning) {
		DEBUGLOGRPICO("flash: BUSY\r\n");
		return request->send(423, "text/plain", "busy");
	}

	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGRPICO("arg %u: %s=%s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());
		if (request->argName(i) == "path") {
			path = urldecode(request->arg(i));
		}
	}
	if (path.length() == 0 || path == "/") {
		return request->send(500, "text/plain", "BAD PATH");
	}
	if (!path.startsWith("/")) {
		path = "/" + path;
	}
	if (!_fs->exists(path)) {
		return request->send(404, "text/plain", "FileNotFound");
	}
	if (!binFileIsFormat(path)) {
		return request->send(415, "text/plain", "Only .bin/.binary supported");
	}

	_flashPath = path;
	_flashNtpStr = NTP.getTimeDateString();

	uint32_t idcode = rp2040_read_dpidr();
	if (idcode == 0) {
		String err = "RP2040 not detected (SWD offline)";
		DEBUGLOGRPICO("flash: %s\r\n", err.c_str());
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", err);
		return request->send(423, "text/plain", err);
	}
	if (idcode != RP2040_DPIDR) {
		char idStr[12];
		snprintf(idStr, sizeof(idStr), "0x%08x", idcode);
		String err = "Unexpected chip DPIDR " + String(idStr) + " (expected RP2040)";
		DEBUGLOGRPICO("flash: %s\r\n", err.c_str());
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", err);
		return request->send(423, "text/plain", err);
	}

	RpChipConfig_t chip;
	if (!chipCfg_FindById(idcode, chip)) {
		String err = "Chip not found in " + String(RPICO_CFG_JSON);
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", err);
		return request->send(423, "text/plain", err);
	}

	String expectedChipName = CfgFile_Prog.chip_name;
	if (expectedChipName.length() > 0 && chip.name != expectedChipName) {
		String err = "Chip mismatch: expected '" + expectedChipName + "', detected '" + chip.name + "'";
		DEBUGLOGRPICO("flash: %s\r\n", err.c_str());
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", err);
		return request->send(423, "text/plain", err);
	}

	if (!rpicoProg.startFlash(chip.flash_start, path, chip.flash_size,
	                          chip.sector_size, chip.page_size)) {
		DEBUGLOGRPICO("flash: startFlash failed\r\n");
		return request->send(500, "text/plain", "startFlash failed");
	}

	_progRunning = true;
	_progResult = -1;
	_progStartTime = millis();

	rpicoProg.beginFlashStep();

	request->send(200, "text/plain", "ok");
	DEBUGLOGRPICO("flash: started for %s (%s)\r\n", path.c_str(), chip.name.c_str());
}

void Class_SubRpico::registerCustomRoutes() {
	// Диагностика на время bring-up (под auth, как остальные маршруты):
	//   /rpico/diag?rst=<ms>   — удержать RUN в низком уровне
	//   /rpico/diag?levels=1   — уровни SWCLK/SWDIO
	ESPHTTPServer.on("/rpico/diag", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {
			return request->requestAuthentication();
		}
		String out = "";

		if (request->hasArg("halt")) {
			uint8_t clk0 = rp_swd_get_clk_pin();
			uint8_t data0 = rp_swd_get_data_pin();
			rp_swd_set_pins(clk0, data0);
			rp_swd_gpio_init();
			rp_swd_extra_turnaround = 0;

			uint32_t id = rp_swd_init();
			rp_swd_DP_Write(DP_ABORT, 0x1E);
			rp_swd_DP_Write(DP_CTRLSTAT, 0x50000000UL);
			rp_swd_DP_Write(DP_SELECT, 0);
			rp_swd_AP_Write(AP_CSW, RP2040_AP_CSW);

			uint32_t dh0 = 0;
			bool r0 = rp2040_read_word(DBG_HCSR, dh0);
			uint8_t ack0 = rp_swd_last_ack;

			bool wh = rp2040_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT);
			uint8_t ack1 = rp_swd_last_ack;

			uint32_t dh1 = 0;
			bool r1 = rp2040_read_word(DBG_HCSR, dh1);
			uint8_t ack2 = rp_swd_last_ack;

			delay(20);
			uint32_t dh2 = 0;
			bool r2 = rp2040_read_word(DBG_HCSR, dh2);
			uint8_t ack3 = rp_swd_last_ack;

			out += "init=0x" + String(id, HEX) + "|div\n";
			out += "dhcsr_before r=" + String((int)r0) + " val=0x" + String(dh0, HEX) + " ack=" + String(ack0) + "|div\n";
			out += "halt_write w=" + String((int)wh) + " ack=" + String(ack1) + "|div\n";
			out += "dhcsr_after r=" + String((int)r1) + " val=0x" + String(dh1, HEX) + " ack=" + String(ack2) + "|div\n";
			out += "dhcsr_delay r=" + String((int)r2) + " val=0x" + String(dh2, HEX) + " ack=" + String(ack3) + "|div\n";
			DEBUGLOGRPICO("diag halt: before=%08x wr=%d after=%08x/%08x\r\n",
				dh0, (int)wh, dh1, dh2);
		}

		if (request->hasArg("steps")) {
			uint8_t clk0 = rp_swd_get_clk_pin();
			uint8_t data0 = rp_swd_get_data_pin();
			rp_swd_set_pins(clk0, data0);
			rp_swd_gpio_init();
			rp_swd_extra_turnaround = 0;

			uint32_t id = rp_swd_init();
			out += "init id=0x" + String(id, HEX) + " ack=" + String(rp_swd_last_ack) + "|div\n";

			bool wabort = rp_swd_DP_Write(DP_ABORT, 0x1E);
			out += "abort w=" + String((int)wabort) + " ack=" + String(rp_swd_last_ack) + "|div\n";

			bool wcs = rp_swd_DP_Write(DP_CTRLSTAT, 0x50000000UL);
			uint32_t ctrl = 0;
			bool rcs = rp_swd_DP_Read(DP_CTRLSTAT, ctrl);
			out += "ctrl w=" + String((int)wcs) + " r=" + String((int)rcs) +
			       " val=0x" + String(ctrl, HEX) + " ack=" + String(rp_swd_last_ack) + "|div\n";

			bool wsel = rp_swd_DP_Write(DP_SELECT, 0);
			out += "select w=" + String((int)wsel) + " ack=" + String(rp_swd_last_ack) + "|div\n";

			bool wcsw = rp_swd_AP_Write(AP_CSW, RP2040_AP_CSW);
			out += "csw write=" + String((int)wcsw) + " ack=" + String(rp_swd_last_ack) + "|div\n";

			uint32_t csw = 0;
			// читаем CSW через AP-read + RDBUFF
			{
				uint32_t dummy = 0;
				bool rr = rp_swd_AP_Read(AP_CSW, dummy);
				uint32_t val = 0;
				bool rb = rp_swd_DP_Read(DP_RDBUFF, val);
				csw = val;
				out += "csw read=" + String((int)rr) + "/" + String((int)rb) +
				       " val=0x" + String(csw, HEX) + " ack=" + String(rp_swd_last_ack) + "|div\n";
			}

			uint32_t rom = 0;
			bool rrom = rp2040_read_word(0x14, rom);
			out += "rom14 r=" + String((int)rrom) + " val=0x" + String(rom, HEX) +
			       " ack=" + String(rp_swd_last_ack) + "|div\n";

			DEBUGLOGRPICO("diag steps done\r\n");
		}

		if (request->hasArg("ap")) {
			// Проверка доступности AP: (1) после dormant-последовательности
			// без TARGETSEL, (2) после line reset + TARGETSEL.
			uint8_t clk0 = rp_swd_get_clk_pin();
			uint8_t data0 = rp_swd_get_data_pin();
			rp_swd_set_pins(clk0, data0);
			rp_swd_gpio_init();
			rp_swd_extra_turnaround = 0;

			rp_swd_dormant_to_swd();
			uint32_t id1 = 0;
			rp_swd_DP_Read(DP_IDCODE, id1);
			rp_swd_DP_Write(DP_ABORT, 0x1E);
			rp_swd_DP_Write(DP_SELECT, 0);
			rp_swd_AP_Write(AP_CSW, RP2040_AP_CSW);
			uint32_t ap1 = 0;
			bool ok1 = rp2040_read_word(0x14, ap1);
			uint8_t ack1 = rp_swd_last_ack;

			rp_swd_line_reset_swd();
			rp_swd_targetsel(RP2040_TARGETSEL);
			uint32_t id2 = 0;
			rp_swd_DP_Read(DP_IDCODE, id2);
			rp_swd_DP_Write(DP_ABORT, 0x1E);
			rp_swd_DP_Write(DP_SELECT, 0);
			rp_swd_AP_Write(AP_CSW, RP2040_AP_CSW);
			uint32_t ap2 = 0;
			bool ok2 = rp2040_read_word(0x14, ap2);
			uint8_t ack2 = rp_swd_last_ack;

			out += "no_tsel id=0x" + String(id1, HEX) + " rom_ok=" + String((int)ok1) +
			       " rom14=0x" + String(ap1, HEX) + " ack=" + String(ack1) + "|div\n";
			out += "with_tsel id=0x" + String(id2, HEX) + " rom_ok=" + String((int)ok2) +
			       " rom14=0x" + String(ap2, HEX) + " ack=" + String(ack2) + "|div\n";
			DEBUGLOGRPICO("diag ap: no_tsel id=%08x rom=%d/0x%08x ack=%u; with_tsel id=%08x rom=%d/0x%08x ack=%u\r\n",
				id1, (int)ok1, ap1, ack1, id2, (int)ok2, ap2, ack2);
		}

		if (request->hasArg("resetprobe")) {
			// Подключение под сбросом: SWD-последовательность при RUN=0,
			// затем отпускаем и читаем DPIDR ещё раз.
			uint8_t clk0 = rp_swd_get_clk_pin();
			uint8_t data0 = rp_swd_get_data_pin();
			rp_swd_set_pins(clk0, data0);
			rp_swd_gpio_init();
			rp_swd_extra_turnaround = 0;

			rp_swd_rst_assert();
			delay(5);
			uint32_t id_in_reset = rp_swd_init();
			uint8_t ack_in_reset = rp_swd_last_ack;

			rp_swd_rst_release();
			delay(2);
			uint32_t id_after = rp_swd_init();
			uint8_t ack_after = rp_swd_last_ack;

			out += "in_reset id=0x" + String(id_in_reset, HEX);
			out += "|ack=" + String(ack_in_reset) + "|div\n";
			out += "after_reset id=0x" + String(id_after, HEX);
			out += "|ack=" + String(ack_after) + "|div\n";
		}

		if (request->hasArg("probe")) {
			// Матрица: порядок пинов × число тактов turnaround.
			uint8_t clk0 = rp_swd_get_clk_pin();
			uint8_t data0 = rp_swd_get_data_pin();
			rp_swd_rst_pulse(10);
			delay(20);
			for (uint8_t pins = 0; pins < 2; pins++) {
				rp_swd_set_pins(pins ? data0 : clk0, pins ? clk0 : data0);
				rp_swd_gpio_init();
				for (uint8_t ta = 0; ta < 2; ta++) {
					rp_swd_extra_turnaround = ta;
					uint32_t idcode = rp_swd_init();
					out += "pins=";
					out += (pins ? "swapped" : "normal");
					out += "|ta=" + String(ta);
					out += "|id=0x" + String(idcode, HEX);
					out += "|ack=" + String(rp_swd_last_ack) + "|div\n";
				}
			}
			rp_swd_extra_turnaround = 0;
			rp_swd_set_pins(clk0, data0);
			rp_swd_gpio_init();
		}

		if (request->hasArg("levels")) {
			int clk_lo = -1, clk_hi = -1, data_pd = -1, data_pu = -1;
			int clk_pd = -1, clk_pu = -1, clk_float = -1, data_float = -1, clk_float_low = -1;
			rp_swd_pin_levels(clk_lo, clk_hi, data_pd, data_pu, clk_pd, clk_pu, clk_float, data_float, clk_float_low);
			out += "clk_low=" + String(clk_lo) + "|div\n";
			out += "clk_high=" + String(clk_hi) + "|div\n";
			out += "clk_pull_down=" + String(clk_pd) + "|div\n";
			out += "clk_pull_up=" + String(clk_pu) + "|div\n";
			out += "clk_float_after_high=" + String(clk_float) + "|div\n";
			out += "clk_float_after_low=" + String(clk_float_low) + "|div\n";
			out += "data_pull_down=" + String(data_pd) + "|div\n";
			out += "data_pull_up=" + String(data_pu) + "|div\n";
			out += "data_float_after_low=" + String(data_float) + "|div\n";
			out += "clk_pin=" + String(rp_swd_get_clk_pin()) + "|div\n";
			out += "data_pin=" + String(rp_swd_get_data_pin()) + "|div\n";
			out += "rst_pin=" + String((int)RPICO_RST_PIN) + "|div\n";
			DEBUGLOGRPICO("diag levels: clk %d/%d (pd/pu %d/%d float hi/lo %d/%d), data pd/pu %d/%d float %d\r\n",
				clk_lo, clk_hi, clk_pd, clk_pu, clk_float, clk_float_low, data_pd, data_pu, data_float);
		}

		if (request->hasArg("rst")) {
			uint32_t hold = request->arg("rst").toInt();
			if (hold > 5000) hold = 5000;
			if (hold > 0) {
				DEBUGLOGRPICO("diag: hold RUN low for %u ms\r\n", hold);
				rp_swd_rst_assert();
				delay(hold);
				rp_swd_rst_release();
				delay(5);
			}
		}

		request->send(200, "text/plain", out.length() ? out : "ok\n");
	});
}

void Class_SubRpico::_chipInfoAppendFields(JsonObject &out, JsonObject &chip) {
	out["signature"]   = chip["idcode"].as<const char*>();
	out["family"]      = chip["family"].as<const char*>();
	out["flash"]       = chip["flash_size"].as<uint32_t>();
	out["sector"]      = chip["sector_size"].as<uint32_t>();
	out["page"]        = chip["page_size"].as<uint32_t>();
	out["flash_start"] = chip["flash_start"].as<const char*>();
}
