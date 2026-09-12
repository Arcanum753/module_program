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

Class_SubIsp progIsp(0);
Class_SubIsp::Class_SubIsp(uint8_t in): Class_ProgBase(in){ }

// ============================================================
// Переопределения виртуальных методов Class_ProgBase
// ============================================================

bool Class_SubIsp::chipSpecificInit() {
	avrprog.begin();
	avrprog.setFs(_fs);
	return true;
}

void Class_SubIsp::web_CheckChipStatus(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);

	if (_progRunning || avrprog.isFlashBusy()) {
		request->send(200, "text/plain", "chipstatus|busy|div\n");
		return;
	}

	avrprog.startChipCheck();
	request->send(200, "text/plain", "chipstatus|checking|div\n");
}

void Class_SubIsp::web_FileUpload2Chip(AsyncWebServerRequest *request) {
	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}

	if (avrprog.isFlashBusy()) {
		DEBUGLOGISP("web_FileUpload2Chip: BUSY\n\r");
		return request->send(423, "text/plain", "busy");
	}
	if (_progRunning) {
		DEBUGLOGISP("web_FileUpload2Chip: _progRunning already true\r\n");
		return request->send(423, "text/plain", "busy");
	}

	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGISP("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "/")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 		{path = "/" + path;}
	if (!_fs->exists(path)) 		{	return request->send(404, "text/plain", "FileNotFound");	}

	DEBUGLOGISP("\t upload status: %s\r\n", path.c_str());

	_flashPath = path;
	_flashNtpStr = NTP.getTimeDateString();

	uint32_t flashStart = 0;
	uint32_t chipMemSize = 32768;
	uint32_t pageSize = 128;

	String expectedChipName = CfgFile_Prog.chip_name;
	String signature = avrprog.chipSignRead();

	if (signature.length() == 0 || signature == "0x000000") {
		if (expectedChipName.length() > 0) {
			String errorText = "Chip offline - unable to read signature";
			DEBUGLOGISP("web_FileUpload2Chip: %s\n\r", errorText.c_str());
			filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
			return request->send(423, "text/plain", errorText);
		}
		DEBUGLOGISP("web_FileUpload2Chip: chip not detected, using defaults\n\r");
	} else {
		DEBUGLOGISP("web_FileUpload2Chip: detected chip signature=%s\n\r", signature.c_str());

		ChipConfigAvr_t chipCfg;
		bool found = chipCfg_FindBySignature(signature, chipCfg);

		if (found) {
			if (expectedChipName.length() > 0 && chipCfg.name != expectedChipName) {
				String errorText = "Chip mismatch: expected '" + expectedChipName + "', detected '" + chipCfg.name + "'";
				DEBUGLOGISP("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			chipMemSize = chipCfg.flash_size;
			pageSize = chipCfg.page_size;
			DEBUGLOGISP("web_FileUpload2Chip: using config for %s (flash=%u page=%u)\n\r",
				chipCfg.name.c_str(), chipMemSize, pageSize);
		} else {
			if (expectedChipName.length() > 0) {
				String errorText = "Chip '" + expectedChipName + "' not found in avrisp_cfg.json (signature=" + signature + ")";
				DEBUGLOGISP("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			DEBUGLOGISP("web_FileUpload2Chip: chip signature=%s not in avrisp_cfg.json, using defaults\n\r", signature.c_str());
		}
	}

	if (!avrprog.startFlash(flashStart, _flashPath, chipMemSize, pageSize)) {
		DEBUGLOGISP("web_FileUpload2Chip: startFlash() failed\r\n");
		return request->send(500, "text/plain", "startFlash failed");
	}

	_progRunning = true;
	_progResult = -1;
	_progStartTime = millis();

	avrprog.beginFlashStep();

	request->send(200, "text/plain", "ok");
	DEBUGLOGISP("web_FileUpload2Chip: EERTOS flash started for %s\r\n", _flashPath.c_str());
}

void Class_SubIsp::registerCustomRoutes() {
	ESPHTTPServer.on("/avr/fuseread", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		avrFusesRead(request);
	});
	ESPHTTPServer.on("/avr/fusewrite", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		avrWebFusesWrite(request);
	});
	ESPHTTPServer.on("/avr/info", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_AvrCfgInfo(request);
	});
	ESPHTTPServer.on("/avr/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_AvrCfgSave(request);
	});
	ESPHTTPServer.on("/avr/readsignature", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_AvrCfgReadSignature(request);
	});
	ESPHTTPServer.on("/avrcfg", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		ESPHTTPServer.handleFileRead("/web/avrcfg.html", request);
	});
}

void Class_SubIsp::avrFusesRead(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);
	String values = "";

	AVRISP_fuses_t fuses;
	avrprog.chipFusesRead(fuses);

	values += "avrfuselow|"   + String(fuses.low, HEX)  + "|div\n";
	values += "avrfusehigh|"  + String(fuses.high, HEX) + "|div\n";
	values += "avrfuseext|"   + String(fuses.ext, HEX)  + "|div\n";
	values += "avrfuseprot|"  + String(fuses.lock, HEX) + "|div\n";

	request->send(200, "text/plain", values);
}

void Class_SubIsp::avrWebFusesWrite(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);

	if (request->args() == 0) {
		request->send(500, "text/plain", "BAD ARGS");
		return;
	}

	uint8_t high = 0, low = 0, lock = 0, ext = 0;
	bool hasHigh = false, hasLow = false, hasLock = false, hasExt = false;

	for (uint8_t i = 0; i < request->args(); i++) {
		if (request->argName(i) == "avrfusehigh") {
			high = (uint8_t)strtoul(request->arg(i).c_str(), NULL, 16);
			hasHigh = true;
		} else if (request->argName(i) == "avrfuselow") {
			low = (uint8_t)strtoul(request->arg(i).c_str(), NULL, 16);
			hasLow = true;
		} else if (request->argName(i) == "avrfuseprot") {
			lock = (uint8_t)strtoul(request->arg(i).c_str(), NULL, 16);
			hasLock = true;
		} else if (request->argName(i) == "avrfuseext") {
			ext = (uint8_t)strtoul(request->arg(i).c_str(), NULL, 16);
			hasExt = true;
		}
	}

	if (!hasHigh && !hasLow && !hasLock && !hasExt) {
		request->send(500, "text/plain", "BAD ARGS: no fuse values provided");
		return;
	}

	avrprog.chipFusesWrite(high, low, lock, ext);
	request->send(200, "text/plain", "OK");
}

void Class_SubIsp::web_AvrCfgInfo(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);
	String values = "";

	CfgFile_ProgBase_t cfg;
	cfg_FileStructGet(cfg);
	values += "projname|" + cfg.project_name + "|input\n";
	values += "chipname|" + cfg.chip_name + "|input\n";

	if (_chipIdstr.length() > 0 && _chipIdstr != "0x000000") {
		values += "signature|" + _chipIdstr + "|div\n";
		ChipConfigAvr_t chipCfg;
		if (chipCfg_FindBySignature(_chipIdstr, chipCfg)) {
			values += "chipname|" + chipCfg.name + "|div\n";
		} else {
			values += "chipname|Unknown|div\n";
		}
	} else {
		values += "signature|N/A|div\n";
		values += "chipname|N/A|div\n";
	}

	request->send(200, "text/plain", values);
}

void Class_SubIsp::web_AvrCfgSave(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);

	if (request->args() == 0) {
		request->send(500, "text/plain", "BAD ARGS");
		return;
	}

	CfgFile_ProgBase_t newCfg;
	cfg_FileStructGet(newCfg);

	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGISP("Arg %d: %s = %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());
		if (request->argName(i) == "projname") {
			newCfg.project_name = urldecode(request->arg(i));
			continue;
		}
		if (request->argName(i) == "chipname") {
			newCfg.chip_name = urldecode(request->arg(i));
			continue;
		}
	}

	if (newCfg.project_name.length() == 0) {
		request->send(500, "text/plain", "ERROR|Project name cannot be empty");
		return;
	}

	if (cfg_FileSaveFromWeb(newCfg) == 0) {
		request->send(200, "text/plain", "OK");
		DEBUGLOGISP("web_AvrCfgSave: saved project='%s' chip='%s'\r\n",
			newCfg.project_name.c_str(), newCfg.chip_name.c_str());
	} else {
		request->send(500, "text/plain", "ERROR|Failed to save configuration");
	}
}

void Class_SubIsp::web_AvrCfgReadSignature(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);
	String values = "";
	_chipIdstr = avrprog.chipSignRead();
	values += "signature|" + _chipIdstr + "|div\n";
	request->send(200, "text/plain", values);
}

void Class_SubIsp::_chipInfoAppendFields(JsonObject &out, JsonObject &chip) {
	out["signature"] = chip["signature"].as<const char*>();
	out["flash"] = chip["flash_size"].as<uint32_t>();
	out["page"] = chip["page_size"].as<uint32_t>();
}
