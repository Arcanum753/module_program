#include "core_web/FSWebServerLib.h"

#include <cstring>
#include <Arduino.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

#include "prog_rpico.h"
#include "submodule_rpico.h"
#include "rp2040_swd.h"
#include "../module_prog/format_bin.h"
#include "core_sys/eertos.h"

// ============================================================
// Кооперативная прошивка RP2040 (EERTOS)
// ============================================================

ESP_PROGRPICO::ESP_PROGRPICO() {
}

#if defined(ESP32)
void ESP_PROGRPICO::setFs(fs::LittleFSFS *fs) {
	_fs = fs;
}
#endif

bool ESP_PROGRPICO::startFlash(uint32_t flash_base, String &path, uint32_t flash_size,
                               uint32_t sector_size, uint32_t page_size) {
	if (isFlashBusy()) return false;
	if (!_fs) return false;

	if (!path.startsWith("/")) path = "/" + path;
	_closeFile();

	_flashPath  = path;
	_flashBase  = flash_base ? flash_base : RP2040_FLASH_BASE;
	_flashSize  = flash_size ? flash_size : RP2040_FLASH_SIZE;
	_sectorSize = sector_size ? sector_size : RP2040_SECTOR_SIZE;
	_pageSize   = page_size ? page_size : RP2040_PAGE_SIZE;

	_fileSize        = 0;
	_written         = 0;
	_percent         = 0;
	_speed           = 0;
	_chunkOffset     = 0;
	_chunkRawLen     = 0;
	_chunkLen        = 0;
	_dataSent        = 0;
	_verifyPos       = 0;
	_startTime       = 0;
	_flashError      = false;
	_flashErrorString = "";
	_flashErrorStage  = "";
	_flashErrorPercent = 0;
	_targetReady     = false;
	memset(_romFn, 0, sizeof(_romFn));
	memset(_chunkBuf, 0xFF, sizeof(_chunkBuf));

	if (!_openFile()) {
		DEBUGLOGRPICO("startFlash: cannot open %s\r\n", _flashPath.c_str());
		return false;
	}
	_fileSize = binFileGetSize(_flashFile);
	if (_fileSize == 0) {
		DEBUGLOGRPICO("startFlash: empty file %s\r\n", _flashPath.c_str());
		_closeFile();
		return false;
	}
	if (_fileSize > _flashSize) {
		DEBUGLOGRPICO("startFlash: file size %u > flash size %u\r\n", _fileSize, _flashSize);
		_closeFile();
		return false;
	}

	DEBUGLOGRPICO("startFlash: %s, %u bytes, flash 0x%08x..0x%08x\r\n",
		_flashPath.c_str(), _fileSize, _flashBase, _flashBase + _flashSize);
	_flashState = RP_FLASH_INIT;
	return true;
}

void ESP_PROGRPICO::beginFlashStep() {
	SetTask(rpico_flash_step_task_wrapper);
}

void rpico_flash_step_task_wrapper() {
	rpicoProg.flashStep();
	if (rpicoProg.isFlashBusy()) {
		SetTask(rpico_flash_step_task_wrapper);
	}
}

bool ESP_PROGRPICO::_openFile() {
	if (!_fs) return false;
	_flashFile = binFileOpen(*_fs, _flashPath);
	return (bool)_flashFile;
}

void ESP_PROGRPICO::_closeFile() {
	if (_flashFile) {
		binFileClose(_flashFile);
	}
}

bool ESP_PROGRPICO::_initTarget() {
	uint32_t idcode = rp2040_connect();
	if (idcode == 0) {
		_flashErrorString = "RP2040 not detected (SWD offline)";
		DEBUGLOGRPICO("init: %s\r\n", _flashErrorString.c_str());
		return false;
	}
	if (idcode != RP2040_DPIDR) {
		char buf[64];
		snprintf(buf, sizeof(buf), "Unexpected DPIDR 0x%08x (expected 0x%08x)",
			idcode, (uint32_t)RP2040_DPIDR);
		_flashErrorString = buf;
		DEBUGLOGRPICO("init: %s\r\n", buf);
		return false;
	}
	if (!rp2040_halt()) {
		_flashErrorString = "Core did not halt";
		return false;
	}
	if (!rp2040_disable_irqs()) {
		_flashErrorString = "Failed to disable target IRQs";
		return false;
	}
	if (!rp2040_load_algo()) {
		_flashErrorString = "Failed to load flash algorithm to SRAM";
		return false;
	}
	if (!rp2040_resolve_rom(_romFn)) {
		_flashErrorString = "Failed to resolve bootrom flash functions";
		return false;
	}
	_targetReady = true;
	return true;
}

bool ESP_PROGRPICO::_readChunk() {
	uint32_t want = _fileSize - _written;
	if (want > RPICO_CHUNK_SIZE) want = RPICO_CHUNK_SIZE;

	memset(_chunkBuf, 0xFF, sizeof(_chunkBuf));
	uint32_t got = _flashFile.read(_chunkBuf, want);
	if (got != want) {
		_fail("Read firmware file failed", "READ");
		return false;
	}

	_chunkOffset = _written;
	_chunkRawLen = got;
	_chunkLen = ((got + _pageSize - 1) / _pageSize) * _pageSize;
	if (_chunkLen == 0 || _chunkLen > RPICO_CHUNK_SIZE) {
		_fail("Invalid chunk length", "READ");
		return false;
	}
	_dataSent = 0;
	return true;
}

bool ESP_PROGRPICO::_transferData() {
	uint32_t n = _chunkLen - _dataSent;
	if (n > RPICO_DATA_SUBSTEP) n = RPICO_DATA_SUBSTEP;

	if (!rp2040_write_block(RPICO_SRAM_DATA + _dataSent, _chunkBuf + _dataSent, n)) {
		_fail("SWD write to target SRAM failed", "WRITE");
		return false;
	}
	_dataSent += n;
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
	return true;
}

bool ESP_PROGRPICO::_programChunk() {
	if (!rp2040_erase(_chunkOffset, _sectorSize)) {
		_fail("Flash erase failed at 0x" + String(_chunkOffset, HEX), "ERASE");
		return false;
	}
	if (!rp2040_program(_chunkOffset, _chunkLen, RPICO_SRAM_DATA)) {
		_fail("Flash program failed at 0x" + String(_chunkOffset, HEX), "FLASH_WRITE");
		return false;
	}

	_written += _chunkRawLen;
	_percent = (uint8_t)(((uint64_t)_written * 100ULL) / (uint64_t)_fileSize);
	progRpico.setUploadPercent(_percent);
	DEBUGLOGRPICO("chunk 0x%06x raw=%u prog=%u -> %u%%\r\n",
		_chunkOffset, _chunkRawLen, _chunkLen, _percent);

	_chunkRawLen = 0;
	_chunkLen = 0;
	_dataSent = 0;
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
	return true;
}

bool ESP_PROGRPICO::_verifyStep() {
	uint32_t want = _fileSize - _verifyPos;
	if (want > sizeof(_verifyBuf)) want = sizeof(_verifyBuf);

	uint32_t got = _flashFile.read(_chunkBuf, want);
	if (got != want) {
		_fail("Read firmware file during verify failed", "VERIFY");
		return false;
	}

	uint32_t abs_addr = _flashBase + _verifyPos;
	if (!rp2040_read_flash(abs_addr, _verifyBuf, want)) {
		_fail("Read flash during verify failed", "VERIFY");
		return false;
	}

	for (uint32_t i = 0; i < want; i++) {
		if (_chunkBuf[i] != _verifyBuf[i]) {
			char msg[80];
			snprintf(msg, sizeof(msg), "Verify mismatch at 0x%08x (file 0x%02x, flash 0x%02x)",
				abs_addr + i, _chunkBuf[i], _verifyBuf[i]);
			_fail(msg, "VERIFY");
			return false;
		}
	}

	_verifyPos += got;
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
	return true;
}

void ESP_PROGRPICO::_fail(const String &text, const char *stage) {
	_flashError = true;
	_flashErrorString = text;
	_flashErrorStage = stage;
	_flashErrorPercent = _percent;
	_flashState = RP_FLASH_DONE;
	DEBUGLOGRPICO("ERROR [%s]: %s\r\n", stage, text.c_str());
}

void ESP_PROGRPICO::flashStep() {
	switch (_flashState) {
		case RP_FLASH_INIT: {
			if (!_initTarget()) {
				if (!_flashError) _fail(_flashErrorString, "FLASH_INIT");
				break;
			}
			_startTime = millis();
			_flashState = RP_FLASH_WRITE;
			DEBUGLOGRPICO("init done, start write\r\n");
			break;
		}

		case RP_FLASH_WRITE: {
			if (_written >= _fileSize) {
				// Прошивка завершена. Перечитать нельзя без переоткрытия файла.
				_closeFile();
				_verifyPos = 0;
				if (!_openFile()) {
					_fail("Cannot reopen firmware file for verify", "VERIFY");
					break;
				}
				_flashState = RP_FLASH_VERIFY;
				break;
			}
			if (_chunkLen == 0) {
				if (!_readChunk()) break;
			}
			if (_dataSent < _chunkLen) {
				_transferData();
				break;  // одна порция данных за квант EERTOS
			}
			_programChunk();
			break;
		}

		case RP_FLASH_VERIFY: {
			if (_verifyPos >= _fileSize) {
				_closeFile();
				_flashState = RP_FLASH_DONE;
				break;
			}
			_verifyStep();
			break;
		}

		case RP_FLASH_DONE: {
			if (_targetReady) {
				rp2040_reset_target();
			}
			_closeFile();

			uint32_t elapsed = (_startTime > 0) ? (millis() - _startTime) : 0;
			if (!_flashError && elapsed > 0 && _written > 0) {
				// байты/мс = КБ/с
				_speed = (float)_written / (float)elapsed;
			}

			_flashState = RP_FLASH_IDLE;
			DEBUGLOGRPICO("done: %s, %u bytes, %u ms\r\n",
				_flashError ? "error" : "ok", _written, elapsed);
			progRpico.onFlashComplete();
			break;
		}

		case RP_FLASH_IDLE:
		default:
			break;
	}
}
