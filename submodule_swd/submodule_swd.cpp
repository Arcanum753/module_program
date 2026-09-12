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

Class_SubSwd progSwd(0);
Class_SubSwd::Class_SubSwd(uint8_t in): Class_ProgBase(in){ }

// ============================================================
// Переопределения виртуальных методов Class_ProgBase
// ============================================================

bool Class_SubSwd::chipSpecificInit() {
	swd_gpio_init();
	swdprog.setFs(_fs);
	return true;
}

void Class_SubSwd::web_CheckChipStatus(AsyncWebServerRequest *request) {
	DEBUGLOGSWD("%s\n\r", __FUNCTION__);
	String values = "";

	if (_progRunning || swdprog.isFlashBusy()) {
		values += "status|busy|div\n";
		request->send(200, "text/plain", values);
		return;
	}

	if (swdprog.isChipCheckBusy()) {
		values += "status|checking|div\n";
		request->send(200, "text/plain", values);
		return;
	}

	swdprog.startChipCheck();

	values += "status|checking|div\n";
	request->send(200, "text/plain", values);
	DEBUGLOGSWD("web_CheckChipStatus: EERTOS chip check started\r\n");
}

void Class_SubSwd::web_FileUpload2Chip(AsyncWebServerRequest *request) {
	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}

	if (swdprog.isFlashBusy()) {
		DEBUGLOGSWD("web_FileUpload2Chip: BUSY\n\r");
		return request->send(423, "text/plain", "busy");
	}
	if (_progRunning) {
		DEBUGLOGSWD("web_FileUpload2Chip: _progRunning already true\r\n");
		return request->send(423, "text/plain", "busy");
	}

	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGSWD("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "/")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 		{path = "/" + path;}
	if (!_fs->exists(path)) 		{	return request->send(404, "text/plain", "FileNotFound");	}

	DEBUGLOGSWD("\t upload status: %s\r\n", path.c_str());

	_flashPath = path;
	_flashNtpStr = NTP.getTimeDateString();

	uint32_t flashStart = DEFAULT_FLASH_START_ADDR;
	uint32_t chipMemSize = DEFAULT_PAGE_SIZE * 64;
	uint32_t pageSize = DEFAULT_PAGE_SIZE;
	uint32_t wordSize = DEFAULT_WORD_SIZE;
	uint32_t cswValue = DEFAULT_CSW_VALUE;

	String expectedChipName = CfgFile_Prog.chip_name;

	swd_gpio_init();
	uint32_t idcode = swd_init();

	if (idcode == 0) {
		if (expectedChipName.length() > 0) {
			String errorText = "Chip offline - unable to read IDCODE";
			DEBUGLOGSWD("web_FileUpload2Chip: %s\n\r", errorText.c_str());
			filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
			return request->send(423, "text/plain", errorText);
		}
		DEBUGLOGSWD("web_FileUpload2Chip: chip not detected, using defaults\n\r");
	} else {
		DEBUGLOGSWD("web_FileUpload2Chip: detected chip ID=0x%08x\n\r", idcode);

		ChipConfig_t chipCfg;
		bool found = chipCfg_FindById(idcode, chipCfg);

		if (found) {
			if (expectedChipName.length() > 0 && chipCfg.name != expectedChipName) {
				String errorText = "Chip mismatch: expected '" + expectedChipName + "', detected '" + chipCfg.name + "'";
				DEBUGLOGSWD("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			flashStart = chipCfg.flash_start;
			chipMemSize = chipCfg.flash_size;
			pageSize = chipCfg.page_size;
			wordSize = chipCfg.word_size;
			cswValue = chipCfg.csw_value;
			swdprog.setChipFamily(chipCfg.family);
			DEBUGLOGSWD("web_FileUpload2Chip: using config for %s (family=%s flash=%u start=0x%08x page=%u word=%u csw=0x%08x)\n\r",
				chipCfg.name.c_str(), chipCfg.family.c_str(), chipMemSize, flashStart, pageSize, wordSize, cswValue);
		} else {
			if (expectedChipName.length() > 0) {
				char idStr[12];
				snprintf(idStr, sizeof(idStr), "0x%08x", idcode);
				String errorText = "Chip '" + expectedChipName + "' not found in swd_cfg.json (IDCODE=" + String(idStr) + ")";
				DEBUGLOGSWD("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			swdprog.setChipFamily("stm32f1");
			DEBUGLOGSWD("web_FileUpload2Chip: chip ID=0x%08x not in swd_cfg.json, using defaults (family=stm32f1)\n\r", idcode);
		}
	}

	if (!swdprog.startFlash(flashStart, _flashPath, chipMemSize, pageSize, wordSize, cswValue)) {
		DEBUGLOGSWD("web_FileUpload2Chip: startFlash() failed\r\n");
		return request->send(500, "text/plain", "startFlash failed");
	}

	_progRunning = true;
	_progResult = -1;
	_progStartTime = millis();

	swdprog.beginFlashStep();

	request->send(200, "text/plain", "ok");
	DEBUGLOGSWD("web_FileUpload2Chip: EERTOS flash started for %s\r\n", _flashPath.c_str());
}

void Class_SubSwd::registerCustomRoutes() {
}

void Class_SubSwd::_chipInfoAppendFields(JsonObject &out, JsonObject &chip) {
	out["signature"] = chip["idcode"].as<const char*>();
	out["family"] = chip["family"].as<const char*>();
	out["flash"] = chip["flash_size"].as<uint32_t>();
	out["page"] = chip["page_size"].as<uint32_t>();
	out["flash_start"] = chip["flash_start"].as<const char*>();
	out["word_size"] = chip["word_size"].as<uint32_t>();
	out["csw_value"] = chip["csw_value"].as<const char*>();
}
