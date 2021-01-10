#include "bootrom.h"
#include <ets_sys.h>
#include <user_interface.h>
#include <spi_flash.h>
#include "../../../toolchain-xtensa/include/xtensa/corebits.h"
#include "../../../framework-esp8266-nonos-sdk/driver_lib/include/driver/uart.h"
#include <Arduino.h>

typedef struct {
	uint32_t* const dest;
	uint32_t* const dest_end;
	const uint32_t* const src;
} rom_store_table_t;

struct {
	uint8_t var1;
	uint8_t reserved;
	uint16_t var3;
} UartMsgStruct;


/*** located in RAM ***/
extern uint32_t _xtos_exc_handler_table_[];
extern uint32_t _sysram_data_end[0];
extern uint32_t _sysram_rodata[];
extern uint32_t user_exc_vector_table[];
extern uint32_t _sysram_bss[];
extern uint32_t _sysram_bss_end[0];

void (*user_start_fptr)() = NULL;	/* originally allocated at 0x3fffdcd0 */


/*** located in ROM ***/
uint32_t* const _ResetVectorDataPtr = NULL;	/* originally allocated at 0x40000084 */

extern const uint32_t _rom_store[];
extern const uint32_t _rom_store_seg1[];
extern const uint32_t _rom_store_seg2[];
extern const uint32_t _rom_store_seg_empty[0];

const rom_store_table_t _rom_store_table[] = {
	{_xtos_exc_handler_table_, _sysram_data_end, _rom_store},
	{_sysram_rodata, user_exc_vector_table, _rom_store_seg1},
	{user_exc_vector_table, _sysram_bss, _rom_store_seg2},
	{_sysram_bss, _sysram_bss, _rom_store_seg_empty},
	{_sysram_bss, _sysram_bss, _rom_store_seg_empty},
	{_sysram_bss, _sysram_bss, _rom_store_seg_empty},
	{_sysram_bss, _sysram_bss, _rom_store_seg_empty},
	{NULL, NULL, NULL}
};


static inline uint32_t __rsil_1() {
	uint32_t program_state;
	asm volatile("rsil %0, 1" : "=r" (program_state));
	return program_state;
}

static inline uint32_t __rsr_prid() {
	uint32_t processor_id;
	asm volatile("rsr.prid %0" : "=r" (processor_id));
	return processor_id;
}

static inline void __witlb(uint32_t attribute, uint32_t page) {
	asm volatile("witlb %0, %1" :: "r" (attribute), "r" (page));
}

static inline void __wdtlb(uint32_t attribute, uint32_t page) {
	asm volatile("wdtlb %0, %1" :: "r" (attribute), "r" (page));
}

static inline void __wsr_intenable(uint32_t interupt_enable) {
	asm volatile("wsr.intenable %0" :: "r" (interupt_enable));
}

static inline void __wsr_litbase(uint32_t literal_base) {
	asm volatile("wsr.litbase %0" :: "r" (literal_base));
}

static inline void __wsr_ps(uint32_t program_state) {
	asm volatile("wsr.ps %0" :: "r" (program_state));
}

static inline void __wsr_vecbase(uint32_t vector_base) {
	asm volatile("wsr.vecbase %0" :: "r" (vector_base));
}


void _ResetHandler()
{
	/* disable all level 1 interrupts */
	__wsr_intenable(0);
	/* Clear the literal base to use an offset of 0 for
	 * Load 32-bit PC-Relative(L32R) instructions
	 */
	__wsr_litbase(0);
	asm volatile("rsync");

	/* in case of ESP8266 always 0 because only one processor core exists */
	uint32_t processor_id = __rsr_prid();
	if (_ResetVectorDataPtr != NULL && (processor_id & 0xFF) == 0x00)
		*_ResetVectorDataPtr = 0;

	/* Set interrupt vector base address to system ROM */
	__wsr_vecbase(0x40000000);
	/* Set interrupt level to 1. Therefore disable interrupts of level 1.
	 * Above levels like level 2,... might still be active if available
	 * on ESP8266.
	 */
	__rsil_1();

	/* Set TLB attributes for instruction access for all pages
	 * START	END		Attrib	Description
	 * 0x00000000	0x1FFFFFFFh	0xF	Illegal
	 * 0x20000000	0x5FFFFFFFh	0x1	RWX, Cache Write-Through
	 * 0x60000000h	0xFFFFFFFFh	0x2	RWX, Bypass Cache
	 */
	uint32_t instruction_page = 0;
	uint32_t page_of_this_func = ((uint32_t)&&critical_page) & 0xe0000000;
	for (uint32_t instruction_attr_list = 0x2222211f;; instruction_attr_list >>= 4) {
		/* 0x400000f3 */
		uint32_t attribute = instruction_attr_list & 0x0F;
critical_page:
		if (instruction_page == page_of_this_func) {
			/* 0x400000e0 */
			/* isync has to be executed immediately when changing
			 * attributes of the page currently executing
			 */
			__witlb(attribute, instruction_page);
			asm volatile("isync");
			instruction_page -= 0xe0000000;
			if (instruction_page < 0x10)
				break;
		} else {
			/* 0x400000f9 */
			__witlb(attribute, instruction_page);
			instruction_page -= 0xe0000000;
			if (instruction_page < 0x10) {
				/* for currently not executed pages, calling isync only
				 * once when done with loop is sufficient
				 */
				asm volatile("isync");
				break;
			}
		}
	}

	/* 0x40000105 */
	/* Set TLB attributes for data access for all pages same as for instructions */
	uint32_t data_page = 0;
	uint32_t data_attr_list = 0x2222211f;
	do {
		__wdtlb(data_attr_list & 0x0F, data_page);
		data_page -= 0xe0000000;
		data_attr_list >>= 4;
	} while (data_page >= 0x10);
	asm volatile("dsync");

	/* Copy system ROM data to system RAM */
	const rom_store_table_t* iter = _rom_store_table;
	if (iter != NULL) {
		uint32_t* dest;
		const uint32_t* src;
		do {
			while (true) {
				/* 0x40000124 */
				dest = iter->dest;
				uint32_t* dest_end = iter->dest_end;
				src = iter->src;
				++iter;
				if (dest >= dest_end)
					break;

				/* 0x40000130 */
				do {
					*dest = *src;
					++src;
					++dest;
				} while (dest < dest_end);
			}
			/* 0x40000140 */
		} while (dest != NULL || src != NULL);
	}

	/* 0x40000146 */
	asm volatile("isync");
	_start();
}

void _start()
{
	/* Set stack pointer to upper end of data RAM */
	const uint32_t stack_pointer = 0x40000000;
	asm volatile("mov a1, %0" :: "r" (stack_pointer));

	/* Set the program state register
	 * Name				Value	Description
	 * Interupt level disable	0	enable all interrupt levels
	 * Exception mode		0	normal operation
	 * User vector mode		1	user vector mode, exceptions need to switch stacks
	 * Privilege level		0	Set to Ring 0
	 */
	__wsr_ps(0x20);
	asm volatile("rsync");

	for(uint32_t *p = _sysram_bss; p<_sysram_bss_end; p++) {
		*p = 0;
	}

	main();

	while (true) {
		/* raise DebugException */
		asm volatile("break 1, 15");
	}
}

int main()
{
	uartAttach();
	Uart_Init(0);
	ets_install_uart_printf(0);

	const BOOT_DEV boot_device = (GPI >> 16) & 0x07;
	if (boot_device == BOOT_DEV_SDIO_6)
		sip_init_attach(0);
	else if (boot_device == BOOT_DEV_SDIO_BOOT)
		sip_init_attach(1);
	else if (boot_device == BOOT_DEV_SDIO_4)
		sip_init_attach(2);
	else if (boot_device == BOOT_DEV_SDIO_UART_DWNLD)
		sip_init_attach(3);

	const RESET_REASON rst_cause = rtc_get_reset_reason();
	const int boot_mode = (GPI >> 29) & 0x7;
	ets_printf("\n ets %s,rst cause:%d, boot mode:(%d,%d)\n\n", "Jan  8 2013",
			rst_cause, boot_device, boot_mode);

	if (rst_cause < SW_RESET && rst_cause != NO_MEAN) {
		/* 0x40001121 */
		boot_from_something(&user_start_fptr);
	} else if (rst_cause == SW_RESET) {
		/* in case of software reset, do not enter download mode */
	} else if (rst_cause == OWDT_RESET) {
		/* 0x40001118 */
		ets_printf("wdt reset\n");
		boot_from_something(&user_start_fptr);
	} else if (rst_cause >= RESET_COUNT || rst_cause < DEEPSLEEP_RESET) {
		/* 0x40001130 */
		ets_printf("unknown reset\n");
		ets_printf("%s %s \n", "ets_main.c", "187");
		while (true);
	}

	/* 0x4000108a */
	if (boot_device == BOOT_DEV_SPI_BOOT || (boot_device == BOOT_DEV_SPI_UART_DWNLD && user_start_fptr == NULL)) {
		if (boot_from_flash() != 0) {
			ets_printf("%s %s \n", "ets_main.c", "181");
			while (true);
		}
	}

	_xtos_set_exception_handler(EXCCAUSE_UNALIGNED, window_spill_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_ILLEGAL, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_INSTR_ERROR, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_STORE_ERROR, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_PROHIBITED, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_STORE_PROHIBITED, print_fatal_exc_handler);
	_xtos_set_exception_handler(EXCCAUSE_PRIVILEGED, print_fatal_exc_handler);

	if (user_start_fptr) {
		user_start_fptr();
	}

	ets_printf("user code done\n");
	ets_run();
	return 0;
}

void boot_from_something(void (**user_start_ptr)())
{
	const int boot_mode = (GPI >> 29) & 0x7;
	/* check if MTDO/GPIO15 is high */
	const int is_sdio = GPI & (1<<18);

	uint32_t uart_no;
	if (is_sdio) {
		if (boot_mode == 2)
			uart_no = 1;
		else
			uart_no = 0;
	} else
		uart_no = 0;

	const BOOT_DEV boot_device = (GPI >> 16) & 0x07;
	if (boot_device == BOOT_DEV_SPI_BOOT) {
		spi_flash_attach();
		*user_start_ptr = NULL;
		return;
	} else if (boot_device == BOOT_DEV_SPI_BOOT_IRAM1) {
		/* start address of instruction RAM1 */
		*user_start_ptr = (void(*)())0x40100000;
		return;
	}

	DWNLD_MODE download_mode = DWNLD_MODE_UNKNOWN;
	if (boot_device == BOOT_DEV_SPI_UART_DWNLD) {
		const uint32_t divlatch = uart_baudrate_detect(uart_no, 0);
		/* NONOS_SDK also cotains such a function. Use the one from ROM here */
		uart_div_modify(uart_no, divlatch & 0xFFFF);
		download_mode = DWNLD_MODE_UART;
	}

	if (is_sdio && boot_mode != 2) {
		ets_printf("waiting for host\n");
		download_mode = DWNLD_MODE_SDIO_WAIT_HOST;
	}
	if (is_sdio) {
		if (boot_mode == 2) {
			Uart_Init(uart_no);
			uart_buff_switch(uart_no);
			ets_isr_unmask(32);

			if (download_mode == DWNLD_MODE_UNKNOWN) {
				do {
					DWNLD_MODE a2_143 =  UartConnCheck();
					download_mode = a2_143 & 0xFF;
					if (download_mode)
						break;
					download_mode = DWNLD_MODE_UNKNOWN;
					if (sip_get_state() == 2)
						download_mode = DWNLD_MODE_SDIO_WAIT_HOST;
				} while (download_mode == DWNLD_MODE_UNKNOWN);
			}
			ets_isr_mask(32);
		}
	}
	if (download_mode == DWNLD_MODE_UART)
		UartDwnLdProc((void*)0x3fffa000, 0x2000, user_start_ptr);
	else
		/* sip_wait_until_state_2() */
		sip_40001160();
	if (uart_no)
		uart_buff_switch(0);
}

void UartDwnLdProc(uint8_t* buf, uint32_t buf_size, void (**user_start_ptr)())
{
	uint32_t var_18 = 0;
	UartMsgStruct.var1 = 1;
	UartMsgStruct.var3 = 2;
	ets_isr_unmask(0x20);

	if (var_18 == 2) {
		goto loc_400033f1;
	}

	uint32_t var_20 = unk_3fffde04;
	// TODO
}

