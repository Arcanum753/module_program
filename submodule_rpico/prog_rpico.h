#ifndef _PROG_RPICO_H
#define _PROG_RPICO_H

// ============================================================
// prog_rpico.h — кооперативный автомат прошивки RP2040 (EERTOS).
// По образцу prog_swd.h, но вместо записи регистров flash-контроллера
// выполняет ROM-функции bootrom через Thumb-блоб в SRAM.
// ============================================================

#include "../module_prog/module_prog.h"
#include "submodule_rpico_types.h"

class Class_SubRpico;
extern Class_SubRpico progRpico;

class ESP_PROGRPICO {
public:
	ESP_PROGRPICO();
#if defined(ESP32)
	void setFs(fs::LittleFSFS *fs);
#endif

	// Подготовка: открыть файл, проверить размер, состояние RP_FLASH_INIT.
	bool startFlash(uint32_t flash_base, String &path, uint32_t flash_size,
	                uint32_t sector_size, uint32_t page_size);
	void beginFlashStep();
	void flashStep();

	inline bool isFlashBusy() const { return _flashState != RP_FLASH_IDLE; }
	inline bool isFlashError() const { return _flashError; }
	inline uint8_t getPercent() const { return _percent; }
	inline float getSpeed() const { return _speed; }
	String getFlashErrorString() const { return _flashErrorString; }
	String getFlashErrorStage() const { return _flashErrorStage; }
	uint8_t getFlashErrorPercent() const { return _flashErrorPercent; }

protected:
	bool _openFile();
	void _closeFile();
	bool _initTarget();
	bool _readChunk();
	bool _transferData();
	bool _programChunk();
	bool _verifyStep();
	void _fail(const String &text, const char *stage);

	RpFlashState _flashState = RP_FLASH_IDLE;
	bool     _flashError = false;
	String   _flashErrorString = "";
	String   _flashErrorStage = "";
	uint8_t  _flashErrorPercent = 0;
	volatile uint8_t _percent = 0;
	volatile float _speed = 0;
	bool     _targetReady = false;

	File     _flashFile;
	String   _flashPath;
	uint32_t _flashBase    = RP2040_FLASH_BASE;
	uint32_t _flashSize    = RP2040_FLASH_SIZE;
	uint32_t _sectorSize   = RP2040_SECTOR_SIZE;
	uint32_t _pageSize     = RP2040_PAGE_SIZE;
	uint32_t _fileSize     = 0;
	uint32_t _written      = 0;
	uint32_t _chunkOffset  = 0;
	uint32_t _chunkRawLen  = 0;
	uint32_t _chunkLen     = 0;
	uint32_t _dataSent     = 0;
	uint32_t _verifyPos    = 0;
	uint32_t _startTime    = 0;
	uint32_t _romFn[RP2040_ROM_FUNC_COUNT] = {0, 0, 0, 0, 0, 0};

	uint8_t  _chunkBuf[RPICO_CHUNK_SIZE];
	uint8_t  _verifyBuf[256];

#if defined(ESP32)
	fs::LittleFSFS *_fs = nullptr;
#endif
};

// EERTOS-обёртка (определена в prog_rpico.cpp)
void rpico_flash_step_task_wrapper();

// Глобальный объект (определён в submodule_rpico.cpp)
extern ESP_PROGRPICO rpicoProg;

#endif // _PROG_RPICO_H
