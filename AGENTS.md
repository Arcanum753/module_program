# module_program — программатор AVR (ISP) и STM32 (SWD)

> **Опциональный модуль.** Подключается через `src_filter` + `build_flags`.
> **Работоспособен только в составе сборки, содержащей ядро** (см. `../../TRS.md` §2.1).

Один git-репозиторий, включающий три компонента: общий контейнер `module_prog/`
(`Class_ProgBase`), AVR ISP-субмодуль `submodule_isp/` (`Class_SubIsp`) и STM32 SWD-субмодуль
`submodule_swd/` (`Class_SubSwd`). Все инструкции — общий контейнер, ISP-специфика,
SWD-специфика и руководства пользователя обоими программаторами — собраны в этом файле.

- **Репозиторий:** https://github.com/Arcanum753/module_program
- **Папка:** `src/module_program/`
- **Флаг активации:** `-D PROGTYPE_ISP` или `-D PROGTYPE_SWD` (выбор субмодуля — через `src_filter`, а не через `#if PROGTYPE_*`)
- **Registry:** `object=progIsp`, `define=PROGTYPE_ISP` (ISP) либо `object=progSwd`, `define=PROGTYPE_SWD` (SWD); `web=1`, `loop=0`; без `namespace`/`res`/`prio`
- **Только для платформы:** обе; env — `esp32-isp` / `esp32-swd`
- **Зависит от модулей:** —
- **Зависит от ядра:** `core_web`, `core_sys`, `core_json`, `core_ntp`, `core_led`, `core_state`

## Состав

| Компонент | Каталог | Класс / объект | Назначение |
|-----------|---------|----------------|------------|
| `module_prog` | `src/module_program/module_prog/` | `Class_ProgBase` (абстрактный), объекта нет | общая база: проектная конфигурация, список файлов, загрузка в FS, диск, статус чипа |
| `submodule_isp` | `src/module_program/submodule_isp/` | `Class_SubIsp : public Class_ProgBase`, `progIsp` | программирование AVR по ISP |
| `submodule_swd` | `src/module_program/submodule_swd/` | `Class_SubSwd : public Class_ProgBase`, `progSwd` | программирование STM32 по SWD |

# module_prog — база программатора

**Каталог:** `src/module_program/module_prog/` · **Класс:** `Class_ProgBase` (абстрактный) ·
**Объект:** отсутствует · **`.ini`:** отсутствует (обрабатывается генератором как модуль без ini).

**Назначение:** общая база субмодулей программатора. Владеет проектной конфигурацией,
списком файлов прошивок, загрузкой файлов в FS (MD5 + размер), информацией о диске, опросом
статуса чипа, версионной точкой. Работа с чипом делегирована виртуальным методам.

## Функциональные требования (module_prog)

- FR-MODPROG-1: Абстрактный интерфейс для наследников: `getProgTypePrefix`, `isFlashBusy`, `getFlashPercent`, `chipSpecificInit`, `web_FileUpload2Chip`, `onFlashComplete`, `getChipCfgJsonPath`.
- FR-MODPROG-2: Конфиг `/config_prog.json` (`project`, `chip_name`), API `cfg_FileStructGet/SaveFromWeb/SetDefault/FileLoad/FileSave`.
- FR-MODPROG-3: Список `/prog_filelist.json` (`filename`, `upload_date`, `md5`, `prog_date`, `prog_status`, `prog_time`, `prog_error`, `prog_error_stage`, `prog_error_percent`, `prog_speed`).
- FR-MODPROG-4: Загрузка файлов в FS с MD5 (`web_FileUpload2FS`, `file_ComputeMD5`), удаление (`web_FileDelete`), информация о диске (`web_GetDiskInfo`).
- FR-MODPROG-5: Миграция старых путей (`config_prog_isp.json`, `config_prog_swd.json`, `isp_filelist.json`, `swd_filelist.json`).
- FR-MODPROG-6: Коды ошибок `progerr_t`: `-1` SIGN, `-2` BUSY, `-3` FLASH, `-4` ERASE, `-5` HEX, `-6` CFG, `-8` OPENFILE, `-9` INCORRECTFILE, `-10` NOFILE, `-11` HEXCRC, `-12` HEXADDR, `-13` HEXMEMOVER, `-14` CHIP_OFFLINE, `-15` CHIP_MISMATCH, `-16` CHIP_NOT_IN_CFG.

# Формат-хелперы программатора

**Каталоги:** `module_prog/format_hex.*`, `module_prog/format_bin.*`

- FR-FMT-1: Intel HEX — `hexFileLineParser`, `hexFileParseStream` (валидация), `hexFileParseStreamWrite` (потоковая запись через callback), `hexFileIsFormat`, `hexFileGetBinarySize`.
- FR-FMT-2: Проверки HEX: CRC, монотонность адресов, переполнение памяти чипа (коды −9/−10/−11/−12/−13), ошибка записи −14.
- FR-FMT-3: BIN — `binFileOpen/GetSize/ReadPage/Close/IsFormat` (`.bin`/`.binary`).
- FR-FMT-4: Формат определяется по расширению в `startFlash()`; неизвестное расширение — BIN.

# submodule_isp — программатор AVR ISP

**Каталог:** `src/module_program/submodule_isp/` · **Класс:** `Class_SubIsp : public Class_ProgBase` · **Объект:** `progIsp`
**`[registry]`:** `object=progIsp`, `define=PROGTYPE_ISP`, `web=1`, `loop=0`.

**Назначение:** программирование AVR (STK500-совместимое, bit-bang SPI + reset), чтение
сигнатуры и фьюзов, запись фьюзов, поиск чипа в базе, автомат определения чипа. Обёртка над
`ESP_AVRISP avrprog`.

## Функциональные требования (ISP)

- FR-ISP-1: Читать подпись чипа (`chipSignRead`) и находить в базе `/avrisp_cfg.json` по `signature` (`chipCfg_FindBySignature`).
- FR-ISP-2: Сверять найденный чип с `chip_name` из `/config_prog.json`; при несовпадении — `HTTP 423`/`ERR_CHIP_MISMATCH`.
- FR-ISP-3: Для неизвестной сигнатуры без заданного имени — по умолчанию flash 32768 Б, page 128 Б.
- FR-ISP-4: Прошивать HEX или BIN постранично, кооперативно (`startFlash` + `beginFlashStep`; `FLASH_IDLE/INIT/WRITE/DONE`), с прогрессом (`getPercent`) и верификацией (`chipFlashVerification`).
- FR-ISP-5: Чтение/запись фьюзов (`chipFusesRead`, `chipFusesWrite`).
- FR-ISP-6: Определять чип с повторами (15 попыток; `CHIP_IDLE/INIT/PROBE/DONE`).
- FR-ISP-7: База `/avrisp_cfg.json` — `chips[]`: `signature`, `name`, `flash_size`, `page_size`; поставка: ATmega328P (`0x1E950F`, 32768/128), ATmega168P (`0x1E940B`, 16384/128).
- FR-ISP-8: Константы: `AVRISP_SPI_FREQLOW=100000`, `AVRISP_SPI_FREQHIGH=500000`, `MEM_PAGE_SIZE=128`.
- FR-ISP-9: Выводы по умолчанию: `PIN_MISO=19`, `PIN_MOSI=23`, `PIN_SCK=18`, `PIN_RST=5`.

## Подключение AVR чипа. Распиновка

> [!WARNING]
> **ВНИМАНИЕ! ESP И AVR ЧИПЫ ДОЛЖНЫ БЫТЬ СОГЛАСОВАНЫ ПО УРОВНЮ НАПРЯЖЕНИЯ ПИТАНИЯ ИЛИ ДОЛЖЕН ИСПОЛЬЗОВАТЬСЯ СОГЛАСОВАТЕЛЬ НАПРЯЖЕНИЙ СИГНАЛЬНЫХ ЛИНИЙ!**

| ESP8266 | AVR (Atmega328p) |
|:--------|:-----------------|
| G12     | MISO (D6)        |
| G13     | MOSI (D7)        |
| G14     | SCK (D5)         |
| G5      | RST (D1)         |
| GND     | GND              |
| +3.3V   | +3.3V            |

Распиновка приведена на примере чипа Atmega328p. Любой другой AVR чип подключается аналогично на интерфейс **SPI**.

Для ESP32 пины настраиваются через `build_flags`:
```
-D PIN_MISO=19
-D PIN_MOSI=23
-D PIN_SCK=18
-D PIN_RST=5
```

## Редактирование fuse битов

Реализована возможность чтения и редактирования fuse битов подключенного чипа по маске. Редактор доступен на странице `/avr.html`, изначально скрыт (раскрывается чекбоксом "Expand").

## Откат прошивки

В ФС ESP постоянно хранится две версии .hex файла — текущая и предыдущая. По нажатию кнопки "Rollback version" происходит прошивка AVR чипа предыдущей версией.

# submodule_swd — программатор STM32 SWD

**Каталог:** `src/module_program/submodule_swd/` · **Класс:** `Class_SubSwd : public Class_ProgBase` · **Объект:** `progSwd`
**`[registry]`:** `object=progSwd`, `define=PROGTYPE_SWD`, `web=1`, `loop=0`.

**Назначение:** программирование STM32 F1/F4 по SWD (bit-bang SWD поверх GPIO), проверка
IDCODE, mass-erase + program + reset. Обёртка над `ESP_PROGSWD swdprog`.

## Функциональные требования (SWD)

- FR-SWD-1: Читать IDCODE (`swd_init`), находить чип в `/swd_cfg.json` по `idcode` (`chipCfg_FindById`).
- FR-SWD-2: Сверять чип с `chip_name` из `/config_prog.json`.
- FR-SWD-3: Применять параметры чипа (`flash_start`, `flash_size`, `page_size`, `word_size`, `csw_value`); выбирать алгоритм F1/F4 по полю `family`.
- FR-SWD-4: Прошивать HEX потоково или BIN постранично, кооперативно (`startFlash` + `beginFlashStep`), с прогрессом и скоростью (`getSpeed`).
- FR-SWD-5: Выполнять `mass_erase`/`erase_sector`, запись 16/32-битными словами, halt/unhalt/reset.
- FR-SWD-6: Значения по умолчанию: `flash_start=0x08000000`, `page_size=1024`, `word_size=2`, `csw_value=0xA2000002`.
- FR-SWD-7: База `/swd_cfg.json` — `chips[]`: `idcode`, `name`, `family`, `flash_size`, `flash_start`, `page_size`, `word_size`, `csw_value`; поставка: STM32F103C8 (`0x1BA01477`, `stm32f1`, 65536 Б), STM32F411 (`0x2BA01477`, `stm32f4`, 524288 Б).
- FR-SWD-8: Выводы по умолчанию: `SWDPIN_CLK=21`, `SWDPIN_DATA=19`.

## Подключение STM32 чипа. Распиновка

> [!WARNING]
> **ВНИМАНИЕ! ESP И STM32 ЧИПЫ ДОЛЖНЫ БЫТЬ СОГЛАСОВАНЫ ПО УРОВНЮ НАПРЯЖЕНИЯ ПИТАНИЯ ИЛИ ДОЛЖЕН ИСПОЛЬЗОВАТЬСЯ СОГЛАСОВАТЕЛЬ НАПРЯЖЕНИЙ СИГНАЛЬНЫХ ЛИНИЙ!**

| ESP32 | STM32 SWD |
|:-----|:----------|
| G21   | SWCLK     |
| G19   | SWDIO     |
| GND   | GND       |
| +3.3V | +3.3V     |

Пины SWD настраиваются через `build_flags`:
```
-D SWDPIN_CLK=21
-D SWDPIN_DATA=19
```

Поддерживаемые семейства STM32:
- STM32F1 (протестировано)
- STM32F4 (протестировано)

# Веб-интерфейс (общий для ISP и SWD)

Общие маршруты: `/prog/diskinfo`, `/prog/fileslist`, `/prog/delete`, `POST /prog/uploadfile`,
`/prog/uploadstat`, `/prog/setmd5`, `/prog/uploadsize`, `/prog/progress`, `/prog/flash`,
`/prog/ver`, `/prog/chipstatus`, `/project/info`, `/project/save`, `/project/chips`,
`/project/chipinfo`, `GET /project`, `GET /prog`. Все защищены `checkAuth`.

ISP-специфичные маршруты: `GET /avr/fuseread`, `POST /avr/fusewrite`, `/avr/info`,
`POST /avr/save`, `GET /avr/readsignature`, `GET /avrcfg`.

SWD-специфичных маршрутов нет — используются только общие маршруты `module_prog`.

Веб-файлы: `_menu.html`, `prog.html`, `project.html`, `avrcfg.html`, `config_prog.json`,
`avrisp_cfg.json`, `swd_cfg.json`, `avrisp.json` (устаревший); shared `spark-md5.js` —
из `core_ota`.

# Конфигурация

| Файл | Владелец | Основные поля |
|------|----------|---------------|
| `/config_prog.json` | `module_prog` | `project`, `chip_name` (один файл для ISP и SWD) |
| `/prog_filelist.json` | `module_prog` | `filename`, `upload_date`, `md5`, `prog_date`, `prog_status`, `prog_time`, `prog_error`, `prog_error_stage`, `prog_error_percent`, `prog_speed` |
| `/avrisp_cfg.json` | `submodule_isp` | `chips[]`: `signature`, `name`, `flash_size`, `page_size` |
| `/swd_cfg.json` | `submodule_swd` | `chips[]`: `idcode`, `name`, `family`, `flash_size`, `flash_start`, `page_size`, `word_size`, `csw_value` |

`config_prog_isp.json` / `config_prog_swd.json` — устаревшие имена, мигрируются при старте.

# Аппаратные интерфейсы

| Интерфейс | Назначение | Выводы по умолчанию |
|-----------|-----------|---------------------|
| AVR ISP | MISO/MOSI/SCK/RST | 19 / 23 / 18 / 5 |
| STM32 SWD | CLK/DATA | 21 / 19 |

# Руководство пользователя AVR-ISP программатора

Пошаговая инструкция по прошивке AVR чипа (AtMega/AtTiny) через веб-интерфейс.

> Поддерживаются файлы прошивки в форматах: `.hex`, `.bin`, `binary` (сырой бинарный файл).

## Шаг 1. Подключите AVR чип к ESP

Подключите AVR чип к ESP согласно распиновке, приведённой в разделе [Подключение AVR чипа. Распиновка](#подключение-avr-чипа-распиновка). Убедитесь, что уровни напряжения согласованы.

## Шаг 2. Включите ESP и подключитесь к веб-интерфейсу

Подайте питание на ESP. Подключитесь к веб-интерфейсу через браузер (по IP-адресу или через `http://<имя>_<серийник>.local`).

## Шаг 3. Откройте страницу программатора

В меню веб-интерфейса перейдите на страницу **AVR fOTA** (`/avr.html`).

## Шаг 4. Настройте проект (однократно)

Нажмите на ссылку **Project configuration** (или перейдите на `/project.html`):
- В поле **Project name** введите название проекта (например, `MyAVRProject`).
- Из выпадающего списка **Chip** выберите целевой чип AVR (например, `ATmega328p`).
- Нажмите **Save**.
- После сохранения отобразится информация о чипе: сигнатура, размер flash, размер страницы.

> Конфигурация проекта сохраняется в ФС ESP и восстанавливается автоматически при последующих сессиях.

## Шаг 5. Загрузите файл прошивки

На странице `/avr.html`:
- Нажмите кнопку **Choose File** (или **Browse**) и выберите файл прошивки (форматы `.hex`, `.bin` или `binary`) на вашем компьютере.
- Нажмите кнопку **Upload firmware file**.
- Дождитесь завершения загрузки — прогресс отображается в индикаторе **Upload progress**.
- После успешной загрузки файл появится в таблице файлов прошивки.

## Шаг 6. Выберите файл в таблице

В таблице файлов прошивки кликните на строку с загруженным файлом — строка выделится. Отобразится информация:
- **Name** — имя файла.
- **Type** — тип файла (например, `hex`, `bin`, `binary`).
- **Size** — размер файла.
- **Upload date** — дата загрузки.
- **Program date** — дата последней прошивки этим файлом.
- **Status** — статус (например, `current` — текущая версия, `previous` — предыдущая).

## Шаг 7. Нажмите Program

Убедитесь, что нужный файл выделен в таблице, и нажмите кнопку **Program**.
- Начнётся процесс прошивки AVR чипа.
- Прогресс отображается в индикаторе **Programming progress**.
- Дождитесь завершения (100%).

## Шаг 8. Проверьте результат

После завершения прошивки:
- Статус файла изменится на `current`.
- Предыдущая версия прошивки автоматически сохранится со статусом `previous` (для возможности отката).
- AVR чип перезапустится и начнёт выполнять новую прошивку.

## Шаг 9. Откат к предыдущей версии (при необходимости)

Если новая прошивка работает некорректно:
- Выделите в таблице файл со статусом `current`.
- Нажмите кнопку **Rollback version** (доступна, если есть файл со статусом `previous`).
- ESP перепрошьёт AVR чип предыдущей версией прошивки.

# Руководство пользователя SWD программатора

> [!WARNING]
> **Прошивка STM32 через SWD может длиться продолжительное время (до нескольких минут), особенно для чипов с большим объёмом flash-памяти. Не прерывайте процесс и не отключайте питание ESP до завершения прошивки!**

Пошаговая инструкция по прошивке STM32 чипа через SWD.

> Поддерживаются файлы прошивки в форматах: `.hex`, `.bin`, `binary` (сырой бинарный файл).

## Шаг 1. Подключите STM32 чип к ESP

Подключите STM32 чип к ESP согласно распиновке, приведённой в разделе [Подключение STM32 чипа. Распиновка](#подключение-stm32-чипа-распиновка). Убедитесь, что уровни напряжения согласованы.

## Шаг 2. Включите ESP и подключитесь к веб-интерфейсу

Подайте питание на ESP. Подключитесь к веб-интерфейсу через браузер (по IP-адресу или через `http://<имя>_<серийник>.local`).

## Шаг 3. Откройте страницу программатора

В меню веб-интерфейса перейдите на страницу **STM32 SWD** (`/stm32.html`).

## Шаг 4. Настройте проект (однократно)

Нажмите на ссылку **Project configuration** (или перейдите на `/project.html`):
- В поле **Project name** введите название проекта (например, `MySTM32Project`).
- Из выпадающего списка **Chip** выберите целевой чип STM32 (например, `STM32F103C8`).
- Нажмите **Save**.
- После сохранения отобразится информация о чипе: IDCODE, семейство, размер flash, размер страницы.

> Конфигурация проекта сохраняется в ФС ESP и восстанавливается автоматически при последующих сессиях.

## Шаг 5. Загрузите файл прошивки

На странице `/stm32.html`:
- Нажмите кнопку **Choose File** (или **Browse**) и выберите файл прошивки (форматы `.hex`, `.bin` или `binary`) на вашем компьютере.
- Нажмите кнопку **Upload firmware file**.
- Дождитесь завершения загрузки — прогресс отображается в индикаторе **Upload progress**.
- После успешной загрузки файл появится в таблице файлов прошивки.

## Шаг 6. Выберите файл в таблице

В таблице файлов прошивки кликните на строку с загруженным файлом — строка выделится. Отобразится информация:
- **Name** — имя файла.
- **Type** — тип файла (например, `hex`, `bin`, `binary`).
- **Size** — размер файла.
- **Upload date** — дата загрузки.
- **Program date** — дата последней прошивки этим файлом.
- **Status** — статус (например, `current` — текущая версия, `previous` — предыдущая).

## Шаг 7. Нажмите Program

Убедитесь, что нужный файл выделен в таблице, и нажмите кнопку **Program**.
- Начнётся процесс прошивки STM32 чипа через SWD.
- **Внимание:** прошивка может длиться от нескольких секунд до нескольких минут в зависимости от размера flash-памяти чипа.
- Прогресс отображается в индикаторе **Programming progress**.
- **Не закрывайте страницу и не отключайте питание ESP до завершения прошивки!**
- Дождитесь завершения (100%).

## Шаг 8. Проверьте результат

После завершения прошивки:
- Статус файла изменится на `current`.
- Предыдущая версия прошивки автоматически сохранится со статусом `previous` (для возможности отката).
- STM32 чип перезапустится и начнёт выполнять новую прошивку.

## Шаг 9. Откат к предыдущей версии (при необходимости)

Если новая прошивка работает некорректно:
- Выделите в таблице файл со статусом `current`.
- Нажмите кнопку **Rollback version** (доступна, если есть файл со статусом `previous`).
- ESP перепрошьёт STM32 чип предыдущей версией прошивки.

# Слоистая структура

Из `../../LAYERS.md`: `module_prog` — общие cfg/filelist/md5/миграция; типы в `.h`,
не-веб логика в `.cpp`; выделить `_types.h`, `_engine.cpp` (cfg/filelist/md5).
`submodule_isp` — аппаратная часть уже в `prog_isp.*`; выделить `_types.h`, engine частично.
`submodule_swd` — аппаратная часть в `swd.*`/`stm32f*_flash.*`; выделить `_types.h`, engine
частично. Приоритет — низкий.

# Критерии приёмки

- AC-7: AVR ISP: чтение сигнатуры/фьюзов, прошивка HEX/BIN, верификация, прогресс; при неверном чипе — ошибка без повреждения конфига.
- AC-8: STM32 SWD: чтение IDCODE, определение чипа, mass-erase + прошивка + reset; алгоритмы F1/F4 выбираются верно.

# Ограничения и известные проблемы

- OPEN-9: Отладочные команды программатора в терминале закомментированы (`flash`, `flash2`, `stm32`, `swdf`, `avr`) и не активны.

# Терминальные команды

Активных команд нет (`flash`/`flash2`/`stm32`/`swdf`/`avr` закомментированы, см. OPEN-9).

# Тестирование

- Host unit-тесты (уровень 1): `format_hex.*`/`format_bin.*` — CRC, переполнение адресов,
  коды ошибок −9…−13, постраничное чтение BIN; поиск чипа по сигнатуре/IDCODE на моковых
  JSON-базах (см. `../../TESTING.md`).
- HIL-стенд (уровень 5): реальный ATmega328P/STM32F103, прошивка эталонного HEX,
  верификация, чтение fuses/IDCODE, проверка `CHIP_MISMATCH` (см. `../../TESTING.md`).

# Ссылки

- Ядро и конвенции: `../../TRS.md`
- Слоистая структура: `../../LAYERS.md`
- Общие утилиты: `../../TRS.md` §3.1.12 (`common/`)
- Сборка: `../../BUILD.md`
- Реестр компонентов: `../../INVENTORY.md`

> Если модуль читается вне дерева ядра (standalone), корневые документы доступны в
> репозитории ядра avr-fota.
