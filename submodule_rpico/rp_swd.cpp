#include "main.h"

#include "Arduino.h"
#include "core_web/FSWebServerLib.h"

#include "submodule_rpico.h"
#include "rp_swd.h"

// ============================================================
// SWD bit-bang PHY (копия submodule_swd/swd.cpp с префиксом rp_)
// ============================================================

#ifndef SWDPIN_CLK
#define SWDPIN_CLK   21   // SWCLK
#endif

#ifndef SWDPIN_DATA
#define SWDPIN_DATA  19   // SWDIO
#endif

#ifndef RPICO_SWD_DELAY
#define RPICO_SWD_DELAY   1
#endif

uint8_t rp_swdDelay = RPICO_SWD_DELAY;
uint8_t rp_swdRetryCount = 5;
uint8_t rp_swd_last_ack = 0;
uint8_t rp_swd_extra_turnaround = 0;

// Пины вынесены в переменные: даёт возможность проверить перепутанные
// CLK/DATA без перепрошивки (см. rp2040_read_dpidr/connect).
static uint8_t rp_swdClkPin  = SWDPIN_CLK;
static uint8_t rp_swdDataPin = SWDPIN_DATA;

void rp_swd_set_pins(uint8_t clk_pin, uint8_t data_pin) {
	rp_swdClkPin = clk_pin;
	rp_swdDataPin = data_pin;
}

uint8_t rp_swd_get_clk_pin() {
	return rp_swdClkPin;
}

uint8_t rp_swd_get_data_pin() {
	return rp_swdDataPin;
}

void rp_swd_set_delay(uint8_t delay) {
	rp_swdDelay = delay;
}

void rp_swd_reset_retry_count() {
	rp_swdRetryCount = 5;
}

void rp_swd_update_retry(bool success) {
	if (success) {
		if (rp_swdRetryCount > 2) {
			rp_swdRetryCount--;
		}
	} else {
		if (rp_swdRetryCount < 15) {
			rp_swdRetryCount += 2;
			if (rp_swdRetryCount > 15) {
				rp_swdRetryCount = 15;
			}
		}
	}
}

static volatile bool turn_state = 0;

void rp_swd_gpio_init() {
	pinMode(rp_swdDataPin, INPUT_PULLUP);
	pinMode(rp_swdClkPin, OUTPUT);
	rp_swd_rst_init();
}

void rp_swd_rst_init() {
#if (RPICO_RST_PIN >= 0)
	pinMode(RPICO_RST_PIN, OUTPUT);
	digitalWrite(RPICO_RST_PIN, HIGH);
#endif
}

void rp_swd_rst_assert() {
#if (RPICO_RST_PIN >= 0)
	digitalWrite(RPICO_RST_PIN, LOW);
#endif
}

void rp_swd_rst_release() {
#if (RPICO_RST_PIN >= 0)
	digitalWrite(RPICO_RST_PIN, HIGH);
#endif
}

void rp_swd_rst_pulse(uint32_t low_ms) {
#if (RPICO_RST_PIN >= 0)
	rp_swd_rst_assert();
	delay(low_ms);
	rp_swd_rst_release();
#else
	(void)low_ms;
#endif
}

int rp_swd_swdio_level() {
	pinMode(rp_swdDataPin, INPUT_PULLUP);
	delayMicroseconds(5);
	return digitalRead(rp_swdDataPin);
}

void rp_swd_pin_levels(int &clk_low, int &clk_high, int &data_pull_down, int &data_pull_up,
                       int &clk_pull_down, int &clk_pull_up,
                       int &clk_float_after_high, int &data_float_after_low,
                       int &clk_float_after_low) {
	pinMode(rp_swdClkPin, OUTPUT);
	digitalWrite(rp_swdClkPin, LOW);
	delayMicroseconds(5);
	clk_low = digitalRead(rp_swdClkPin);

	digitalWrite(rp_swdClkPin, HIGH);
	delayMicroseconds(5);
	clk_high = digitalRead(rp_swdClkPin);

	pinMode(rp_swdDataPin, INPUT_PULLDOWN);
	delayMicroseconds(10);
	data_pull_down = digitalRead(rp_swdDataPin);

	pinMode(rp_swdDataPin, INPUT_PULLUP);
	delayMicroseconds(10);
	data_pull_up = digitalRead(rp_swdDataPin);

	// SWCLK у RP2040 имеет внутренний pull-down: если линия подключена,
	// ESP-овский pull-up (~45k) против ~50k цели даёт промежуточный уровень.
	pinMode(rp_swdClkPin, INPUT_PULLDOWN);
	delayMicroseconds(10);
	clk_pull_down = digitalRead(rp_swdClkPin);

	pinMode(rp_swdClkPin, INPUT_PULLUP);
	delayMicroseconds(10);
	clk_pull_up = digitalRead(rp_swdClkPin);

	// Отпускаем линию в "плавающий" вход: если провод до цели цел, её
	// внутренняя подтяжка вернёт уровень (SWCLK -> 0, SWDIO -> 1).
	pinMode(rp_swdClkPin, OUTPUT);
	digitalWrite(rp_swdClkPin, HIGH);
	delay(2);
	pinMode(rp_swdClkPin, INPUT);
	delay(50);
	clk_float_after_high = digitalRead(rp_swdClkPin);

	pinMode(rp_swdDataPin, OUTPUT);
	digitalWrite(rp_swdDataPin, LOW);
	delay(2);
	pinMode(rp_swdDataPin, INPUT);
	delay(50);
	data_float_after_low = digitalRead(rp_swdDataPin);

	// SWCLK: прижать к 0 и отпустить. У RP2040 на SWCLK pull-up =>
	// при целом проводе линия вернётся в 1.
	pinMode(rp_swdClkPin, OUTPUT);
	digitalWrite(rp_swdClkPin, LOW);
	delay(2);
	pinMode(rp_swdClkPin, INPUT);
	delay(50);
	clk_float_after_low = digitalRead(rp_swdClkPin);

	turn_state = 0;
	pinMode(rp_swdDataPin, INPUT_PULLUP);
	pinMode(rp_swdClkPin, OUTPUT);
}

void rp_swd_line_reset_swd() {
	rp_swd_write(0xffffffff, 32);
	rp_swd_write(0xffffffff, 32);
	rp_swd_write(0, 32);
	rp_swd_write(0, 32);
}

// Dormant-to-SWD (ARM IHI 0031E, см. openocd swd_seq_dormant_to_swd).
// RP2040 — DPv2: DP может находиться в dormant, обычный line reset его не будит.
void rp_swd_dormant_to_swd() {
	static const uint8_t alert[16] = {
		0x92, 0xf3, 0x09, 0x62, 0x95, 0x2d, 0x85, 0x86,
		0xe9, 0xaf, 0xdd, 0xe3, 0xa2, 0x0e, 0xbc, 0x19
	};

	rp_swd_write(0xff, 8);			// >= 8 тактов SWDIO=1
	for (uint8_t i = 0; i < 16; i++) {
		rp_swd_write(alert[i], 8);	// selection alert (128 бит)
	}
	rp_swd_write(0xa0, 8);			// 4 такта 0 + начало activation code 0x1a
	rp_swd_write(0xf1, 8);
	rp_swd_write(0xff, 8);			// >= 8 тактов 1
	rp_swd_write(0xffffffff, 32);		// line reset: >= 50 тактов 1
	rp_swd_write(0xffff, 16);
	rp_swd_write(0, 8);			// >= 2 idle такта 0
}

bool rp_swd_targetsel(uint32_t targetsel) {
	// Точная последовательность OpenOCD (cmsis_dap_metacmd_targetsel):
	//   OUT 8 бит: header TARGETSEL (DP 0x0C, write) = 0x99
	//   IN 5 бит:  Trn + ACK(3) + Trn   (DPv2 не отвечает на TARGETSEL)
	//   OUT 33 бита: 32 данных + чётность
	// Никаких лишних idle-тактов после.
	rp_swd_write(0x99, 8);
	rp_swd_turn(0);				// первый Trn (1 такт)
	uint32_t ack_bits = rp_swd_read(4);	// ACK(3) + второй Trn
	uint8_t ack = (uint8_t)(ack_bits & 0x7U);
	rp_swd_last_ack = ack;

	// Вернуть управление линией без дополнительного такта
	pinMode(rp_swdDataPin, OUTPUT);
	turn_state = 1;

	rp_swd_write(targetsel, 32);
	rp_swd_write(rp_swd_calculate_parity(targetsel), 1);

	DEBUGLOGRPICO("targetsel 0x%08x: ack=%u\r\n", targetsel, ack);
	return ack != 1;
}

uint32_t rp_swd_init() {
	DEBUGLOGRPICO("%s\r\n", __FUNCTION__);

	uint32_t idcode = 0;

	// 1) Основной путь (проверен на RP2040): JTAG-to-SWD, line reset,
	//    TARGETSEL (SWDv2 multi-drop), затем DPIDR.
	for (uint8_t attempt = 0; attempt < 2 && idcode == 0; attempt++) {
		rp_swd_write(0xffffffff, 32);
		rp_swd_write(0xffffffff, 32);
		rp_swd_write(0xe79e, 16);
		rp_swd_write(0xffffffff, 32);
		rp_swd_write(0xffffffff, 32);
		rp_swd_write(0, 32);
		rp_swd_write(0, 32);
		rp_swd_targetsel(RP2040_TARGETSEL);
		rp_swd_DP_Read(DP_IDCODE, idcode);
		if (idcode == 0 && attempt == 0) {
			DEBUGLOGRPICO("init: JTAG-to-SWD attempt 1 failed (ack=%u)\r\n", rp_swd_last_ack);
			delay(2);
		}
	}

	// 2) Резерв: dormant-to-SWD (если DP всё же в dormant)
	if (idcode == 0) {
		DEBUGLOGRPICO("init: retry dormant-to-SWD (ack=%u)\r\n", rp_swd_last_ack);
		rp_swd_dormant_to_swd();
		rp_swd_targetsel(RP2040_TARGETSEL);
		rp_swd_DP_Read(DP_IDCODE, idcode);
	}

	DEBUGLOGRPICO("DPIDR: %08x (ack=%u)\r\n", idcode, rp_swd_last_ack);
	return idcode;
}

bool rp_swd_AP_Write(unsigned addr, uint32_t data) {
	uint8_t retry = rp_swdRetryCount;
	while (retry--) {
		bool state = rp_swd_transfer(addr, 1, 0, data);
		if (state) {
			rp_swd_update_retry(true);
			return true;
		}
		rp_swd_update_retry(false);
	}
	return false;
}

bool rp_swd_AP_Read(unsigned addr, uint32_t &data) {
	uint8_t retry = rp_swdRetryCount;
	while (retry--) {
		bool state = rp_swd_transfer(addr, 1, 1, data);
		if (state) {
			rp_swd_update_retry(true);
			return true;
		}
		rp_swd_update_retry(false);
	}
	return false;
}

bool rp_swd_DP_Write(unsigned addr, uint32_t data) {
	uint8_t retry = rp_swdRetryCount;
	while (retry--) {
		bool state = rp_swd_transfer(addr, 0, 0, data);
		if (state) {
			rp_swd_update_retry(true);
			return true;
		}
		rp_swd_update_retry(false);
	}
	return false;
}

bool rp_swd_DP_Read(unsigned addr, uint32_t &data) {
	uint8_t retry = rp_swdRetryCount;
	while (retry--) {
		bool state = rp_swd_transfer(addr, 0, 1, data);
		if (state) {
			rp_swd_update_retry(true);
			return true;
		}
		rp_swd_update_retry(false);
	}
	return false;
}

bool rp_swd_DP_Read_once(unsigned addr, uint32_t &data) {
	return rp_swd_transfer(addr, 0, 1, data);
}

bool rp_swd_AP_Read_once(unsigned addr, uint32_t &data) {
	return rp_swd_transfer(addr, 1, 1, data);
}

// port, ap = 1, ad = 0, read = 1, write = 0
bool rp_swd_transfer(unsigned port_address, bool APorDP, bool RorW, uint32_t &data) {
	bool parity = APorDP ^ RorW ^ ((port_address >> 2) & 1) ^ ((port_address >> 3) & 1);
	uint8_t filled_address = (1 << 0) | (APorDP << 1) | (RorW << 2) |
	                         ((port_address & 0xC) << 1) | (parity << 5) | (1 << 7);
	rp_swd_write(filled_address, 8);
	// Переход на приём (1 такт turnaround)
	rp_swd_turn(0);
	if (rp_swd_extra_turnaround) {
		// Диагностический вариант: ещё один такт ввода без выборки
		digitalWrite(rp_swdClkPin, LOW);
		if (rp_swdDelay == 0) {
			__asm__ __volatile__ ("nop" ::: "memory");
		} else {
			delayMicroseconds(rp_swdDelay);
		}
		digitalWrite(rp_swdClkPin, HIGH);
		if (rp_swdDelay == 0) {
			__asm__ __volatile__ ("nop" ::: "memory");
		} else {
			delayMicroseconds(rp_swdDelay);
		}
	}
	uint8_t ack = rp_swd_read(3);
	rp_swd_last_ack = ack;
	if (ack == 1) {
		if (RorW) {
			data = rp_swd_read(32);
			if (rp_swd_read(1) == rp_swd_calculate_parity(data)) {
				rp_swd_write(0, 1);
				return true;
			}
		} else {
			rp_swd_write(data, 32);
			rp_swd_write(rp_swd_calculate_parity(data), 1);
			rp_swd_write(0, 1);
			return true;
		}
	}
	rp_swd_write(0, 32);
	return false;
}

bool rp_swd_calculate_parity(uint32_t in_data) {
	in_data = (in_data & 0xFFFF) ^ (in_data >> 16);
	in_data = (in_data & 0xFF)   ^ (in_data >> 8);
	in_data = (in_data & 0xF)    ^ (in_data >> 4);
	in_data = (in_data & 0x3)    ^ (in_data >> 2);
	in_data = (in_data & 0x1)    ^ (in_data >> 1);
	return in_data & 1;
}

void rp_swd_write(uint32_t in_data, uint8_t bits) {
	if (turn_state == 0) {
		rp_swd_turn(1);
	}
	if (rp_swdDelay == 0) {
		while (bits--) {
			digitalWrite(rp_swdDataPin, in_data & 1);
			digitalWrite(rp_swdClkPin, LOW);
			__asm__ __volatile__ ("nop" ::: "memory");
			in_data >>= 1;
			digitalWrite(rp_swdClkPin, HIGH);
			__asm__ __volatile__ ("nop" ::: "memory");
		}
	} else {
		while (bits--) {
			digitalWrite(rp_swdDataPin, in_data & 1);
			digitalWrite(rp_swdClkPin, LOW);
			delayMicroseconds(rp_swdDelay);
			in_data >>= 1;
			digitalWrite(rp_swdClkPin, HIGH);
			delayMicroseconds(rp_swdDelay);
		}
	}
}

uint32_t rp_swd_read(uint8_t bits) {
	uint32_t out_data = 0;
	uint32_t input_bit = 1;
	if (turn_state == 1) {
		rp_swd_turn(0);
	}
	if (rp_swdDelay == 0) {
		while (bits--) {
			if (digitalRead(rp_swdDataPin)) {
				out_data |= input_bit;
			}
			digitalWrite(rp_swdClkPin, LOW);
			__asm__ __volatile__ ("nop" ::: "memory");
			input_bit <<= 1;
			digitalWrite(rp_swdClkPin, HIGH);
			__asm__ __volatile__ ("nop" ::: "memory");
		}
	} else {
		while (bits--) {
			if (digitalRead(rp_swdDataPin)) {
				out_data |= input_bit;
			}
			digitalWrite(rp_swdClkPin, LOW);
			delayMicroseconds(rp_swdDelay);
			input_bit <<= 1;
			digitalWrite(rp_swdClkPin, HIGH);
			delayMicroseconds(rp_swdDelay);
		}
	}
	return out_data;
}

// 1 = write, 0 = read
void rp_swd_turn(bool WorR) {
	digitalWrite(rp_swdDataPin, HIGH);
	pinMode(rp_swdDataPin, INPUT_PULLUP);
	digitalWrite(rp_swdClkPin, LOW);
	if (rp_swdDelay == 0) {
		__asm__ __volatile__ ("nop" ::: "memory");
		digitalWrite(rp_swdClkPin, HIGH);
		__asm__ __volatile__ ("nop" ::: "memory");
	} else {
		delayMicroseconds(rp_swdDelay);
		digitalWrite(rp_swdClkPin, HIGH);
		delayMicroseconds(rp_swdDelay);
	}
	if (WorR) {
		pinMode(rp_swdDataPin, OUTPUT);
	}
	turn_state = WorR;
}
