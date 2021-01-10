#include "bootrom.h"
#include <ets_sys.h>
#include <user_interface.h>
#include <spi_flash.h>

#define PERIPHS_DPORT_18		(PERIPHS_DPORT_BASEADDR + 0x018)	// 0x3feffe00 + 0x218
#define PERIPHS_DPORT_IRAM_MAPPING	(PERIPHS_DPORT_BASEADDR + 0x024)	// 0x3feffe00 + 0x224
#define IRAM_UNMAP_40108000		BIT4
#define IRAM_UNMAP_4010C000		BIT3
#define PERIPHS_I2C_48			(0x60000a00 + 0x348)


// TODO move to right header
void clockgate_watchdog(uint32_t);
void pm_open_rf();

// TODO part of libmain.a but not user_interface.o
void user_uart_wait_tx_fifo_empty(uint32_t ch, uint32_t arg2);


ETSTimer* unk_3ffec684;

static void loc_40253a80(void *timer_arg)
{
	*(uint8_t*)(unk_3ffec690 + 0x8) = 0;
	if (*(uint8_t*)(unk_3ffec690 + 0x9) == 0) goto loc_40253b22;

	if (pm_is_waked()) {
		if (!pm_is_open()) goto loc_40253b1f;
	}

	uint8_t* a0 = *(uint8_t*)(unk_3ffec690 + 0xa) + unk_3ffec7b0;
	uint8_t a0_val = *a0;
	if (a0_val == 0x1) goto loc_40253b10;
	else if (a0_val == 0x2) goto loc_40253b0a;
	else if (a0_val == 0x3) goto loc_40253afe;
	else if (a0_val == 0x4) goto loc_40253af8;
	else if (a0_val == 0x5) goto loc_40253aec;
	else if (a0_val != 0x6) goto loc_40253ad3;

	fun_40256444(unk_3ffec708, *(uint8_t*)(unk_3ffec690 + 0xe4));

	uint8_t a4 = *(uint8_t*)(unk_3ffec690 + 0xa);
	a4++;
	a4 &= 0xFF; // $a4 = bitfield($a4, /*lsb*/0, /*sz*/8)

	uint8_t a0 = *(uint8_t*)(unk_3ffec690 + 0x9);
	a0--;
	a0 &= 0xFF; // $a0 = bitfield($a0, /*lsb*/0, /*sz*/8)
	*(uint8_t*)(unk_3ffec690 + 0x9) = a0;

	if (a4 != 0xa) goto loc_40253b16;
	*(uint8_t*)(unk_3ffec690 + 0xa) = 0;

	// TODO continue here
	goto loc_40253b19
}

/* xtensa-lx106-elf-ar x libmain.a user_interface.o
 * xtensa-lx106-elf-objcopy --add-symbol system_func1=.irom0.text:0x54,function,global user_interface.o
 * xtensa-lx106-elf-ar r libmain.a user_interface.o
 */
int32_t system_func1(uint32_t arg1)
{
	if (!fpm_allow_tx(/* TODO arg1 */)) {
		wifi_fpm_do_wakeup(/* TODO */);
	}
	if (pm_is_open(/* TODO */)) {
		if (!*(uint8_t*)(unk_3ffec684 - 4)) {
			ets_timer_setfn(unk_3ffec684, loc_40253a80, 0);
			*(uint8_t*)(unk_3ffec684 - 4) = 1;
		}

		if (!pm_is_waked() || *(uint8_t*)(unk_3ffec684 - 4 + 0x18) == 1 ) {
			// a12 => *(uint8_t*)(unk_3ffec684 - 4 + 0x18)
			// from loc_402539f1: a0 = a12 but may be someone else also uses loc_40253a14
			if (!$a0) {
				pm_post(1);
				ets_timer_disarm(unk_3ffec684);
				ets_timer_arm_new(unk_3ffec684, 10, 0, 1);	/* 10ms once */
				*(uint8_t*)(unk_3ffec684 - 4 + 0x18) = 1;
			}
			uint8_t val = *(uint8_t*)(unk_3ffec684 - 4 + 0x19);
			val++;
			val &= 0xFF; // $a0 = bitfield($a0, /*lsb*/0, /*sz*/8)
			*(uint8_t*)(unk_3ffec684 - 4 + 0x19) = val;

			if (10 < val) {
				os_printf_plus("DEFERRED FUNC NUMBER IS BIGGER THAN 10\n");
				*(uint8_t*)(unk_3ffec684 - 4 + 0x19) = 10;
			}

			int32_t a3 = *(uint8_t*)(unk_3ffec684 - 4 + 0x1A) + 10;
			a0 = a3 + unk_3ffec72c;
			if (10 >= a3) goto loc_40253a04;
			*(uint8_t)(unk_3ffec72c + 0x79 + a3) = sp; //TODO 40253a68     $a4 = *(u32*)$sp
			return -1; // See 40253a09 $a2 = -0x1


		}
	}

	return 0; // See 402539fc $a2 = 0x0
}

/* arduinoespressif8266 2.7.4 (NONOSDK22x_190703) */
void system_restart_core()
{
	Wait_SPI_Idle(flashchip);
	Cache_Read_Disable();
	/* map IRAM section starting at 0x40108000 and 0x4010C000 */
	CLEAR_PERI_REG_MASK(PERIPHS_DPORT_IRAM_MAPPING, IRAM_UNMAP_40108000 | IRAM_UNMAP_4010C000); /* 0x18 == ~(-0x19) */
	_ResetVector();
}

/* arduinoespressif8266 2.7.4 (NONOSDK22x_190703) */
void system_restart_hook()
{
	return;
}

/* arduinoespressif8266 2.7.4 (NONOSDK22x_190703) */
void system_restart_local()
{
	if (system_func1(0x4) == -1) {
		clockgate_watchdog(0);
		SET_PERI_REG_MASK(PERIPHS_DPORT_REG_18, 0xffff00ff);
		pm_open_rf();
	}

	struct rst_info rst_info;
	system_rtc_mem_read(0, &rst_info, sizeof(rst_info));
	if (rst_info.reason != REASON_SOFT_WDT_RST &&
		rst_info.reason != REASON_EXCEPTION_RST) {
		ets_memset(&rst_info, 0, sizeof(rst_info));
		WRITE_PERI_REG(RTC_STORE0, REASON_SOFT_RESTART);
		rst_info.reason = REASON_SOFT_RESTART;
        	system_rtc_mem_write(0, &rst_info, sizeof(rst_info));
	}
	system_restart_hook();
	user_uart_wait_tx_fifo_empty(0, 0x7a120);
	user_uart_wait_tx_fifo_empty(1, 0x7a120);
	ets_intr_lock();
	SET_PERI_REG_MASK(PERIPHS_DPORT_REG_18, 0x7500);
	CLEAR_PERI_REG_MASK(PERIPHS_DPORT_REG_18, 0x7500); //~0xffff8aff;
	SET_PERI_REG_MASK(PERIPHS_I2C_REG_48, 0x2);
	CLEAR_PERI_REG_MASK(PERIPHS_I2C_REG_48, 0x2);	// ~(-3)

	system_restart_core();
}

