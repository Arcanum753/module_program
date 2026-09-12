#ifndef _MODULE_PROG_TYPES_h
#define _MODULE_PROG_TYPES_h

// ============================================================
// module_prog_types.h — типы, структуры и define'ы базового класса
// программатора Class_ProgBase.
// Реализация: module_prog.cpp (шаблон) и module_prog_engine.cpp
// (cfg/filelist/upload/md5/migration).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#define CONFIG_PROG_JSON      "/config_prog.json"
#define PROG_FILELIST_JSON    "/prog_filelist.json"

#define  FILE_TYPE_COMMA            '.'
#define  FILE_TYPE_HEX              "hex"
#define  FILE_TYPE_BIN              "bin"
#define  FILE_TYPE_BINARY           "binary"

#define DEFAULT_PROG_PROJNAME       "projname"
#define DEFAULT_CHIP_NAME           ""
#define PROJECT_NAME_MAX_LEN        16

#define CHIP_STATUS_TIMEOUT     300

#define JSON_STR_LEN			512
#define JSON_FILESIZEMAX		1024

#define MAX_FILENAME_LEN		30

// Таймаут отсутствия данных при загрузке файла в FS (мс)
#define UPLOAD_TIMEOUT_MS 30000

typedef enum progerr_e  {
	ERROR_OK = 0,
	ERR_SIGN = -1,
	ERR_BUSY = -2,
	ERR_FLASH = -3,
	ERR_ERASE = -4,
	ERR_HEX = -5,
	ERR_CFG = -6,
	ERR_OPENFILE = -8,
	ERR_INCORRECTFILE = -9,
	ERR_NOFILE = -10,
	ERR_HEXCRC = -11,
	ERR_HEXADDR = -12,
	ERR_HEXMEMOVER = -13,
	ERR_CHIP_OFFLINE = -14,
	ERR_CHIP_MISMATCH = -15,
	ERR_CHIP_NOT_IN_CFG = -16,
} progerr_t;

typedef struct {
    String project_name;
    String chip_name;
} CfgFile_ProgBase_t;

#endif // _MODULE_PROG_TYPES_h
