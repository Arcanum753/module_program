#include <cstddef>
#include <cstring>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <LittleFS.h>
#endif

#include "../module_prog/module_prog.h"
#include "submodule_isp.h"
#include "prog_isp.h"

// ============================================================
// Конкретная логика модуля (ISP)
// ============================================================

bool Class_SubIsp::chipCfg_FindBySignature(const String &signature, ChipConfigAvr_t &cfg) {
	DEBUGLOGISP("%s: searching for signature=%s\n\r", __FUNCTION__, signature.c_str());

	if (!_fs) {
		DEBUGLOGISP("chipCfg_FindBySignature: FS not initialized\n\r");
		return false;
	}
	if (!_fs->exists(AVRISP_CFG_JSON)) {
		DEBUGLOGISP("chipCfg_FindBySignature: %s not found\n\r", AVRISP_CFG_JSON);
		return false;
	}

	File file = _fs->open(AVRISP_CFG_JSON, "r");
	if (!file) {
		DEBUGLOGISP("chipCfg_FindBySignature: failed to open %s\n\r", AVRISP_CFG_JSON);
		return false;
	}

	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, file);
	file.close();

	if (err) {
		DEBUGLOGISP("chipCfg_FindBySignature: JSON parse error: %s\n\r", err.c_str());
		return false;
	}

	JsonArray chips = doc["chips"].as<JsonArray>();
	if (chips.isNull()) {
		DEBUGLOGISP("chipCfg_FindBySignature: no 'chips' array in %s\n\r", AVRISP_CFG_JSON);
		return false;
	}

	String searchSig = signature;
	if (searchSig.startsWith("0x") || searchSig.startsWith("0X")) {
		searchSig = searchSig.substring(2);
	}
	searchSig.toLowerCase();

	for (JsonObject chip : chips) {
		String chipSig = chip["signature"].as<const char*>();
		if (chipSig.startsWith("0x") || chipSig.startsWith("0X")) {
			chipSig = chipSig.substring(2);
		}
		chipSig.toLowerCase();

		if (chipSig == searchSig) {
			cfg.signature = chip["signature"].as<const char*>();
			cfg.name = chip["name"].as<const char*>();
			cfg.flash_size = chip["flash_size"].as<uint32_t>();
			cfg.page_size = chip["page_size"].as<uint32_t>();

			DEBUGLOGISP("chipCfg_FindBySignature: found %s (signature=%s) flash=%u page=%u\n\r",
				cfg.name.c_str(), cfg.signature.c_str(), cfg.flash_size, cfg.page_size);
			return true;
		}
	}

	DEBUGLOGISP("chipCfg_FindBySignature: signature=%s not found in %s\n\r", signature.c_str(), AVRISP_CFG_JSON);
	return false;
}

bool Class_SubIsp::chip_IsConnected() {
	if (_chipStatusTime > 0 && (millis() - _chipStatusTime) < (CHIP_STATUS_TIMEOUT * 1000)) {
		return _chipConnected;
	}
	String sig = avrprog.chipSignRead();
	_chipStatusTime = millis();
	if (sig.length() > 0 && sig != "0x000000") {
		_chipConnected = true;
		_chipIdstr = sig;
	} else {
		_chipConnected = false;
		_chipIdstr = "";
	}
	DEBUGLOGISP("chip_IsConnected: %s (sig=%s)\r\n", _chipConnected ? "YES" : "NO", _chipIdstr.c_str());
	return _chipConnected;
}

void Class_SubIsp::onChipCheckComplete(const String &signature) {
	DEBUGLOGISP("onChipCheckComplete: signature=%s\r\n", signature.c_str());

	_chipStatusTime = millis();
	if (signature.length() > 0 && signature != "0x000000") {
		_chipConnected = true;
		_chipIdstr = signature;
		DEBUGLOGISP("onChipCheckComplete: chip CONNECTED (sig=%s)\r\n", signature.c_str());
	} else {
		_chipConnected = false;
		_chipIdstr = "";
		DEBUGLOGISP("onChipCheckComplete: chip NOT CONNECTED\r\n");
	}
}

void Class_SubIsp::onFlashComplete() {
	if (avrprog.isFlashError()) {
		DEBUGLOGISP("onFlashComplete: ERROR during programming of %s\r\n", _flashPath.c_str());
		String errorText = avrprog.getFlashErrorString();
		String errorStage = avrprog.getFlashErrorStage();
		String errorPercent = String(avrprog.getFlashErrorPercent());
		String elapsedStr = "";
		if (_progStartTime > 0) {
			elapsedStr = String(millis() - _progStartTime);
		}
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText, elapsedStr, errorStage, errorPercent);
		_progResult = 1;
		_progRunning = false;
		_uploadPercent = 0;
		DEBUGLOGISP("Programming error: %s, stage=%s, pct=%s, saved prog status to filelist\r\n", errorText.c_str(), errorStage.c_str(), errorPercent.c_str());
	} else {
		DEBUGLOGISP("onFlashComplete: success for %s\r\n", _flashPath.c_str());

		String elapsedStr = "";
		if (_progStartTime > 0) {
			elapsedStr = String(millis() - _progStartTime);
		}
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "ok", "", elapsedStr);

		DEBUGLOGISP("Programming success, saved prog date to filelist: %s, time=%sms\r\n", _flashNtpStr.c_str(), elapsedStr.c_str());

		_progResult = 0;
		_progRunning = false;
		_uploadPercent = 100;

		DEBUGLOGISP("Programming end \r\n");
	}
}
