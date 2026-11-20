# module_program — программатор AVR (ISP), STM32 (SWD) и RP2040 (SWD)

> **Опциональный модуль.** Подключается через `src_filter` + `build_flags`.
> **Работоспособен только в составе сборки, содержащей ядро** (см. `../../TRS.md` §2.1).

Один git-репозиторий, включающий четыре компонента: общий контейнер `module_prog/`
(`Class_ProgBase`), AVR ISP-субмодуль `submodule_isp/` (`Class_SubIsp`), STM32 SWD-субмодуль
`submodule_swd/` (`Class_SubSwd`) и RP2040 SWD-субмодуль `submodule_rpico/` (`Class_SubRpico`).
Все инструкции — общий контейнер, специфика субмодулей и руководства пользователя —
собраны в этом файле.

- **Репозиторий:** https://github.com/Arcanum753/module_program
- **Папка:** `src/module_program/`
- **Флаг активации:** `-D PROGTYPE_ISP`, `-D PROGTYPE_SWD` или `-D PROGTYPE_RPICO` (выбор субмодуля — через `src_filter`, а не через `#if PROGTYPE_*`)
- **Registry:** `object=progIsp`, `define=PROGTYPE_ISP` (ISP) либо `object=progSwd`, `define=PROGTYPE_SWD` (SWD), либо `object=progRpico`, `define=PROGTYPE_RPICO` (RP2040); `web=1`, `loop=0`; без `namespace`/`res`/`prio`
- **Только для платформы:** обе; env — `esp32-isp` / `esp32-swd` / `esp32-rpico` (RP2040 — только ESP32)
- **Зависит от модулей:** —
- **Зависит от ядра:** `core_web`, `core_sys`, `core_json`, `core_ntp`, `core_led`, `core_state`

## Состав

| Компонент | Каталог | Класс / объект | Назначение |
|-----------|---------|----------------|------------|
| `module_prog` | `src/module_program/module_prog/` | `Class_ProgBase` (абстрактный), объекта нет | общая база: проектная конфигурация, список файлов, загрузка в FS, диск, статус чипа |
| `submodule_isp` | `src/module_program/submodule_isp/` | `Class_SubIsp : public Class_ProgBase`, `progIsp` | программирование AVR по ISP |
| `submodule_swd` | `src/module_program/submodule_swd/` | `Class_SubSwd : public Class_ProgBase`, `progSwd` | программирование STM32 по SWD |
| `submodule_rpico` | `src/module_program/submodule_rpico/` | `Class_SubRpico : public Class_ProgBase`, `progRpico` | программирование RP2040 (Raspberry Pi Pico) по SWD через ROM-функции bootrom |

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

Реализована возможность чтения и редактирования fuse битов подключенного чипа по маске. Редактор доступен на странице `/avrcfg.html` (вход — со страницы программатора), изначально скрыт (раскрывается чекбоксом "Expand").

## Откат прошивки

Автоматического отката нет: резервирование версий `current`/`previous` и кнопка «Rollback version» удалены. В ФС хранится единый список файлов; для отката выберите в таблице более старый файл и прошейте его вручную.

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

# submodule_rpico — программатор RP2040 SWD

**Каталог:** `src/module_program/submodule_rpico/` · **Класс:** `Class_SubRpico : public Class_ProgBase` · **Объект:** `progRpico`
**`[registry]`:** `object=progRpico`, `define=PROGTYPE_RPICO`, `web=1`, `loop=0`.
**Платформа:** только ESP32 (env `esp32-rpico`).

**Назначение:** программирование flash Raspberry Pi Pico (RP2040) по SWD из файловой
системы. Внешняя QSPI-flash у RP2040 не имеет регистров записи, поэтому стирание/запись
выполняются ROM-функциями bootrom (`connect_internal_flash`, `flash_exit_xip`,
`flash_range_erase`, `flash_range_program`, `flash_flush_cache`, `flash_enter_cmd_xip`),
которые запускаются на самом RP2040 небольшим Thumb-блобом из SRAM.

## Функциональные требования (RP2040)

- FR-RPICO-1: Читать DPIDR (`rp_swd_init`), ожидать `0x0BC12477` (RP2040); иначе — `CHIP_OFFLINE`.
- FR-RPICO-2: Находить чип в `/rpico_cfg.json` по `idcode`; при отсутствии файла — встроенный дефолт RP2040.
- FR-RPICO-3: Сверять чип с `chip_name` из `/config_prog.json` (`423` при несовпадении).
- FR-RPICO-4: halt ядра, отключение IRQ (NVIC ICER), power-up DP, загрузка блоба в SRAM,
  резолв 6 ROM-функций через `rom_table_lookup`.
- FR-RPICO-5: Прошивать `.bin`/`.binary` кооперативно (EERTOS), чанками по 4096 Б:
  `flash_range_erase` (4-КБ сектор, `0x20`) + `flash_range_program` (страницы по 256 Б,
  хвост дополняется `0xFF`), с прогрессом и скоростью.
- FR-RPICO-6: Проверять записанное чтением через XIP (`0x10000000 + offset`) и сравнением с файлом.
- FR-RPICO-7: Сбрасывать цель `AIRCR.SYSRESETREQ` (fallback — `WATCHDOG_CTRL.TRIGGER`),
  записывать статус в `prog_filelist.json` как в SWD.
- FR-RPICO-8: Значения по умолчанию: `flash_start=0x10000000`, `flash_size=2097152`,
  `sector_size=4096`, `page_size=256`.
- FR-RPICO-9: Выводы по умолчанию: `SWDPIN_CLK=21`, `SWDPIN_DATA=19` (как у SWD).
- FR-RPICO-10: RP2350 не поддерживается (другие ROM-функции/QMI).

## Подключение RP2040 (Raspberry Pi Pico). Распиновка

> [!WARNING]
> **ВНИМАНИЕ! ESP И RP2040 ДОЛЖНЫ БЫТЬ СОГЛАСОВАНЫ ПО УРОВНЮ НАПРЯЖЕНИЯ ПИТАНИЯ.**
> Не подключайте USB Pico одновременно с питанием 3.3V от ESP32.

| ESP32 | Pico (debug header) |
|:-----|:--------------------|
| G21   | SWCLK               |
| G19   | SWDIO               |
| GND   | GND                 |
| +3.3V | 3V3                 |
| G22   | RUN (pin 30)        |

RST (RUN) опционален: если задан `RPICO_RST_PIN` (в env `esp32-rpico` — GPIO22),
он используется как резерв при отсутствии ответа SWD (и в диагностике `/rpico/diag?rst`).
Перед обычным подключением сброс **не** выполняется: после RUN-reset debug-порт RP2040
некоторое время не отвечает. После прошивки цель перезапускается `AIRCR.SYSRESETREQ`
(fallback — watchdog).

Пины SWD настраиваются через `build_flags`:
```
-D SWDPIN_CLK=21
-D SWDPIN_DATA=19
-D RPICO_RST_PIN=22   # -1 = не использовать
-D RPICO_SWD_DELAY=2  # мкс на полутакт; 2 проверено на HIL (~2.4 КБ/с)
```

### Особенности RP2040 (SWDv2 multi-drop) — проверено на HIL

- RP2040 — **DPv2**: после line reset обязательна запись `TARGETSEL` (`0x01002927`, core 0)
  специальной последовательностью **8 out + 5 in (Trn+ACK+Trn) + 33 out (data+parity)**
  (как в OpenOCD `cmsis_dap_metacmd_targetsel`). Без TARGETSEL DPIDR читается, но AP
  недоступен (ACK=FAULT/7).
- Если DP в dormant — перед line reset нужна последовательность **Dormant-to-SWD**
  (8 high + 128-битный selection alert + activation `0x1a`, см. OpenOCD `swd_seq_dormant_to_swd`).
- **`DP_SELECT` (0x08) только пишется.** Чтение 0x08 на DPv2 — это `RESEND`, оно не
  отвечает (ACK=7) и срывает протокол.
- **`AP_IDR` (0xFC) читается только при `APBANKSEL=0xF`**; иначе AP возвращает FAULT,
  выставляется `STICKYERR` и все последующие AP-доступы падают. Перед доступом к AP
  полезно очищать ошибки записью `DP_ABORT=0x1E`.
- Блоб исполняется из SRAM (`0x20000200`), PC пишется чётным адресом (Thumb на M0+ всегда).
- ROM-функции на стенде: IF=0x2490, EX=0x23E4, RE=0x236C, RP=0x23B4, FC=0x2350, CX=0x2320.

Диагностика bring-up (под auth): `GET /rpico/diag?levels=1` — уровни SWCLK/SWDIO
(включая «плавающий» тест подтяжек цели); `GET /rpico/diag?rst=<ms>` — удержать RUN
в низком уровне; `/rpico/diag?probe=1` — матрица (пины × turnaround); `/rpico/diag?halt=1`
— проверка DHCSR; `/rpico/diag?steps=1` — пошаговый AP-доступ; `/rpico/diag?ap=1` — AP с/без TARGETSEL.

## Тестовые прошивки и Thumb-блоб

- `tools/rpico_test_fw/` — CMake-проект (pico-sdk) с 4 целями мигания LED GPIO25
  (100/500/1000/2000 мс) и `build_fw.py`; результат кладётся в `<ядро>/hex_bin/pico_*.bin`.
- `tools/rp2040_algo/` — исходник Thumb-блоба (`rp2040_algo.s`) и `build_algo.py`,
  который генерирует `submodule_rpico/rp2040_algo.h` (блоб коммитится вместе с
  заголовком; пересборка — только при изменении `.s`). Раскладка SRAM:
  блоб `0x20000200`, params `0x20000400`, lookup params `0x20000480`,
  буфер данных `0x20001000`, стек `0x20040000`.

# Веб-интерфейс (общий для ISP, SWD и RP2040)

Общие маршруты: `/prog/diskinfo`, `/prog/fileslist`, `/prog/delete`, `POST /prog/uploadfile`,
`/prog/uploadstat`, `/prog/setmd5`, `/prog/uploadsize`, `/prog/progress`, `/prog/flash`,
`/prog/ver`, `/prog/chipstatus`, `/project/info`, `/project/save`, `/project/chips`,
`/project/chipinfo`, `GET /project`, `GET /prog`. Все защищены `checkAuth`.

ISP-специфичные маршруты: `GET /avr/fuseread`, `POST /avr/fusewrite`, `/avr/info`,
`POST /avr/save`, `GET /avr/readsignature`, `GET /avrcfg`.

SWD- и RP2040-специфичных маршрутов нет — используются только общие маршруты `module_prog`.

Веб-файлы: `_menu.html`, `prog.html`, `project.html`, `avrcfg.html`, `config_prog.json`,
`avrisp_cfg.json`, `swd_cfg.json`, `rpico_cfg.json`, `avrisp.json` (устаревший);
shared `spark-md5.js` — из `core_ota`.

# Конфигурация

| Файл | Владелец | Основные поля |
|------|----------|---------------|
| `/config_prog.json` | `module_prog` | `project`, `chip_name` (один файл для ISP, SWD и RP2040) |
| `/prog_filelist.json` | `module_prog` | `filename`, `upload_date`, `md5`, `prog_date`, `prog_status`, `prog_time`, `prog_error`, `prog_error_stage`, `prog_error_percent`, `prog_speed` |
| `/avrisp_cfg.json` | `submodule_isp` | `chips[]`: `signature`, `name`, `flash_size`, `page_size` |
| `/swd_cfg.json` | `submodule_swd` | `chips[]`: `idcode`, `name`, `family`, `flash_size`, `flash_start`, `page_size`, `word_size`, `csw_value` |
| `/rpico_cfg.json` | `submodule_rpico` | `chips[]`: `idcode`, `name`, `family`, `flash_size`, `flash_start`, `sector_size`, `page_size` |

`config_prog_isp.json` / `config_prog_swd.json` — устаревшие имена, мигрируются при старте.

# Аппаратные интерфейсы

| Интерфейс | Назначение | Выводы по умолчанию |
|-----------|-----------|---------------------|
| AVR ISP | MISO/MOSI/SCK/RST | 19 / 23 / 18 / 5 |
| STM32 SWD | CLK/DATA | 21 / 19 |
| RP2040 SWD | CLK/DATA | 21 / 19 |

# Руководство пользователя AVR-ISP программатора

Пошаговая инструкция по прошивке AVR чипа (AtMega/AtTiny) через веб-интерфейс.

> Поддерживаются файлы прошивки в форматах: `.hex`, `.bin`, `binary` (сырой бинарный файл).

## Шаг 1. Подключите AVR чип к ESP

Подключите AVR чип к ESP согласно распиновке, приведённой в разделе [Подключение AVR чипа. Распиновка](#подключение-avr-чипа-распиновка). Убедитесь, что уровни напряжения согласованы.

## Шаг 2. Включите ESP и подключитесь к веб-интерфейсу

Подайте питание на ESP. Подключитесь к веб-интерфейсу через браузер (по IP-адресу или через `http://<имя>_<серийник>.local`).

## Шаг 3. Откройте страницу программатора

В меню веб-интерфейса перейдите на страницу **AVR fOTA** (`/prog.html`).

## Шаг 4. Настройте проект (однократно)

Нажмите на ссылку **Project configuration** (или перейдите на `/project.html`):
- В поле **Project name** введите название проекта (например, `MyAVRProject`).
- Из выпадающего списка **Chip** выберите целевой чип AVR (например, `ATmega328p`).
- Нажмите **Save**.
- После сохранения отобразится информация о чипе: сигнатура, размер flash, размер страницы.

> Конфигурация проекта сохраняется в ФС ESP и восстанавливается автоматически при последующих сессиях.

## Шаг 5. Загрузите файл прошивки

На странице `/prog.html`:
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
- **Status** — результат последней прошивки (ok/error, дата).

## Шаг 7. Нажмите Program

Убедитесь, что нужный файл выделен в таблице, и нажмите кнопку **Program**.
- Начнётся процесс прошивки AVR чипа.
- Прогресс отображается в индикаторе **Programming progress**.
- Дождитесь завершения (100%).

## Шаг 8. Проверьте результат

После завершения прошивки:
- В списке файлов обновится дата и статус последней прошивки.
- AVR чип перезапустится и начнёт выполнять новую прошивку.

## Шаг 9. Откат к предыдущей версии (при необходимости)

Автоматического отката нет. Если новая прошивка работает некорректно, выделите в таблице ранее загруженный файл и нажмите **Program** — он будет прошит как обычный файл.

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

В меню веб-интерфейса перейдите на страницу **STM32 SWD** (`/prog.html`).

## Шаг 4. Настройте проект (однократно)

Нажмите на ссылку **Project configuration** (или перейдите на `/project.html`):
- В поле **Project name** введите название проекта (например, `MySTM32Project`).
- Из выпадающего списка **Chip** выберите целевой чип STM32 (например, `STM32F103C8`).
- Нажмите **Save**.
- После сохранения отобразится информация о чипе: IDCODE, семейство, размер flash, размер страницы.

> Конфигурация проекта сохраняется в ФС ESP и восстанавливается автоматически при последующих сессиях.

## Шаг 5. Загрузите файл прошивки

На странице `/prog.html`:
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
- **Status** — результат последней прошивки (ok/error, дата).

## Шаг 7. Нажмите Program

Убедитесь, что нужный файл выделен в таблице, и нажмите кнопку **Program**.
- Начнётся процесс прошивки STM32 чипа через SWD.
- **Внимание:** прошивка может длиться от нескольких секунд до нескольких минут в зависимости от размера flash-памяти чипа.
- Прогресс отображается в индикаторе **Programming progress**.
- **Не закрывайте страницу и не отключайте питание ESP до завершения прошивки!**
- Дождитесь завершения (100%).

## Шаг 8. Проверьте результат

После завершения прошивки:
- В списке файлов обновится дата и статус последней прошивки.
- STM32 чип перезапустится и начнёт выполнять новую прошивку.

## Шаг 9. Откат к предыдущей версии (при необходимости)

Автоматического отката нет. Если новая прошивка работает некорректно, выделите в таблице ранее загруженный файл и нажмите **Program** — он будет прошит как обычный файл.

# Руководство пользователя RP2040 (Pico) программатора

> [!WARNING]
> **Не отключайте питание ESP и Pico во время прошивки.** Прерывание стирания/записи
> оставит Pico с частично затёртой flash; восстановление — BOOTSEL + USB с ПК.

1. Подключите Pico по SWD (CLK=G21, DATA=G19, GND, 3V3) и подайте питание.
2. Прошейте ESP с env `esp32-rpico` (`pio run -e esp32-rpico -t upload`); ФС можно не
   обновлять: `/rpico_cfg.json` с дефолтом RP2040 создаётся при старте, если его нет.
   **Если на устройстве старая ФС без страниц модуля**, второй колонки меню и `/prog.html`
   не будет: загрузите `page_head.html`, `prog.html`, `project.html` (и `udp.html` при
   `module_udp`) через `POST /fs/upload?path=/...` (raw body,
   `Content-Type: application/octet-stream`) — Wi-Fi-конфиги сохранятся. Полный
   `pio run -e esp32-rpico -t uploadfs` тоже обновит страницы, но сбросит Wi-Fi-конфиги,
   `secret.json` и файлы прошивок на ФС.
3. Откройте `/project.html`, укажите имя проекта и чип `RP2040`, нажмите **Save**.
4. На `/prog.html` загрузите `.bin` (HEX не поддерживается), выделите файл и нажмите **Program**.
5. Дождитесь 100%: прогресс идёт по секторам 4 КБ, затем выполняется verify и сброс Pico.
6. Pico перезапустится и начнёт выполнять новую прошивку; в таблице файлов появится статус
   `ok`, время и скорость.

Тестовые прошивки (мигание LED GPIO25) собираются `tools/rpico_test_fw/build_fw.py` и
кладутся в `<ядро>/hex_bin/` как `pico_100ms.bin`, `pico_500ms.bin`, `pico_1s.bin`, `pico_2s.bin`.

## Полное обновление ФС с сохранением пользовательских данных

`pio run -e esp32-rpico -t uploadfs` перезаписывает образ ФС целиком (страницы модулей и
дефолтные конфиги), но удаляет пользовательские файлы. Порядок (проверено 2026-11-20):

1. Сохранить по одному файлу (`GET /fs/download?path=/...`): `config_wifi*.json`,
   `secret.json`, `config_sys.json`, `config_ntp/time/prog/udp/state.json`,
   `prog_filelist.json`, `rpico_cfg.json`, `/macros/*.lua`, `pico_*.bin`.
2. Выполнить `pio run -e esp32-rpico -t uploadfs` (устройство перезагрузится). Wi-Fi берётся
   из `config_wifi0.json` образа; в репозитории слот 0 содержит рабочую сеть, поэтому IP
   сохраняется.
3. Восстановить файлы: `POST /fs/upload?path=/...` с `Content-Type: application/octet-stream`.
   Для макросов предварительно `POST /fs/mkdir?path=/macros` (ответ `409`, если каталог уже есть).
4. Проверить `/fs/info` (поле `fsVersion`), `/prog/fileslist` и страницы фронта.

> Если в образе слот 0 не содержит рабочей сети, после `uploadfs` устройство уйдёт в AP:
> Wi-Fi восстанавливается через терминал (`wifi set 0 ssid <...>`, `wifi set 0 pass <...>`,
> `wifi apply`) или страницу Wi-Fi в AP-режиме.

# Слоистая структура

Из `../../LAYERS.md`: `module_prog` — общие cfg/filelist/md5/миграция; типы в
`module_prog_types.h`, не-веб логика в `module_prog_engine.cpp`.
`submodule_isp` — аппаратная часть в `prog_isp.*`, типы в `submodule_isp_types.h`,
engine-часть в `submodule_isp_engine.cpp`.
`submodule_swd` — аппаратная часть в `swd.*`/`stm32f*_flash.*`, типы в
`submodule_swd_types.h`, engine-часть в `submodule_swd_engine.cpp`.
`submodule_rpico` — bit-bang PHY в `rp_sw.*`, низкий уровень RP2040 в `rp2040_swd.*`,
автомат прошивки в `prog_rpico.*`, типы в `submodule_rpico_types.h`, engine-часть в
`submodule_rpico_engine.cpp`; исполняемый на цели блоб — `rp2040_algo.h` (генерируется).
Выделение слоёв выполнено.

# Критерии приёмки

- AC-7: AVR ISP: чтение сигнатуры/фьюзов, прошивка HEX/BIN, верификация, прогресс; при неверном чипе — ошибка без повреждения конфига.
- AC-8: STM32 SWD: чтение IDCODE, определение чипа, mass-erase + прошивка + reset; алгоритмы F1/F4 выбираются верно.
- AC-9: RP2040 SWD: DPIDR `0x0BC12477`, резолв ROM-функций, прошивка `.bin` + verify, программный reset; Pico мигает с заданным периодом.

# Ограничения и известные проблемы

- OPEN-9: Отладочные команды программатора (`flash`, `flash2`, `stm32`, `swdf`, `avr`) в текущем коде отсутствуют — удалены при разделении на слои; терминальная команда программатора не регистрируется. Планируется `isp`/`swd` (см. план терминальных команд).

# Терминальные команды

Активных команд нет; блоки `flash`/`flash2`/`stm32`/`swdf`/`avr` в коде не найдены — команды удалены (см. OPEN-9). Планируются `isp`/`swd`.

# Тестирование

- Host unit-тесты (уровень 1): `format_hex.*`/`format_bin.*` — CRC, переполнение адресов,
  коды ошибок −9…−13, постраничное чтение BIN; поиск чипа по сигнатуре/IDCODE на моковых
  JSON-базах (см. `../../TESTING.md`).
- HIL-стенд (уровень 5): реальный ATmega328P/STM32F103, прошивка эталонного HEX,
  верификация, чтение fuses/IDCODE, проверка `CHIP_MISMATCH` (см. `../../TESTING.md`).
- RP2040 (HIL): Pico, прошивка `pico_100ms.bin`/`pico_500ms.bin`/`pico_1s.bin`/`pico_2s.bin`
  из `hex_bin/`, проверка мигания LED GPIO25 и статуса `ok` в списке файлов.
  **Пройдено 2026-11-20:** все 4 прошивки прошиты и подтверждены пользователем
  (0.1/0.5/1/2 с), verify и reset работают; скорость 2.36 КБ/с при `RPICO_SWD_DELAY=2`.
- Фронт после полного `uploadfs` (2026-11-20, `esp32-rpico`): 13 страниц, 8 ассетов и
  13 API-эндпоинтов — все `200`; меню с второй колонкой (Programmer / Project config / UDP);
  end-to-end `/prog/flash` для `pico_500ms.bin` — 100 %, status `ok`, 2.34 КБ/с;
  Pico мигает ~0.5 с. Конфиги, макросы и прошивки после backup+restore сохранены.
- Thumb-блоб: после `build_algo.py` проверить `objdump -d` — только 16-битные Thumb-инструкции
  (Cortex-M0+), без вызовов `__aeabi_*`.

# Ссылки

- Ядро и конвенции: `../../TRS.md`
- Слоистая структура: `../../LAYERS.md`
- Общие утилиты: `../../TRS.md` §3.1.12 (`common/`)
- Сборка: `../../BUILD.md`
- Реестр компонентов: `../../INVENTORY.md`

> Если модуль читается вне дерева ядра (standalone), корневые документы доступны в
> репозитории ядра avr-fota.
