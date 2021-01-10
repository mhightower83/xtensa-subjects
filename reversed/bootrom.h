#include <stdint.h>
#include <spi_flash.h>

/* from https://github.com/espressif/arduino-esp32/blob/master/tools/sdk/include/esp32/rom/rtc.h */
typedef enum {
    NO_MEAN                =  0,
    POWERON_RESET          =  1,    /**<1, Vbat power on reset*/
    HW_RESET               =  2,
    SW_RESET               =  3,    /**<3, Software reset digital core*/
    OWDT_RESET             =  4,    /**<4, Legacy watch dog reset digital core*/
    DEEPSLEEP_RESET        =  5,    /**<3, Deep Sleep reset digital core*/
    SDIO_RESET             =  6,    /**<6, Reset by SLC module, reset digital core*/
    RESET_COUNT
} RESET_REASON;

typedef enum {
	BOOT_DEV_SPI_UART_DWNLD	= 1,
	BOOT_DEV_SPI_BOOT_IRAM1	= 2,
	BOOT_DEV_SPI_BOOT	= 3,
	BOOT_DEV_SDIO_4		= 4,
	BOOT_DEV_SDIO_UART_DWNLD= 5,
	BOOT_DEV_SDIO_6		= 6,
	BOOT_DEV_SDIO_BOOT	= 7
} BOOT_DEV;

typedef enum {
	DWNLD_MODE_UNKNOWN		= 0,
	DWNLD_MODE_SDIO_WAIT_HOST	= 1,
	DWNLD_MODE_UART			= 2
} DWNLD_MODE;

//From xtruntime-frames.h
struct XTensa_exception_frame_s {
	uint32_t pc;
	uint32_t ps;
	uint32_t sar;
	uint32_t vpri;
	uint32_t a[16]; //a0..a15
//These are added manually by the exception code; the HAL doesn't set these on an exception.
	uint32_t litbase;
	uint32_t sr176;
	uint32_t sr208;
	 //'reason' is abused for both the debug and the exception vector: if bit 7 is set,
	//this contains an exception reason, otherwise it contains a debug vector bitmap.
	uint32_t reason;
};


/* boot */
void _ResetVector();
void _start();
int main();
RESET_REASON rtc_get_reset_reason();
void boot_from_something(void (**user_start_ptr)());
int boot_from_flash();
void UartDwnLdProc(uint8_t* buf, uint32_t buf_size, void (**user_start_ptr)());
void ets_run();

/* exceptions */
void _xtos_set_exception_handler(int cause, void (exhandler)(struct XTensa_exception_frame_s*, int));
void window_spill_exc_handler(struct XTensa_exception_frame_s *frame, int cause);
void print_fatal_exc_handler(struct XTensa_exception_frame_s *frame, int cause);

/* UART */
void uartAttach();
void Uart_Init(uint32_t uart_no);
void ets_install_uart_printf(uint32_t uart_no);
DWNLD_MODE UartConnCheck();

/* SPI */
uint32_t Wait_SPI_Idle(SpiFlashChip *fc);
void Cache_Read_Disable();

/* SDIO */
void sip_init_attach(uint8_t channel);

