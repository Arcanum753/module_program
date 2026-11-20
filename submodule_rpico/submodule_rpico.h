#ifndef _SUBMODULE_RPICO_H
#define _SUBMODULE_RPICO_H

// ============================================================
// submodule_rpico.h — субмодуль программатора RP2040 (SWD).
// Наследник Class_ProgBase; использует общий веб-интерфейс
// module_prog (/prog.html, /project.html, /prog/*).
// ============================================================

#include "../module_prog/module_prog.h"
#include "prog_rpico.h"
#include "submodule_rpico_version.h"

#ifdef DEBUG_RPICO
#define DEBUGLOGRPICO(...) DBG_MOD("[M_RPICO] ", __VA_ARGS__)
#else
#define DEBUGLOGRPICO(...)
#endif

#include "submodule_rpico_types.h"

class Class_SubRpico : public Class_ProgBase {
public:
	Class_SubRpico(uint8_t in);

	const char* getProgTypePrefix() override { return "rpico"; }
	const char* getChipCfgJsonPath() override { return RPICO_CFG_JSON; }
	bool isFlashBusy() override { return rpicoProg.isFlashBusy(); }
	uint8_t getFlashPercent() override { return rpicoProg.getPercent(); }
	bool chipSpecificInit() override;
	void web_FileUpload2Chip(AsyncWebServerRequest *request) override;
	void onFlashComplete() override;
	void registerCustomRoutes() override;

	String getVersionStr() override { return String(SUBMODULE_RPICO_VERSION_STR); }
	String getGeneratedTime() override { return String(SUBMODULE_RPICO_GENERATED_TIME); }
	String getCommitDateStr() override { return String(SUBMODULE_RPICO_COMMIT_DATE_STR); }

	// RP2040-specific
	void _chipInfoAppendFields(JsonObject &out, JsonObject &chip) override;
	void web_CheckChipStatus(AsyncWebServerRequest *request) override;
	bool chip_IsConnected() override;
	void onChipCheckComplete(uint32_t chipId);
	bool chipCfg_FindById(uint32_t idcode, RpChipConfig_t &cfg);
	void _ensureChipCfgFile();
};

extern Class_SubRpico progRpico;

#endif // _SUBMODULE_RPICO_H
