#include <cstddef>
#include <cstring>
#include <vector>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <LittleFS.h>
#include "esp_rom_md5.h"
#endif

#include "module_prog.h"

// ============================================================
// Конфиг
// ============================================================

void Class_ProgBase::cfg_SetDefault() {
	DEBUGLOGPROG(__PRETTY_FUNCTION__);	DEBUGLOGPROG("\r\n");
	CfgFile_Prog.project_name  = DEFAULT_PROG_PROJNAME;
	CfgFile_Prog.chip_name     = DEFAULT_CHIP_NAME;
}

bool Class_ProgBase::cfg_FileLoad() {
	DEBUGLOGPROG(__PRETTY_FUNCTION__); DEBUGLOGPROG("\r\n");
	JsonDocument doc;
	if (!core_json.jsonFileLoadDoc(CONFIG_PROG_JSON, doc)) return false;
	CfgFile_Prog.project_name = doc["project"].as<String>();
	CfgFile_Prog.chip_name = doc["chip_name"].as<String>();
	return true;
}

bool Class_ProgBase::cfg_FileSave(){
	DEBUGLOGPROG("Save config PROJ\r\n");
	JsonDocument doc;
	core_json.jsonFileLoadDoc(CONFIG_PROG_JSON, doc);
	doc["project"] = CfgFile_Prog.project_name;
	doc["chip_name"] = CfgFile_Prog.chip_name;
	return core_json.jsonFileSaveDoc(CONFIG_PROG_JSON, doc);
}

int Class_ProgBase::cfg_FileStructGet(CfgFile_ProgBase_t &_inStruct)  {
	DEBUGLOGPROG(__PRETTY_FUNCTION__); DEBUGLOGPROG("\r\n");
	progerr_t _ret = ERROR_OK;
	if(!cfg_FileLoad()) {  return ERR_CFG; }
	_inStruct = CfgFile_Prog;
	return _ret ;
}

int Class_ProgBase::cfg_FileSaveFromWeb(CfgFile_ProgBase_t &_inStruct)  {
	DEBUGLOGPROG(__PRETTY_FUNCTION__);	DEBUGLOGPROG("\r\n");
	CfgFile_Prog = _inStruct;
	bool ret = cfg_FileSave();
	if (ret) {
		return 0;
	}
	return 1;
}

// ============================================================
// Disk Info / Files List
// ============================================================

bool Class_ProgBase::web_GetDiskInfoExe(String &_str)	{
	bool _ret = true;
	String values = "";
	size_t sizeAll = 0;
	size_t sizeUsed = 0;
	if (_fs != nullptr) {
#if defined(ESP32)
		esp_task_wdt_reset();
		sizeAll = _fs->totalBytes();
		sizeUsed = _fs->usedBytes();
		esp_task_wdt_reset();
#endif
	}

	size_t sizeFree = 0;
	if (sizeAll > sizeUsed) { sizeFree = sizeAll - sizeUsed; }

	values += "diskall|"   + (String)(sizeAll)  + "|div\n";
	values += "diskused|"  + (String)(sizeUsed) + "|div\n";
	values += "diskfree|"  + (String)(sizeFree) + "|div\n";
	_str = values;
	return _ret;
}

bool Class_ProgBase::web_GetFilesListExe(String &_str)	{
	bool _ret = true;

	JsonDocument listDoc;
	bool listLoaded = filelist_EnsureLoaded(listDoc);
	JsonArray arr;
	String lastSuccessFilename = "";
	if (listLoaded) {
		arr = listDoc.as<JsonArray>();
		lastSuccessFilename = filelist_GetLastSuccessFilename();
	}

	std::vector<String> fileNames;
	std::vector<String> fileTypes;
	std::vector<size_t> fileSizes;

	if (_fs == nullptr) { _ret = false; }
	else {
#if defined(ESP32)
		File root = _fs->open("/");
		if (root) {
			File files = root.openNextFile();
			while (files) {
				std::string fname = files.name();
				size_t pos = fname.find_last_of(FILE_TYPE_COMMA);
				std::string ftype = fname.substr(pos + 1);
				if ((ftype == FILE_TYPE_BINARY) || (ftype == FILE_TYPE_BIN) || (ftype == FILE_TYPE_HEX)) {
					fileNames.push_back(String(fname.c_str()));
					fileTypes.push_back(String(ftype.c_str()));
					fileSizes.push_back((size_t)files.size());
				}
				files = root.openNextFile();
				esp_task_wdt_reset();
			}
		}
		esp_task_wdt_reset();
#endif
	}

	String json = "[";
	for (size_t i = 0; i < fileNames.size(); i++) {
		const String& fname = fileNames[i];
		const String& ftype = fileTypes[i];
		size_t fsize = fileSizes[i];

		String fileMD5 = "";
		String uploadDate = "";
		String progDate = "";
		String progStatus = "";
		String progTime = "";
		String progError = "";
		String progErrorStage = "";
		String progErrorPercent = "";
		String progSpeed = "";
		if (listLoaded) {
			for (JsonObject entry : arr) {
				if (strcmp(entry["filename"].as<const char*>(), fname.c_str()) == 0) {
					fileMD5 = entry["md5"].as<const char*>();
					uploadDate = entry["upload_date"].as<const char*>();
					progDate = entry["prog_date"].as<const char*>();
					progStatus = entry["prog_status"].as<const char*>();
					const char* pt = entry["prog_time"].as<const char*>();
					if (pt) progTime = String(pt);
					const char* pe = entry["prog_error"].as<const char*>();
					if (pe) progError = String(pe);
					const char* pes = entry["prog_error_stage"].as<const char*>();
					if (pes) progErrorStage = String(pes);
					const char* pep = entry["prog_error_percent"].as<const char*>();
					if (pep) progErrorPercent = String(pep);
					const char* ps = entry["prog_speed"].as<const char*>();
					if (ps) progSpeed = String(ps);
					break;
				}
			}
		}

		bool isLastSuccess = (lastSuccessFilename.length() > 0 && strcmp(fname.c_str(), lastSuccessFilename.c_str()) == 0);

		if (i > 0) json += ",";
		json += "{";
		json +=  "\"filename\":\""; 	json += fname;				json += "\"";
		json += ",\"filetype\":\""; 	json += ftype;				json += "\"";
		json += ",\"filesizestr\":\"";	json += formatBytes(fsize); json += "\"";
		json += ",\"filesizebyte\":\"";	json += (String)fsize;		json += "\"";
		json += ",\"progchip\":\"";									json += "\"";
		json += ",\"progactual\":\"";								json += "\"";
		json += ",\"upload_date\":\"";	json += uploadDate;			json += "\"";
		json += ",\"prog_date\":\"";	json += progDate;			json += "\"";
		json += ",\"prog_status\":\"";	json += progStatus;			json += "\"";
		json += ",\"prog_time\":\"";	json += progTime;			json += "\"";
		json += ",\"prog_error\":\"";	json += progError;			json += "\"";
		json += ",\"prog_error_stage\":\"";	json += progErrorStage;		json += "\"";
		json += ",\"prog_error_percent\":\""; json += progErrorPercent;	json += "\"";
		json += ",\"prog_speed\":\"";		json += progSpeed;			json += "\"";
		json += ",\"md5\":\"";			json += fileMD5;			json += "\"";
		json += ",\"is_last_success\":"; json += (isLastSuccess ? "true" : "false");
		json += "}";
	}

	json += "]";
	_str = json;
	return _ret;
}

// ============================================================
// Filelist Management
// ============================================================

bool Class_ProgBase::filelist_EnsureLoaded(JsonDocument &doc) {
	if (!_fs) return false;
	if (!_fs->exists(PROG_FILELIST_JSON)) {
		doc.clear();
		doc.to<JsonArray>();
		return core_json.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
	}
	if (!core_json.jsonFileLoadDoc(PROG_FILELIST_JSON, doc)) {
		doc.clear();
		doc.to<JsonArray>();
		core_json.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
		return false;
	}
	if (!doc.is<JsonArray>()) {
		doc.clear();
		doc.to<JsonArray>();
	}
	return true;
}

void Class_ProgBase::filelist_Clear() {
	JsonDocument doc;
	doc.to<JsonArray>();
	core_json.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
}

bool Class_ProgBase::filelist_AddEntry(const String &filename, const String &upload_date, const String &md5) {
	JsonDocument doc;
	filelist_EnsureLoaded(doc);
	JsonArray arr = doc.as<JsonArray>();

	String normalizedName = filename;
	if (normalizedName.startsWith("/")) {
		normalizedName = normalizedName.substring(1);
	}

	for (JsonObject entry : arr) {
		if (strcmp(entry["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
			entry["upload_date"] = upload_date;
			entry["md5"] = md5;
			return core_json.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
		}
	}

	JsonObject newEntry = arr.add<JsonObject>();
	newEntry["filename"] = normalizedName;
	newEntry["upload_date"] = upload_date;
	newEntry["md5"] = md5;

	return core_json.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
}

bool Class_ProgBase::filelist_SetProgStatus(const String &filename, const String &prog_date, const String &prog_status, const String &prog_error, const String &prog_time, const String &prog_error_stage, const String &prog_error_percent, const String &prog_speed) {
	JsonDocument doc;
	filelist_EnsureLoaded(doc);
	JsonArray arr = doc.as<JsonArray>();

	String normalizedName = filename;
	if (normalizedName.startsWith("/")) {
		normalizedName = normalizedName.substring(1);
	}

	for (JsonObject entry : arr) {
		if (strcmp(entry["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
			entry["prog_date"] = prog_date;
			entry["prog_status"] = prog_status;
		if (prog_time.length() > 0) {
			entry["prog_time"] = prog_time;
		} else {
			entry.remove("prog_time");
		}
		if (prog_speed.length() > 0) {
			entry["prog_speed"] = prog_speed;
		} else {
			entry.remove("prog_speed");
		}
			if (prog_error.length() > 0) {
				entry["prog_error"] = prog_error;
			} else {
				entry.remove("prog_error");
			}
			if (prog_error_stage.length() > 0) {
				entry["prog_error_stage"] = prog_error_stage;
			} else {
				entry.remove("prog_error_stage");
			}
			if (prog_error_percent.length() > 0) {
				entry["prog_error_percent"] = prog_error_percent;
			} else {
				entry.remove("prog_error_percent");
			}
			return core_json.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
		}
	}

	JsonObject newEntry = arr.add<JsonObject>();
	newEntry["filename"] = normalizedName;
	newEntry["prog_date"] = prog_date;
	newEntry["prog_status"] = prog_status;
	if (prog_time.length() > 0) {
		newEntry["prog_time"] = prog_time;
	}
	if (prog_error.length() > 0) {
		newEntry["prog_error"] = prog_error;
	}
	if (prog_error_stage.length() > 0) {
		newEntry["prog_error_stage"] = prog_error_stage;
	}
	if (prog_error_percent.length() > 0) {
		newEntry["prog_error_percent"] = prog_error_percent;
	}
	if (prog_speed.length() > 0) {
		newEntry["prog_speed"] = prog_speed;
	}
	DEBUGLOGPROG("filelist_SetProgStatus: created new entry for %s (was not in filelist)\r\n", normalizedName.c_str());
	return core_json.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
}

String Class_ProgBase::filelist_GetLastSuccessFilename() {
	JsonDocument doc;
	if (!filelist_EnsureLoaded(doc)) return "";
	JsonArray arr = doc.as<JsonArray>();

	String bestFilename = "";
	String bestDate = "";
	for (JsonObject entry : arr) {
		const char* status = entry["prog_status"].as<const char*>();
		if (status && strcmp(status, "ok") == 0) {
			const char* date = entry["prog_date"].as<const char*>();
			if (date && strlen(date) > 0) {
				if (bestDate.length() == 0 || strcmp(date, bestDate.c_str()) > 0) {
					bestDate = String(date);
					bestFilename = entry["filename"].as<const char*>();
				}
			}
		}
	}
	return bestFilename;
}

bool Class_ProgBase::filelist_RemoveEntry(const String &filename) {
	JsonDocument doc;
	filelist_EnsureLoaded(doc);
	JsonArray arr = doc.as<JsonArray>();

	String normalizedName = filename;
	if (normalizedName.startsWith("/")) {
		normalizedName = normalizedName.substring(1);
	}

	int idx = -1;
	for (size_t i = 0; i < arr.size(); i++) {
		if (strcmp(arr[i]["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
			idx = (int)i;
			break;
		}
	}

	if (idx < 0) return false;

	arr.remove(idx);
	return core_json.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
}

bool Class_ProgBase::filelist_FileExists(const String &filename) {
	JsonDocument doc;
	filelist_EnsureLoaded(doc);
	JsonArray arr = doc.as<JsonArray>();

	String normalizedName = filename;
	if (normalizedName.startsWith("/")) {
		normalizedName = normalizedName.substring(1);
	}

	for (JsonObject entry : arr) {
		if (strcmp(entry["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
			return true;
		}
	}
	return false;
}

String Class_ProgBase::file_ComputeMD5(const String &path) {
	if (!_fs || !_fs->exists(path)) {
		DEBUGLOGPROG("file_ComputeMD5: file not found %s\r\n", path.c_str());
		return "";
	}

	File f = _fs->open(path, "r");
	if (!f) {
		DEBUGLOGPROG("file_ComputeMD5: cannot open %s\r\n", path.c_str());
		return "";
	}

#if defined(ESP32)
	md5_context_t md5Ctx;
	esp_rom_md5_init(&md5Ctx);

	uint8_t buf[256];
	size_t bytesRead;
	while ((bytesRead = f.read(buf, sizeof(buf))) > 0) {
		esp_rom_md5_update(&md5Ctx, buf, bytesRead);
	}

	uint8_t hash[16];
	esp_rom_md5_final(hash, &md5Ctx);
	f.close();

	char hex[33];
	for (int i = 0; i < 16; i++) {
		sprintf(hex + i * 2, "%02x", hash[i]);
	}
	hex[32] = '\0';

	DEBUGLOGPROG("file_ComputeMD5: %s -> %s\r\n", path.c_str(), hex);
	return String(hex);
#endif
}

// Переопределяется в субмодулях для добавления специфичных полей (signature, family, ...)
void Class_ProgBase::_chipInfoAppendFields(JsonObject &out, JsonObject &chip) {
}

// ============================================================
// Cleanup / Migration
// ============================================================

void Class_ProgBase::_cleanupStaleUpload() {
	if (_fsUploadFile) {
		_fsUploadFile.close();
		_fsUploadFile = File();
	}
	if (_uploadFilename.length() > 0) {
		DEBUGLOGPROG("Cleanup: removing stale upload file %s\r\n", _uploadFilename.c_str());
		if (_fs && _fs->exists(_uploadFilename)) {
			_fs->remove(_uploadFilename);
		}
		filelist_RemoveEntry(_uploadFilename);
		_uploadFilename = "";
	}
	_fileUploadBytes = 0;
	_fileUploadError = false;
	_uploadPercent = 0;
	_uploadLastChunkTime = 0;
}

// Миграция старых файлов конфигов и filelist в новые пути
void Class_ProgBase::_migrateOldFiles() {
	if (!_fs) return;

	// Старые пути config (ISPs и SWD)
	const char* oldConfigs[] = {"/config_prog_isp.json", "/config_prog_swd.json"};
	const char* oldFilelists[] = {"/isp_filelist.json", "/swd_filelist.json"};

	// Миграция конфига
	if (!_fs->exists(CONFIG_PROG_JSON)) {
		for (auto oldPath : oldConfigs) {
			if (_fs->exists(oldPath)) {
				DEBUGLOGPROG("_migrateOldFiles: migrating config %s -> %s\r\n", oldPath, CONFIG_PROG_JSON);
				File src = _fs->open(oldPath, "r");
				if (src) {
					JsonDocument doc;
					DeserializationError err = deserializeJson(doc, src);
					src.close();
					if (!err) {
						File dst = _fs->open(CONFIG_PROG_JSON, "w");
						if (dst) {
							serializeJson(doc, dst);
							dst.flush();
							dst.close();
							_fs->remove(oldPath);
							DEBUGLOGPROG("_migrateOldFiles: config migrated and old file removed\r\n");
						}
					}
				}
				break;
			}
		}
	}

	// Миграция filelist
	if (!_fs->exists(PROG_FILELIST_JSON)) {
		for (auto oldPath : oldFilelists) {
			if (_fs->exists(oldPath)) {
				DEBUGLOGPROG("_migrateOldFiles: migrating filelist %s -> %s\r\n", oldPath, PROG_FILELIST_JSON);
				File src = _fs->open(oldPath, "r");
				if (src) {
					JsonDocument doc;
					DeserializationError err = deserializeJson(doc, src);
					src.close();
					if (!err && doc.is<JsonArray>()) {
						File dst = _fs->open(PROG_FILELIST_JSON, "w");
						if (dst) {
							serializeJson(doc, dst);
							dst.flush();
							dst.close();
							_fs->remove(oldPath);
							DEBUGLOGPROG("_migrateOldFiles: filelist migrated and old file removed\r\n");
						}
					}
				}
				break;
			}
		}
	}
}
