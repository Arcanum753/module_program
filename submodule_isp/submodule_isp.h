#ifndef _SUBMODULE_ISP_H
#define _SUBMODULE_ISP_H

#include "../module_prog/module_prog.h"
#include "prog_isp.h"
#include "submodule_isp_version.h"

#ifdef DEBUG_ISP
#define DEBUGLOGISP(...)  DBG_MOD("[M_ISP] ", __VA_ARGS__)
#else
#define DEBUGLOGISP(...)
#endif

#include "submodule_isp_types.h"

class Class_SubIsp : public Class_ProgBase {
public:
	Class_SubIsp(uint8_t in);

	// Виртуальные из Class_ProgBase
	const char* getProgTypePrefix() override { return "isp"; }
	const char* getChipCfgJsonPath() override { return AVRISP_CFG_JSON; }
	bool isFlashBusy() override { return avrprog.isFlashBusy(); }
	uint8_t getFlashPercent() override { return avrprog.getPercent(); }
	bool chipSpecificInit() override;
	void web_FileUpload2Chip(AsyncWebServerRequest *request) override;
	void onFlashComplete() override;
	void registerCustomRoutes() override;

	String getVersionStr() override { return String(SUBMODULE_ISP_VERSION_STR); }
	String getGeneratedTime() override { return String(SUBMODULE_ISP_GENERATED_TIME); }
	String getCommitDateStr() override { return String(SUBMODULE_ISP_COMMIT_DATE_STR); }

	// ISP-specific
	void _chipInfoAppendFields(JsonObject &out, JsonObject &chip) override;
	void onChipCheckComplete(const String &signature) override;
	void web_CheckChipStatus(AsyncWebServerRequest *request) override;
	bool chip_IsConnected() override;
	bool chipCfg_FindBySignature(const String &signature, ChipConfigAvr_t &cfg);
	void avrFusesRead(AsyncWebServerRequest *request);
	void avrWebFusesWrite(AsyncWebServerRequest *request);
	void web_AvrCfgInfo(AsyncWebServerRequest *request);
	void web_AvrCfgSave(AsyncWebServerRequest *request);
	void web_AvrCfgReadSignature(AsyncWebServerRequest *request);
};

extern Class_SubIsp progIsp;

#endif
