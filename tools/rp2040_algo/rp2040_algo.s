@ ============================================================
@ rp2040_algo.s — Thumb-блоб для RP2040, исполняемый из SRAM.
@
@ Загружается ESP32 по SWD в SRAM (0x20000200) и запускается через
@ debug-регистры ядра (PC/SP). Обеспечивает доступ к ROM-функциям
@ bootrom RP2040: connect_internal_flash / flash_exit_xip /
@ flash_range_erase / flash_range_program / flash_flush_cache /
@ flash_enter_cmd_xip.
@
@ ВАЖНО: только 16-битные Thumb-инструкции (Cortex-M0+ = ARMv6-M),
@ регистры только r0-r7, вызовы через blx низкого регистра.
@
@ Раскладка SRAM (см. submodule_rpico_types.h — держать синхронно):
@   блоб:            0x20000200 (<= 512 байт)
@   params:          0x20000400
@   lookup params:   0x20000480
@   буфер данных:    0x20001000 (4096 байт)
@   стек:            0x20040000 (растёт вниз)
@
@ params (слова):
@   0 fn_if | 1 fn_ex | 2 fn_re | 3 fn_rp | 4 fn_fc | 5 fn_cx
@   6 flash_offset | 7 len | 8 data_ptr | 9 status | 10 pages_done
@
@ lookup params (слова):
@   0 func_table | 1 lookup_fn | 2..7 codes(IF,EX,RE,RP,FC,CX)
@   8..13 results | 14 status
@ ============================================================

	.syntax unified
	.cpu cortex-m0plus
	.thumb

	.equ RP2040_PARAMS_ADDR,  0x20000400
	.equ RP2040_LPARAMS_ADDR, 0x20000480

	.text

@ ============================================================
@ lookup: resolve ROM-функции через rom_table_lookup
@ ============================================================
	.global rp2040_algo_lookup
	.thumb_func
rp2040_algo_lookup:
	ldr	r4, =RP2040_LPARAMS_ADDR
	ldr	r6, [r4, #0]		@ func_table
	ldr	r5, [r4, #4]		@ lookup_fn
	movs	r7, #0			@ i
1:
	cmp	r7, #6
	bhs	2f
	lsls	r3, r7, #2
	adds	r3, r3, r4
	ldr	r1, [r3, #8]		@ code (word 2+i)
	mov	r0, r6			@ table
	blx	r5
	lsls	r3, r7, #2
	adds	r3, r3, r4
	str	r0, [r3, #32]		@ result (word 8+i)
	adds	r7, #1
	b	1b
2:
	movs	r0, #1
	str	r0, [r4, #56]		@ status (word 14)
	bkpt	#0
	.align	2
	.pool

@ ============================================================
@ erase: IF, EX, RE(offset, len, 0, 0), затем общий finish
@ ============================================================
	.global rp2040_algo_erase
	.thumb_func
rp2040_algo_erase:
	ldr	r4, =RP2040_PARAMS_ADDR
	ldr	r5, [r4, #0]		@ fn_if
	blx	r5
	ldr	r5, [r4, #4]		@ fn_ex
	blx	r5
	ldr	r0, [r4, #24]		@ flash_offset
	ldr	r1, [r4, #28]		@ len
	movs	r2, #0x10
	lsls	r2, r2, #8		@ block_size = 4096
	movs	r3, #0x20		@ block_cmd = 0x20 (sector erase)
	ldr	r5, [r4, #8]		@ fn_re
	blx	r5
	b	9f

@ ============================================================
@ program: IF, EX, RP(offset+p, data+p, 256) в цикле
@ ============================================================
	.global rp2040_algo_prog
	.thumb_func
rp2040_algo_prog:
	ldr	r4, =RP2040_PARAMS_ADDR
	ldr	r5, [r4, #0]		@ fn_if
	blx	r5
	ldr	r5, [r4, #4]		@ fn_ex
	blx	r5
	ldr	r6, [r4, #32]		@ data_ptr
	ldr	r7, [r4, #28]		@ len
	movs	r5, #0			@ p (callee-saved: ROM его сохраняет)
1:
	cmp	r5, r7
	bhs	2f
	ldr	r0, [r4, #24]		@ flash_offset
	adds	r0, r0, r5
	adds	r1, r6, r5		@ data + p
	movs	r2, #0xFF
	adds	r2, #1			@ 256
	ldr	r3, [r4, #12]		@ fn_rp (r3 clobberable — сразу blx)
	blx	r3
	adds	r5, #128
	adds	r5, #128		@ p += 256
	lsrs	r2, r5, #8
	str	r2, [r4, #40]		@ pages_done
	b	1b
2:
9:
	ldr	r5, [r4, #16]		@ fn_fc
	blx	r5
	ldr	r5, [r4, #20]		@ fn_cx
	blx	r5
	movs	r0, #1
	str	r0, [r4, #36]		@ status
	bkpt	#0
	.align	2
	.pool
