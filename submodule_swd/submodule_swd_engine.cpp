#include <cstddef>
#include <cstring>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <LittleFS.h>
#endif

#include "../module_prog/module_prog.h"
#include "submodule_swd.h"
#include "prog_swd.h"
#include "swd.h"

// ============================================================
// Конкретная логика модуля (SWD)
// ============================================================

int Class_SubSwd::prog_Programm(String path, String fwTime) {
	DEBUGLOGSWD(__PRETTY_FUNCTION__); DEBUGLOGSWD("\r\n");
	DEBUGLOGSWD(" file %s time %s \r\n", path.c_str(), fwTime.c_str());

	if (!_fs) { return ERR_CFG; }
	if (!_fs->exists(path)) { return ERR_NOFILE; }

	int _res = ERR_OPENFILE;
	swdprog.stm32Fx_begin();
	_res = swdprog.stm32_ChipProgrammMain(path);

	String progStatus = (_res == 0) ? "ok" : "error";
	filelist_SetProgStatus(path, fwTime, progStatus);
	DEBUGLOGSWD("Programming %s, saved prog status to filelist: %s date=%s\r\n",
		(_res == 0) ? "success" : "failed", path.c_str(), fwTime.c_str());

	DEBUGLOGSWD("Programming end \r\n");
	return _res;
}

bool Class_SubSwd::chipCfg_FindById(uint32_t idcode, ChipConfig_t &cfg) {
	DEBUGLOGSWD("%s: searching for ID=0x%08x\n\r", __FUNCTION__, idcode);

	if (!_fs) {
		DEBUGLOGSWD("chipCfg_FindById: FS not initialized\n\r");
		return false;
	}
	if (!_fs->exists(SWD_CFG_JSON)) {
		DEBUGLOGSWD("chipCfg_FindById: %s not found\n\r", SWD_CFG_JSON);
		return false;
	}

	File file = _fs->open(SWD_CFG_JSON, "r");
	if (!file) {
		DEBUGLOGSWD("chipCfg_FindById: failed to open %s\n\r", SWD_CFG_JSON);
		return false;
	}

	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, file);
	file.close();

	if (err) {
		DEBUGLOGSWD("chipCfg_FindById: JSON parse error: %s\n\r", err.c_str());
		return false;
	}

	JsonArray chips = doc["chips"].as<JsonArray>();
	if (chips.isNull()) {
		DEBUGLOGSWD("chipCfg_FindById: no 'chips' array in %s\n\r", SWD_CFG_JSON);
		return false;
	}

	for (JsonObject chip : chips) {
		uint32_t chipIdcode = 0;
		if (chip["idcode"].is<const char*>()) {
			chipIdcode = strtoul(chip["idcode"].as<const char*>(), NULL, 0);
		} else {
			chipIdcode = chip["idcode"].as<uint32_t>();
		}

		if (chipIdcode == idcode) {
			cfg.idcode = chipIdcode;
			cfg.name = chip["name"].as<const char*>();
			cfg.family = chip["family"].as<const char*>();
			cfg.flash_size = chip["flash_size"].as<uint32_t>();

			if (chip["flash_start"].is<const char*>()) {
				cfg.flash_start = strtoul(chip["flash_start"].as<const char*>(), NULL, 0);
			} else {
				cfg.flash_start = chip["flash_start"].as<uint32_t>();
			}

			cfg.page_size = chip["page_size"].as<uint32_t>();
			cfg.word_size = chip["word_size"].as<uint32_t>();

			if (chip["csw_value"].is<const char*>()) {
				cfg.csw_value = strtoul(chip["csw_value"].as<const char*>(), NULL, 0);
			} else {
				cfg.csw_value = chip["csw_value"].as<uint32_t>();
			}

			DEBUGLOGSWD("chipCfg_FindById: found %s (family=%s) flash=%u start=0x%08x page=%u word=%u csw=0x%08x\n\r",
				cfg.name.c_str(), cfg.family.c_str(), cfg.flash_size, cfg.flash_start,
				cfg.page_size, cfg.word_size, cfg.csw_value);
			return true;
		}
	}

	DEBUGLOGSWD("chipCfg_FindById: ID=0x%08x not found in %s\n\r", idcode, SWD_CFG_JSON);
	return false;
}

bool Class_SubSwd::chip_IsConnected() {
	if (_progRunning || swdprog.isFlashBusy()) {
		return true;
	}
	if (!_chipConnected) return false;
	if ((millis() - _chipStatusTime) >= (CHIP_STATUS_TIMEOUT * 1000UL)) {
		_chipConnected = false;
		return false;
	}
	return true;
}

void Class_SubSwd::onChipCheckComplete(uint32_t chipId) {
	DEBUGLOGSWD("%s: chipId=0x%08x\n\r", __FUNCTION__, chipId);

	_chipStatusTime = millis();

	if (chipId != 0) {
		_chipConnected = true;
		_chipId = chipId;
		DEBUGLOGSWD("onChipCheckComplete: chip connected, ID=0x%08x\r\n", chipId);
	} else {
		_chipConnected = false;
		_chipId = 0;
		DEBUGLOGSWD("onChipCheckComplete: chip NOT detected\r\n");
	}
}

void Class_SubSwd::onFlashComplete() {
	if (swdprog.isFlashError()) {
		DEBUGLOGSWD("onFlashComplete: ERROR during programming of %s\r\n", _flashPath.c_str());
		String errorText = swdprog.getFlashErrorString();
		String errorStage = swdprog.getFlashErrorStage();
		uint8_t errorPercent = swdprog.getFlashErrorPercent();
		String elapsedStr = "";
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			elapsedStr = (String)elapsed;
		}
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText, elapsedStr, errorStage, (String)errorPercent);
		_progResult = 1;
		_progRunning = false;
		_uploadPercent = 0;
		DEBUGLOGSWD("Programming error: %s, stage=%s, percent=%u, saved prog status to filelist\r\n", errorText.c_str(), errorStage.c_str(), errorPercent);
	} else {
		DEBUGLOGSWD("onFlashComplete: success for %s\r\n", _flashPath.c_str());

		String elapsedStr = "";
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			elapsedStr = (String)elapsed;
		}

		// Скорость в KB/s: _speed = байты/мс → переводим в КБ/с
		float speed = swdprog.getSpeed();
		String speedStr = "";
		if (speed > 0.0f) {
			speedStr = String(speed, 2);
		}

		filelist_SetProgStatus(_flashPath, _flashNtpStr, "ok", "", elapsedStr, "", "", speedStr);

		DEBUGLOGSWD("Programming success, saved prog date to filelist: %s, time=%sms, speed=%s KB/s\r\n", _flashNtpStr.c_str(), elapsedStr.c_str(), speedStr.c_str());

		_progResult = 0;
		_progRunning = false;
		_uploadPercent = 100;

		DEBUGLOGSWD("Programming end \r\n");
	}
}
