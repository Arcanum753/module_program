#include "core_web/FSWebServerLib.h"

#include <cstring>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <LittleFS.h>
#endif

#include "../module_prog/module_prog.h"
#include "submodule_rpico.h"
#include "prog_rpico.h"

// ============================================================
// Конкретная логика субмодуля RP2040
// ============================================================

// Дефолтная база чипов. Создаётся на FS при первом старте, если файла нет:
// иначе /project.html не покажет список чипов (web_ProjectChips читает JSON).
static const char RPICO_CFG_DEFAULT[] =
	"{\"chips\":[{\"idcode\":\"0x0bc12477\",\"name\":\"RP2040\",\"family\":\"rp2040\","
	"\"flash_size\":2097152,\"flash_start\":\"0x10000000\","
	"\"sector_size\":4096,\"page_size\":256}]}";

void Class_SubRpico::_ensureChipCfgFile() {
	if (!_fs) return;
	if (_fs->exists(RPICO_CFG_JSON)) return;

	File file = _fs->open(RPICO_CFG_JSON, "w");
	if (!file) {
		DEBUGLOGRPICO("_ensureChipCfgFile: cannot create %s\r\n", RPICO_CFG_JSON);
		return;
	}
	file.print(RPICO_CFG_DEFAULT);
	file.close();
	DEBUGLOGRPICO("_ensureChipCfgFile: created default %s\r\n", RPICO_CFG_JSON);
}

// Разбор числового поля JSON: строка "0x..." или число.
static uint32_t rpico_jsonU32(JsonVariant v, uint32_t def_val) {
	if (v.is<const char*>()) {
		return (uint32_t)strtoul(v.as<const char*>(), nullptr, 0);
	}
	if (v.is<uint32_t>()) {
		return v.as<uint32_t>();
	}
	return def_val;
}

bool Class_SubRpico::chipCfg_FindById(uint32_t idcode, RpChipConfig_t &cfg) {
	// Встроенный дефолт (работает и без /rpico_cfg.json на FS)
	cfg.idcode      = RP2040_DPIDR;
	cfg.name        = "RP2040";
	cfg.family      = "rp2040";
	cfg.flash_size  = RP2040_FLASH_SIZE;
	cfg.flash_start = RP2040_FLASH_BASE;
	cfg.sector_size = RP2040_SECTOR_SIZE;
	cfg.page_size   = RP2040_PAGE_SIZE;

	if (idcode != RP2040_DPIDR) {
		DEBUGLOGRPICO("chipCfg_FindById: unsupported DPIDR 0x%08x\r\n", idcode);
		return false;
	}

	if (!_fs || !_fs->exists(RPICO_CFG_JSON)) {
		DEBUGLOGRPICO("chipCfg_FindById: %s not found, using built-in RP2040\r\n", RPICO_CFG_JSON);
		return true;
	}

	File file = _fs->open(RPICO_CFG_JSON, "r");
	if (!file) {
		DEBUGLOGRPICO("chipCfg_FindById: failed to open %s, using built-in RP2040\r\n", RPICO_CFG_JSON);
		return true;
	}

	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, file);
	file.close();
	if (err) {
		DEBUGLOGRPICO("chipCfg_FindById: JSON parse error: %s\r\n", err.c_str());
		return true;
	}

	JsonArray chips = doc["chips"].as<JsonArray>();
	if (chips.isNull()) {
		DEBUGLOGRPICO("chipCfg_FindById: no 'chips' array in %s\r\n", RPICO_CFG_JSON);
		return true;
	}

	for (JsonObject chip : chips) {
		uint32_t chip_id = rpico_jsonU32(chip["idcode"], 0);
		if (chip_id != idcode) {
			continue;
		}

		const char *name = chip["name"].as<const char*>();
		if (name) cfg.name = name;

		const char *family = chip["family"].as<const char*>();
		if (family) cfg.family = family;

		cfg.flash_size  = rpico_jsonU32(chip["flash_size"], cfg.flash_size);
		cfg.flash_start = rpico_jsonU32(chip["flash_start"], cfg.flash_start);
		cfg.sector_size = rpico_jsonU32(chip["sector_size"], cfg.sector_size);
		cfg.page_size   = rpico_jsonU32(chip["page_size"], cfg.page_size);

		DEBUGLOGRPICO("chipCfg_FindById: %s (family=%s flash=%u start=0x%08x sector=%u page=%u)\r\n",
			cfg.name.c_str(), cfg.family.c_str(), cfg.flash_size, cfg.flash_start,
			cfg.sector_size, cfg.page_size);
		return true;
	}

	DEBUGLOGRPICO("chipCfg_FindById: DPIDR 0x%08x not found in %s, using built-in RP2040\r\n",
		idcode, RPICO_CFG_JSON);
	return true;
}

bool Class_SubRpico::chip_IsConnected() {
	if (_progRunning || rpicoProg.isFlashBusy()) {
		return true;
	}
	if (!_chipConnected) return false;
	if ((millis() - _chipStatusTime) >= (CHIP_STATUS_TIMEOUT * 1000UL)) {
		_chipConnected = false;
		return false;
	}
	return true;
}

void Class_SubRpico::onChipCheckComplete(uint32_t chipId) {
	_chipStatusTime = millis();

	if (chipId != 0) {
		_chipConnected = true;
		_chipId = chipId;
		DEBUGLOGRPICO("onChipCheckComplete: RP2040 connected, DPIDR=0x%08x\r\n", chipId);
	} else {
		_chipConnected = false;
		_chipId = 0;
		DEBUGLOGRPICO("onChipCheckComplete: chip NOT detected\r\n");
	}
}

void Class_SubRpico::onFlashComplete() {
	if (rpicoProg.isFlashError()) {
		String errorText = rpicoProg.getFlashErrorString();
		String errorStage = rpicoProg.getFlashErrorStage();
		uint8_t errorPercent = rpicoProg.getFlashErrorPercent();
		String elapsedStr;
		if (_progStartTime > 0) {
			elapsedStr = (String)(millis() - _progStartTime);
		}
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText,
			elapsedStr, errorStage, (String)errorPercent);
		_progResult = 1;
		_progRunning = false;
		_uploadPercent = 0;
		DEBUGLOGRPICO("onFlashComplete: error '%s' (stage=%s, %u%%)\r\n",
			errorText.c_str(), errorStage.c_str(), errorPercent);
		return;
	}

	String elapsedStr;
	if (_progStartTime > 0) {
		elapsedStr = (String)(millis() - _progStartTime);
	}

	float speed = rpicoProg.getSpeed();
	String speedStr = (speed > 0.0f) ? String(speed, 2) : "";

	filelist_SetProgStatus(_flashPath, _flashNtpStr, "ok", "", elapsedStr, "", "", speedStr);
	_progResult = 0;
	_progRunning = false;
	_uploadPercent = 100;

	DEBUGLOGRPICO("onFlashComplete: success '%s', %sms, %s KB/s\r\n",
		_flashPath.c_str(), elapsedStr.c_str(), speedStr.c_str());
}
