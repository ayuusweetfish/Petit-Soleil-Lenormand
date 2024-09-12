#include <stm32g0xx_hal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../misc/bitmap_font/bitmap_font.h"
#include "../misc/rng/twofish.h"
#include "../misc/rng/xoodoo.h"

#define PIN_LED_R     GPIO_PIN_4
#define PIN_LED_G     GPIO_PIN_7
#define PIN_LED_B     GPIO_PIN_1
#define PIN_EP_NCS    GPIO_PIN_4
#define PIN_EP_DCC    GPIO_PIN_1
#define PIN_EP_NRST   GPIO_PIN_11
#define PIN_EP_BUSY   GPIO_PIN_12
#define PIN_BUTTON    GPIO_PIN_2
#define EXTI_LINE_BUTTON  EXTI_LINE_2
#define PIN_PWR_LATCH GPIO_PIN_3

static uint8_t swv_buf[256];
static size_t swv_buf_ptr = 0;
__attribute__ ((noinline, used))
void swv_trap_line()
{
  *(volatile char *)swv_buf;
}
static inline void swv_putchar(uint8_t c)
{
  // ITM_SendChar(c);
  if (c == '\n') {
    swv_buf[swv_buf_ptr >= sizeof swv_buf ?
      (sizeof swv_buf - 1) : swv_buf_ptr] = '\0';
    swv_trap_line();
    swv_buf_ptr = 0;
  } else if (++swv_buf_ptr <= sizeof swv_buf) {
    swv_buf[swv_buf_ptr - 1] = c;
  }
}
static void swv_printf(const char *restrict fmt, ...)
{
  char s[256];
  va_list args;
  va_start(args, fmt);
  int r = vsnprintf(s, sizeof s, fmt, args);
  for (int i = 0; i < r && i < sizeof s - 1; i++) swv_putchar(s[i]);
  if (r >= sizeof s) {
    for (int i = 0; i < 3; i++) swv_putchar('.');
    swv_putchar('\n');
  }
}

SPI_HandleTypeDef spi1 = { 0 };
ADC_HandleTypeDef adc1 = { 0 };
TIM_HandleTypeDef tim3, tim14, tim16, tim17;
RTC_HandleTypeDef rtc;

static volatile bool stopped = false;
static volatile bool btn_active = true; // Assume the button is pressed on boot
static volatile uint32_t btn_entropy = 0;
static uint32_t magical_intensity = 0;
static uint32_t magical_started_at = 0;

#pragma GCC push_options
#pragma GCC optimize("O3")
static inline void spi_transmit(const uint8_t *data, size_t size)
{
  HAL_SPI_Transmit(&spi1, (uint8_t *)data, size, 1000); return;
/*
  for (int i = 0; i < size; i++) {
    while (!(SPI1->SR & SPI_SR_TXE)) { }
    SPI1->DR = data[i];
  }
  while (!(SPI1->SR & SPI_SR_TXE)) { }
  while ((SPI1->SR & SPI_SR_BSY)) { }
  // Clear OVR flag
  (void)SPI1->DR;
  (void)SPI1->SR;
*/
}

static inline void spi_receive(uint8_t *data, size_t size)
{
  HAL_SPI_Receive(&spi1, data, size, 1000); return;
}

static inline void _epd_cmd(uint8_t cmd, const uint8_t *params, size_t params_size)
{
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NCS, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_DCC, GPIO_PIN_RESET);
  spi_transmit(&cmd, 1);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_DCC, GPIO_PIN_SET);
  if (params_size > 0) {
    spi_transmit(params, params_size);
  }
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NCS, GPIO_PIN_SET);
}
#define epd_cmd(_cmd, ...) do { \
  uint8_t params[] = { __VA_ARGS__ }; \
  _epd_cmd(_cmd, params, sizeof params); \
} while (0)
static inline bool epd_waitbusy()
{
  // while (HAL_GPIO_ReadPin(GPIOA, PIN_EP_BUSY) == GPIO_PIN_SET) { }
  uint32_t t0 = HAL_GetTick();
  while (HAL_GPIO_ReadPin(GPIOA, PIN_EP_BUSY) == GPIO_PIN_SET)
    if (HAL_GetTick() - t0 > 5000) {
      // Fail!
      TIM14->CCR1 = TIM3->CCR1 = TIM17->CCR1 = 0;
      for (int i = 0; i < 3; i++) {
        TIM3->CCR1 = 2000; HAL_Delay(100);
        TIM3->CCR1 =    0; HAL_Delay(100);
      }
      HAL_GPIO_WritePin(GPIOA, PIN_PWR_LATCH, 0);
      HAL_Delay(1000);
      NVIC_SystemReset();
      return false;
    }
  return true;
}
#pragma GCC pop_options

static inline void print_string(uint8_t *restrict buf, const uint16_t *restrict s, int r, int c)
{
  int c0 = c;
  for (; *s != '\0'; s++) {
    if (*s == '\n') {
      r += 17;
      c = c0;
    } else {
      uint8_t advance_x = bitmap_font_render_glyph(buf, 200, 200, *s, r, c);
      c += advance_x;
    }
  }
}

static void epd_reset(bool partial, bool power_save)
{
  // (1) HW & SW Reset
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NRST, GPIO_PIN_RESET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NRST, GPIO_PIN_SET);
  HAL_Delay(10);
  epd_waitbusy();
  // SW RESET
  epd_cmd(0x12);
  epd_waitbusy();
  // (2) Initialization
  // Driver Output control
  epd_cmd(0x01, 0xC7, 0x00, 0x01);
  // Data Entry mode setting
  epd_cmd(0x11, 0x01);  // Y-, X+
  // Border Waveform Control
  // 0x05 - VBD: GS Transition (Follow LUT, LUT1)
  // 0x80 - VBD: VCOM
  epd_cmd(0x3C, partial ? 0x80 : 0x05);
  // Temperature Sensor Control
  epd_cmd(0x18, 0x80);  // Internal sensor
  // Display Update Control 2; Master Activation
  // 0xB1: Load LUT with DISPLAY Mode 1
  // 0xB9: Load LUT with DISPLAY Mode 2
  epd_cmd(0x22, partial ? 0xB9 : 0xB1);
  // Master Activation
  epd_cmd(0x20);
  epd_waitbusy();

  if (power_save) {
    // Gate Driving voltage control
    // VGH = 10V
    epd_cmd(0x03, 0x03);
    // Source Driving voltage Control
    if (!partial) {
      // VSH1 = 6V, VSH2 = 2.4V, VSL = -9V
      epd_cmd(0x04, 0xB2, 0x8E, 0x1A);
    } else {
      // VSH1 = 9V, VSH2 = 3V, VSL = -12V
      epd_cmd(0x04, 0x23, 0x94, 0x26);
    }
  } else {
    // VSH1 = 9V, VSH2 = 3V, VSL = -12V
    epd_cmd(0x04, 0x23, 0x94, 0x26);
  }
  // Booster Soft start Control
  // All weakest strength and maximum duration
  // Min Off Time unchanged from POR value
  epd_cmd(0x0C, 0x8B, 0x8C, 0x86, 0x3F);

  // Set RAM X-address Start / End position
  epd_cmd(0x44, 0x00, 0x18);  // 0x18 = 200 / 8 - 1
  epd_cmd(0x45, 0xC7, 0x00, 0x00, 0x00);  // 0xC7 = 200 - 1
  // Set starting RAM location
  epd_cmd(0x4E, 0x00);
  epd_cmd(0x4F, 0xC7, 0x00);
}

static void epd_set_grey()
{
  epd_reset(false, true);
  // XXX: Maybe load custom waveform with 0x32?
  // Display Update Control 2
  epd_cmd(0x22, 0xB9);  // Load LUT with DISPLAY Mode 2
  // Master Activation
  epd_cmd(0x20);
  epd_waitbusy();
  // VGH = 10V
  epd_cmd(0x03, 0x03);
  // Source Driving voltage Control
  // VSH1 = 2.4V, VSH2 = 2.4V, VSL = -5V
  epd_cmd(0x04, 0x8E, 0x8E, 0x0A);
}

static void epd_write_ram(const uint8_t *pixels)
{
  _epd_cmd(0x24, pixels, 200 * 200 / 8);
  // XXX: Discard further data. This makes a difference in the cases where
  // soldering defects results in EPD_NCS always grounded (?!)
  epd_cmd(0x7f);
}

static void epd_write_ram_prev(const uint8_t *pixels)
{
  _epd_cmd(0x26, pixels, 200 * 200 / 8);
  epd_cmd(0x7f);
}

static void epd_display(bool partial)
{
  if (!partial) epd_cmd(0x22, 0xC7);  // DISPLAY with DISPLAY Mode 1
  else          epd_cmd(0x22, 0xCF);  // DISPLAY with DISPLAY Mode 2
  epd_cmd(0x20);
  epd_waitbusy();
}

static void epd_sleep()
{
  // Deep sleep
  // NOTE: Deep sleep mode 2 (0x10, 0x03) results in unstable display?
  epd_cmd(0x10, 0x01);
  // RAM does not need to be retained if the entire image is re-sent
}

#define flash_cs_0() (GPIOB->BSRR = (uint32_t)1 << (9 + 16))
#define flash_cs_1() (GPIOB->BSRR = (uint32_t)1 << (9))

static inline void spi_flash_tx_rx(
  uint8_t *txbuf, size_t txsize,
  uint8_t *rxbuf, size_t rxsize
) {
  flash_cs_0();
  spi_transmit(txbuf, txsize);
  if (rxsize != 0) {
    while (SPI1->SR & SPI_SR_BSY) { }
    spi_receive(rxbuf, rxsize);
  }
  flash_cs_1();
}

#define flash_cmd(_cmd) \
  spi_flash_tx_rx((_cmd), sizeof (_cmd), NULL, 0)
#define flash_cmd_sized(_cmd, _cmdlen) \
  spi_flash_tx_rx((_cmd), (_cmdlen), NULL, 0)
#define flash_cmd_bi(_cmd, _rxbuf) \
  spi_flash_tx_rx((_cmd), sizeof (_cmd), (_rxbuf), sizeof (_rxbuf))
#define flash_cmd_bi_sized(_cmd, _cmdlen, _rxbuf, _rxlen) \
  spi_flash_tx_rx((_cmd), (_cmdlen), (_rxbuf), (_rxlen))

uint8_t flash_status0()
{
  flash_cs_0();
  uint8_t op_read_status[] = {0x05};
  spi_transmit(op_read_status, sizeof op_read_status);
  uint8_t status0;
  spi_receive(&status0, 1);
  flash_cs_1();
  return status0;
}

__attribute__ ((noinline))
uint32_t flash_status_all()
{
  uint8_t op_read_status;
  uint8_t status[3];
  flash_cs_0();
  op_read_status = 0x05; spi_transmit(&op_read_status, 1); spi_receive(&status[0], 1);
  flash_cs_1();
  flash_cs_0();
  op_read_status = 0x35; spi_transmit(&op_read_status, 1); spi_receive(&status[1], 1);
  flash_cs_1();
  flash_cs_0();
  op_read_status = 0x15; spi_transmit(&op_read_status, 1); spi_receive(&status[2], 1);
  flash_cs_1();
  return ((uint32_t)status[2] << 16) | ((uint32_t)status[1] << 8) | status[0];
}

void flash_wait_poll(uint32_t interval_us)
{
  uint8_t status0;
  flash_cs_0();
  uint8_t op_read_status[] = {0x05};
  spi_transmit(op_read_status, sizeof op_read_status);
  do {
    // dwt_delay(interval_us * CYC_MICROSECOND);
    spi_receive(&status0, 1);
    // swv_printf("BUSY = %u, SysTick = %lu\n", status0 & 1, HAL_GetTick());
  } while (status0 & 1);
  flash_cs_1();
}

void flash_id(uint8_t jedec[3], uint8_t uid[4])
{
  uint8_t op_read_jedec[] = {0x9F};
  flash_cmd_bi_sized(op_read_jedec, sizeof op_read_jedec, jedec, 3);
  uint8_t op_read_uid[] = {0x4B, 0x00, 0x00, 0x00, 0x00};
  flash_cmd_bi_sized(op_read_uid, sizeof op_read_uid, uid, 4);
}

void flash_erase_4k(uint32_t addr)
{
  addr &= ~0xFFF;
  uint8_t op_write_enable[] = {0x06};
  flash_cmd(op_write_enable);
  uint8_t op_sector_erase[] = {
    0x20, // Sector Erase
    (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, (addr >> 0) & 0xFF,
  };
  flash_cmd(op_sector_erase);
  // Wait for completion (t_SE max. = 400 ms, typ. = 40 ms)
  flash_wait_poll(1000);
}

void flash_erase_64k(uint32_t addr)
{
  addr &= ~0xFFFF;
  uint8_t op_write_enable[] = {0x06};
  flash_cmd(op_write_enable);
  uint8_t op_sector_erase[] = {
    0xD8, // 64KB Block Erase
    (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, (addr >> 0) & 0xFF,
  };
  flash_cmd(op_sector_erase);
  // Wait for completion (t_BE2 max. = 2000 ms, typ. = 150 ms)
  flash_wait_poll(1000);
}

void flash_erase_chip()
{
  uint8_t op_write_enable[] = {0xC7};
  flash_cmd(op_write_enable);
  uint8_t op_chip_erase[] = {
    0x20, // Chip Erase
  };
  flash_cmd(op_chip_erase);
  // Wait for completion (t_CE max. = 25000 ms, typ. = 5000 ms)
  flash_wait_poll(10000);
}

void flash_write_page(uint32_t addr, uint8_t *data, size_t size)
{
  // assert(size > 0 && size <= 256);
  uint8_t op_write_enable[] = {0x06};
  flash_cmd(op_write_enable);
  uint8_t op_page_program[260] = {
    0x02, // Page Program
    (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, (addr >> 0) & 0xFF,
  };
  for (size_t i = 0; i < size; i++)
    op_page_program[4 + i] = data[i];
  flash_cmd_sized(op_page_program, 4 + size);
  // Wait for completion (t_PP max. = 3 ms, typ. = 0.4 ms)
  flash_wait_poll(100);
}

__attribute__ ((noinline))
void flash_read(uint32_t addr, uint8_t *data, size_t size)
{
  uint8_t op_read_data[] = {
    0x03, // Read Data
    (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, (addr >> 0) & 0xFF,
  };
  flash_cmd_bi_sized(op_read_data, sizeof op_read_data, data, size);
}

// TODO: Replace this with direct register access (SPIx->DR)
#define flash_read_op(_name, _op) \
void flash_read_##_name(uint32_t addr, uint8_t *data, size_t size) \
{ \
  uint8_t op_read_data[] = { \
    0x03, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, (addr >> 0) & 0xFF, \
  }; \
  flash_cs_0(); \
  spi_transmit(op_read_data, 4); \
  uint8_t rxbuf[16]; \
  for (size_t i = 0; i < size; i += 16) { \
    size_t rxsize = 16; \
    if (i + 16 > size) rxsize = size - i; \
    while (SPI1->SR & SPI_SR_BSY) { } \
    spi_receive(rxbuf, rxsize); \
    for (size_t j = 0; j < rxsize; j++) \
      data[i + j] _op (rxbuf[j]); \
  } \
  flash_cs_1(); \
}
flash_read_op(set, |=)  // flash_read_set
flash_read_op(xor, ^=)  // flash_read_xor
flash_read_op(and, &=)  // flash_read_and
flash_read_op(clear, &= ~)  // flash_read_clear

uint8_t flash_test_write_buf[256 * 8];

__attribute__ ((noinline))
void flash_test_write(uint32_t addr, size_t size)
{
  for (uint32_t block_start = 0; block_start < size; block_start += 256) {
    uint32_t block_size = 256;
    if (block_start + block_size >= size)
      block_size = size - block_start;
    flash_write_page(
      addr + block_start,
      flash_test_write_buf + block_start,
      block_size
    );
  }
}

void flash_test_write_breakpoint()
{
  swv_printf("all status = %06x\n", flash_status_all());
  if (*(volatile uint32_t *)flash_test_write_buf == 0x11223344) {
    uint8_t data[4] = {1, 2, 3, 4};
    flash_read(0, data, sizeof data);
    for (int i = 0; i < 4; i++) swv_printf("%d\n", data[i]);
    flash_erase_4k(0);
    flash_erase_64k(0);
    flash_erase_chip();
    flash_test_write(0, 1);
  }
  while (1) { }
}

#define FILE_ADDR___wenquanyi_9pt_bin 0
#define FILE_SIZE___wenquanyi_9pt_bin 704169
#define FILE_ADDR___cards_bin        704169
#define FILE_SIZE___cards_bin        468036

void bitmap_font_read_data(uint32_t glyph, uint8_t *buf)
{
  uint8_t idxbuf[2];
  flash_read(FILE_ADDR___wenquanyi_9pt_bin + glyph * 2, idxbuf, 2);
  uint16_t idx = ((uint16_t)idxbuf[0] << 8) | idxbuf[1];
  flash_read(FILE_ADDR___wenquanyi_9pt_bin + 0x20000 + (uint32_t)idx * 19, buf, 19);
}

static inline void sleep_delay(uint32_t ticks)
{
  uint32_t t0 = HAL_GetTick();
  while (HAL_GetTick() - t0 < ticks) {
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
  }
}

static inline void spin_delay(uint32_t cycles)
{
  // GCC (10.3.1 xPack) gives extraneous `cmp r0, #0`??
  // for (cycles = (cycles - 5) / 4; cycles > 0; cycles--) asm volatile ("");
  __asm__ volatile (
    "   cmp %[cycles], #5\n"
    "   ble 2f\n"
    "   sub %[cycles], #5\n"
    "   lsr %[cycles], #2\n"
    "1: sub %[cycles], #1\n"
    "   nop\n"
    "   bne 1b\n"   // 2 cycles if taken
    "2: \n"
    : [cycles] "+l" (cycles)
    : // No output
    : "cc"
  );
}

// Entropy from randomly initialised memory
// Accumulators in registers, avoiding reflexive operations in the stack
static void entropy_ram(uint32_t *pool)
{
  __asm__ (
    "   ldr r1, =%[start_addr]\n" // r1 - Pointer
    "   ldr r2, =%[end_addr]\n"   // r2 - End address

    // r4-11 - Eight accumulator words
    "   mov r4, #0\n"
    "   mov r5, #0\n"
    "   mov r6, #0\n"
    "   mov r7, #0\n"
    "   mov r8, r7\n"
    "   mov r9, r7\n"
    "   mov r10, r7\n"
    "   mov r11, r7\n"

    "1: \n"
    // r3 - Load destination
    "   ldr r3, [r1, #0]\n"
    "   add r4, r3\n"
    "   ldr r3, [r1, #4]\n"
    "   add r5, r3\n"
    "   ldr r3, [r1, #8]\n"
    "   add r6, r3\n"
    "   ldr r3, [r1, #12]\n"
    "   add r7, r3\n"
    "   ldr r3, [r1, #16]\n"
    "   add r8, r3\n"
    "   ldr r3, [r1, #20]\n"
    "   add r9, r3\n"
    "   ldr r3, [r1, #24]\n"
    "   add r10, r3\n"
    "   ldr r3, [r1, #28]\n"
    "   add r11, r3\n"
    "   add r1, #32\n"
    "   cmp r1, r2\n"
    "   bl  1b\n"

    // Write to memory
    "   str r4, [%[pool_addr], #0]\n"
    "   str r5, [%[pool_addr], #4]\n"
    "   str r6, [%[pool_addr], #8]\n"
    "   str r7, [%[pool_addr], #12]\n"
    "   mov r8, r4\n"
    "   str r4, [%[pool_addr], #16]\n"
    "   mov r9, r4\n"
    "   str r4, [%[pool_addr], #20]\n"
    "   mov r10, r4\n"
    "   str r4, [%[pool_addr], #24]\n"
    "   mov r11, r4\n"
    "   str r4, [%[pool_addr], #28]\n"
    :
    : [start_addr] "i" (0x20000000),
      [end_addr]   "i" (0x20002000),
      [pool_addr]  "r" (pool)   // r0
    : "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "cc", "memory"
  );
}

// Requires `n` to be even
// Also mixes in TIM3, if it is enabled
static inline void entropy_adc(uint32_t *out_v, int n)
{
  ADC_ChannelConfTypeDef adc_ch13;
  adc_ch13.Channel = ADC_CHANNEL_VREFINT;
  adc_ch13.Rank = ADC_REGULAR_RANK_1;
  adc_ch13.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;

  ADC_ChannelConfTypeDef adc_ch_temp;
  adc_ch_temp.Channel = ADC_CHANNEL_TEMPSENSOR;
  adc_ch_temp.Rank = ADC_REGULAR_RANK_1;
  adc_ch_temp.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;

  // 1000 samples takes 167 ms
while (0) {
  HAL_ADC_ConfigChannel(&adc1, &adc_ch13);
  uint32_t t0 = HAL_GetTick();
  for (int i = 0; i < 1000; i++) {
    HAL_ADC_Start(&adc1);
    HAL_ADC_PollForConversion(&adc1, 1000);
    HAL_ADC_GetValue(&adc1);
    HAL_ADC_Stop(&adc1);
  }
  uint32_t t1 = HAL_GetTick();
  swv_printf("%u - %u\n", t0, t1 - t0);
}

  // for (int i = 0; i < n; i++) out_v[i] = 0;

  for (int ch = 0; ch <= 1; ch++) {
    if (ch == 0) HAL_ADC_ConfigChannel(&adc1, &adc_ch13);
    else HAL_ADC_ConfigChannel(&adc1, &adc_ch_temp);
    uint32_t v = 0;
    for (int i = 0; i < n * 2; i++) {
      HAL_ADC_Start(&adc1);
      HAL_ADC_PollForConversion(&adc1, 1000);
      uint32_t adc_value = HAL_ADC_GetValue(&adc1);
      // ADC has independent clock, but TIM3 lowest bit does not seem to change (stays at 1)?
      uint32_t tim_cnt = (TIM3->CNT >> 1) ^ (TIM16->CCR1 << 4);
      v = (v << 8) | ((adc_value ^ (tim_cnt << 2) ^ (tim_cnt >> 2)) & 0xff);
      if (i % 4 == 3) out_v[i / 4 * 2 + ch] ^= v;
    }
  }
  HAL_ADC_Stop(&adc1);

  // for (int i = 0; i < n; i++) swv_printf("%08x%c", out_v[i], i % 10 == 9 ? '\n' : ' ');
  // while (1) { }
}

static inline void entropy_clocks_start()
{
  HAL_RCC_OscConfig(&(RCC_OscInitTypeDef){
    .OscillatorType = RCC_OSCILLATORTYPE_LSI,
    .LSIState = RCC_LSI_ON,
  });

  __HAL_RCC_TIM16_CLK_ENABLE();
  tim16 = (TIM_HandleTypeDef){
    .Instance = TIM16,
    .Init = {
      .Prescaler = 1 - 1,
      .CounterMode = TIM_COUNTERMODE_UP,
      .Period = 65536 - 1,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
      .RepetitionCounter = 0,
    },
  };
  HAL_TIM_IC_Init(&tim16);

  TIM_ClockConfigTypeDef tim16_cfg_ti1 = {
    .ClockSource = TIM_CLOCKSOURCE_TI1,
    .ClockPolarity = TIM_CLOCKPOLARITY_RISING,
    .ClockPrescaler = TIM_CLOCKPRESCALER_DIV1,
    .ClockFilter = 0,
  };
  HAL_TIM_ConfigClockSource(&tim16, &tim16_cfg_ti1);
  HAL_TIMEx_TISelection(&tim16, TIM_TIM16_TI1_LSI, TIM_CHANNEL_1);

  HAL_TIM_IC_ConfigChannel(&tim16, &(TIM_IC_InitTypeDef){
    .ICPolarity = TIM_ICPOLARITY_BOTHEDGE,
    .ICSelection = TIM_ICSELECTION_DIRECTTI,
    .ICPrescaler = TIM_ICPSC_DIV1,
    .ICFilter = 0,
  }, TIM_CHANNEL_1);
  HAL_TIM_IC_Start(&tim16, TIM_CHANNEL_1);
}

static inline void entropy_clocks_stop()
{
  // Stop timer and restore clock source to SYSCLK
  HAL_TIM_IC_Stop(&tim16, TIM_CHANNEL_1);
  HAL_TIM_IC_DeInit(&tim16);
  TIM_ClockConfigTypeDef tim16_cfg_int = {
    .ClockSource = TIM_CLOCKSOURCE_INTERNAL,
    .ClockPolarity = TIM_CLOCKPOLARITY_RISING,
    .ClockPrescaler = TIM_CLOCKPRESCALER_DIV1,
    .ClockFilter = 0,
  };
  HAL_TIM_ConfigClockSource(&tim16, &tim16_cfg_int);
  __HAL_RCC_TIM16_CLK_DISABLE();

  HAL_RCC_OscConfig(&(RCC_OscInitTypeDef){
    .OscillatorType = RCC_OSCILLATORTYPE_LSI,
    .LSIState = RCC_LSI_OFF,
  });
}

#pragma GCC push_options
#pragma GCC optimize("O3")
static inline void entropy_jitter(uint32_t *_s, int n)
{
  uint16_t *s = (uint16_t *)_s;
  uint16_t capt = TIM16->CCR1;
  uint16_t acc = s[n * 2 - 2] ^ s[n * 2 - 1];
  s[n * 2 - 2] ^= capt;
  s[n * 2 - 1] ^= TIM16->CNT;
  for (int i = 0; i < n * 2; i++) {
    for (int j = (acc >> 3) & 3; j >= 0; j--) {
      uint16_t new_capt;
      while ((new_capt = TIM16->CCR1) == capt) { }
      s[i] = (s[i] << 7) | (s[i] >> 9);
      s[i] += new_capt;
    }
    acc += s[i];
  }
}

static inline void mix(uint32_t *pool, uint32_t n, uint32_t n_round)
{
  static const uint32_t xoodoo_rc[12] = {
    0x00000058, 0x00000038, 0x000003C0, 0x000000D0,
    0x00000120, 0x00000014, 0x00000060, 0x0000002C,
    0x00000380, 0x000000F0, 0x000001A0, 0x00000012,
  };
  for (int i = 0; i < 3; i++) {
    uint32_t rc = xoodoo_rc[(n_round % 4) * 3 + i];
    xoodoo(pool, rc);
    xoodoo(pool + (n - 12), rc);
  }
}

static int draw_card(const uint32_t *pool, const size_t len)
{
  uint32_t key[8] = { 0 };
  for (int i = 0; i < len; i++) key[i % 8] ^= pool[i];
  twofish_set_key(key, 256);

  int n_itrs = (len + 3) / 4;
  uint32_t block[2][4] = {{ 0 }};
  uint32_t accum = 0;
  // Twofish cipher in CBC mode
  for (int it = 0; it < n_itrs * 2; it++) {
    uint32_t *plain = block[it % 2];
    uint32_t *cipher = block[(it % 2) ^ 1];
    for (int i = 0; i < 4; i++) plain[i] ^= pool[(it * 4 + i) % len];
    twofish_encrypt(plain, cipher);
    for (int i = 0; i < 4; i++) accum ^= cipher[i];
    if (it > n_itrs && accum < 0x100000000 - 0x100000000 % 37 && accum % 37 != 0) break;
    if (it == n_itrs * 2 - 1) accum = 34;
  }

  uint8_t card_id = accum % 37 - 1;
  return card_id;
}

void setup_clocks()
{
  HAL_PWREx_EnableLowPowerRunMode();

  RCC_OscInitTypeDef osc_init = { 0 };
  osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc_init.HSIState = RCC_HSI_ON;
  osc_init.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc_init.PLL.PLLState = RCC_PLL_OFF;
  HAL_RCC_OscConfig(&osc_init);

  RCC_ClkInitTypeDef clk_init = { 0 };
  clk_init.ClockType =
    RCC_CLOCKTYPE_SYSCLK |
    RCC_CLOCKTYPE_HCLK |
    RCC_CLOCKTYPE_PCLK1;
  clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_HSI; // 16 MHz
  clk_init.AHBCLKDivider = RCC_SYSCLK_DIV4;     // 4 MHz
  clk_init.APB1CLKDivider = RCC_HCLK_DIV4;      // 4 MHz
  HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_2);

  entropy_clocks_start();
}
#pragma GCC pop_options

int main()
{
  uint32_t pool[20];
  entropy_ram(pool);

  HAL_Init();

  // ======== GPIO ========
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef gpio_init;

  // SWD (PA13, PA14)
  gpio_init.Pin = GPIO_PIN_13 | GPIO_PIN_14;
  gpio_init.Mode = GPIO_MODE_AF_PP; // Pass over control to AF peripheral
  gpio_init.Alternate = GPIO_AF0_SWJ;
  gpio_init.Pull = GPIO_PULLUP;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio_init);

  // ======= Power latch ======
  gpio_init = (GPIO_InitTypeDef){
    .Pin = PIN_PWR_LATCH,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW,
  };
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, PIN_PWR_LATCH, 1);

  // ======== Clocks ========
  setup_clocks();
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

  // Start the lights as soon as possible
  // ======== TIM3, used during ADC entropy accumulation and magical lights ========
  __HAL_RCC_TIM3_CLK_ENABLE();
  tim3 = (TIM_HandleTypeDef){
    .Instance = TIM3,
    .Init = {
      .Prescaler = 1 - 1,
      .CounterMode = TIM_COUNTERMODE_UP,
      .Period = 4000 - 1,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
      .RepetitionCounter = 0,
    },
  };
  HAL_TIM_Base_Init(&tim3);
  HAL_TIM_Base_Start_IT(&tim3);

  HAL_TIM_PWM_Init(&tim3);
  TIM_OC_InitTypeDef tim3_ch1_oc_init = {
    .OCMode = TIM_OCMODE_PWM2,
    .Pulse = 0, // to be filled
    .OCPolarity = TIM_OCPOLARITY_LOW,
  };
  HAL_TIM_PWM_ConfigChannel(&tim3, &tim3_ch1_oc_init, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&tim3, TIM_CHANNEL_1);

  // ======== LED Timers ========
  // APB1 = 16 MHz
  // period = 4 kHz = 4000 cycles

  // LED Blue, TIM14
  gpio_init.Pin = GPIO_PIN_1;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Alternate = GPIO_AF0_TIM14;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio_init);
  __HAL_RCC_TIM14_CLK_ENABLE();
  tim14 = (TIM_HandleTypeDef){
    .Instance = TIM14,
    .Init = {
      .Prescaler = 1 - 1,
      .CounterMode = TIM_COUNTERMODE_UP,
      .Period = 4000 - 1,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
      .RepetitionCounter = 0,
    },
  };
  HAL_TIM_PWM_Init(&tim14);
  TIM_OC_InitTypeDef tim14_ch1_oc_init = {
    .OCMode = TIM_OCMODE_PWM2,
    .Pulse = 0, // to be filled
    .OCPolarity = TIM_OCPOLARITY_LOW,
  };
  HAL_TIM_PWM_ConfigChannel(&tim14, &tim14_ch1_oc_init, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&tim14, TIM_CHANNEL_1);

  // LED Red, TIM3
  gpio_init.Pin = GPIO_PIN_4;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Alternate = GPIO_AF1_TIM3;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio_init);

  // LED Green, TIM17
  gpio_init.Pin = GPIO_PIN_7;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Alternate = GPIO_AF2_TIM17;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio_init);
  __HAL_RCC_TIM17_CLK_ENABLE();
  tim17 = (TIM_HandleTypeDef){
    .Instance = TIM17,
    .Init = {
      .Prescaler = 1 - 1,
      .CounterMode = TIM_COUNTERMODE_UP,
      .Period = 4000 - 1,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
      .RepetitionCounter = 0,
    },
  };
  HAL_TIM_PWM_Init(&tim17);
  TIM_OC_InitTypeDef tim17_ch1_oc_init = {
    .OCMode = TIM_OCMODE_PWM2,
    .Pulse = 1, // to be filled
    .OCNPolarity = TIM_OCNPOLARITY_LOW,  // Output is TIM17_CH1N
  };
  HAL_TIM_PWM_ConfigChannel(&tim17, &tim17_ch1_oc_init, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&tim17, TIM_CHANNEL_1);

  // ======== Magic colours! ========
  HAL_NVIC_SetPriority(TIM3_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(TIM3_IRQn);

  // Activate EPD driver (SSD1681) reset signal
  gpio_init = (GPIO_InitTypeDef){
    .Pin = PIN_EP_NRST,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW,
  };
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NRST, 0);

  // ======== Button ========
  gpio_init.Pin = PIN_BUTTON;
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &gpio_init);

  EXTI_HandleTypeDef exti_handle = {
    // .Line = 2,
    .RisingCallback = NULL,
    .FallingCallback = NULL,
  };
  EXTI_ConfigTypeDef exti_cfg = {
    .Line = EXTI_LINE_BUTTON,
    .Mode = EXTI_MODE_INTERRUPT,
    .Trigger = EXTI_TRIGGER_RISING_FALLING,
    .GPIOSel = EXTI_GPIOA,
  };
  HAL_EXTI_SetConfigLine(&exti_handle, &exti_cfg);

  // Interrupt
  magical_started_at = HAL_GetTick();
  magical_intensity = 65536 / 8;
  HAL_NVIC_SetPriority(EXTI2_3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);

  btn_active = !(GPIOA->IDR & PIN_BUTTON);

// Returns false for a short press, and true for a long press (exceeding `max_dur`)
bool stop_wait_button(uint32_t min_dur, uint32_t max_dur)
{
  while (true) {
    // while (HAL_GPIO_ReadPin(GPIOA, PIN_BUTTON) == 1) sleep_delay(10);

    HAL_SuspendTick();

    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF);
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&rtc, RTC_FLAG_WUTF);
    // 117 = 120 s * (32 kHz / (0x7F + 1) * (0xFF + 1))
    HAL_RTCEx_SetWakeUpTimer_IT(&rtc, 117, RTC_WAKEUPCLOCK_CK_SPRE_16BITS);

    EXTI->RTSR1 &= ~EXTI_LINE_BUTTON; // Disable rising trigger
    stopped = true;

    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    EXTI->RTSR1 |=  EXTI_LINE_BUTTON; // Enable rising trigger

    HAL_RTCEx_DeactivateWakeUpTimer(&rtc);

    HAL_ResumeTick();

    btn_active = true;
    uint32_t t0 = HAL_GetTick();
    while (btn_active) {
      if (HAL_GetTick() - t0 >= max_dur) return true; // Long press
      sleep_delay(1);
    }
    uint32_t t = HAL_GetTick() - t0;
    if (t >= min_dur) return false; // Short press
  }
}

  setup_clocks();

  // ======== RTC ========
  // https://community.st.com/t5/stm32-mcus-embedded-software/stm32g071rb-rtc-init-bug/m-p/331272/highlight/true#M23692
  // https://community.st.com/t5/stm32-mcus-products/stm32g030-can-t-setup-rtc-hal-rtc-init-always-return-hal-timeout/td-p/692500
  // https://stackoverflow.com/q/43210417
  // When HAL_RTC_Init() returns HAL_TIMEOUT,
  // wake-up interrupt by RTCCLK/16 (RTC_WAKEUPCLOCK_RTCCLK_DIV16) might still work
  // but ck_spre (RTC_WAKEUPCLOCK_CK_SPRE_16BITS)
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_RTCAPB_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  // Backup domain reset
  // Needs to be issued after backup domain unlock: http://www.efton.sk/STM32/gotcha/g62.html
  RCC->BDCR |=  RCC_BDCR_BDRST; spin_delay(1000);
  RCC->BDCR &= ~RCC_BDCR_BDRST; spin_delay(1000);

  __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
/*
  // Equivalent:
  HAL_RCCEx_PeriphCLKConfig(&(RCC_PeriphCLKInitTypeDef){
    .PeriphClockSelection = RCC_PERIPHCLK_RTC,
    .RTCClockSelection = RCC_RTCCLKSOURCE_LSI,
  });
*/
  __HAL_RCC_RTC_ENABLE(); // Put before `HAL_RTC_Init()`!

  rtc = (RTC_HandleTypeDef){
    .Instance = RTC,
    .Init = (RTC_InitTypeDef){
      .HourFormat = RTC_HOURFORMAT_24,
      .AsynchPrediv = 0x7F,
      .SynchPrediv = 0xFF,
      .OutPut = RTC_OUTPUT_WAKEUP,
      .OutPutRemap = RTC_OUTPUT_REMAP_NONE,
      .OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH,
      .OutPutType = RTC_OUTPUT_TYPE_PUSHPULL,
      .OutPutPullUp = RTC_OUTPUT_PULLUP_NONE,
    },
  };
  HAL_RTC_Init(&rtc);

  HAL_RTCEx_DeactivateWakeUpTimer(&rtc); // Prevent extraneous interrupts during debug
  __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF);
  __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&rtc, RTC_FLAG_WUTF);
  HAL_NVIC_SetPriority(RTC_TAMP_IRQn, 1, 1);
  HAL_NVIC_EnableIRQ(RTC_TAMP_IRQn);

  // ======== ADC ========
  gpio_init.Pin = GPIO_PIN_0;
  gpio_init.Mode = GPIO_MODE_ANALOG;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio_init);

  __HAL_RCC_ADC_CLK_ENABLE();
  adc1.Instance = ADC1;
  adc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  adc1.Init.Resolution = ADC_RESOLUTION_12B;
  adc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  adc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  adc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  adc1.Init.LowPowerAutoWait = DISABLE;
  adc1.Init.LowPowerAutoPowerOff = ENABLE;
  adc1.Init.ContinuousConvMode = DISABLE;
  adc1.Init.NbrOfConversion = 1;
  adc1.Init.DiscontinuousConvMode = DISABLE;
  adc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  adc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_LOW;
  HAL_ADC_Init(&adc1);

  HAL_ADCEx_Calibration_Start(&adc1);

  // Wait some time for the voltage to recover?
  sleep_delay(2);

  uint32_t adc_vrefint, adc_vri, vri_mV;

void sense_vri()
{
  ADC_ChannelConfTypeDef adc_ch13;
  adc_ch13.Channel = ADC_CHANNEL_VREFINT;
  adc_ch13.Rank = ADC_REGULAR_RANK_1;
  adc_ch13.SamplingTime = ADC_SAMPLETIME_79CYCLES_5; // Stablize
  HAL_ADC_ConfigChannel(&adc1, &adc_ch13);
  HAL_ADC_Start(&adc1);
  HAL_ADC_PollForConversion(&adc1, 1000);
  adc_vrefint = HAL_ADC_GetValue(&adc1);
  HAL_ADC_Stop(&adc1);
  // swv_printf("ADC VREFINT cal = %lu\n", *VREFINT_CAL_ADDR);
  // swv_printf("ADC VREFINT = %lu\n", adc_vrefint);
  // VREFINT cal = 1667, VREFINT read = 1550 -> VDD = 1667/1550 * 3 V = 3.226 V

  ADC_ChannelConfTypeDef adc_ch0;
  adc_ch0.Channel = ADC_CHANNEL_0;
  adc_ch0.Rank = ADC_REGULAR_RANK_1;
  adc_ch0.SamplingTime = ADC_SAMPLETIME_79CYCLES_5; // Stablize
  HAL_ADC_ConfigChannel(&adc1, &adc_ch0);
  HAL_ADC_Start(&adc1);
  HAL_ADC_PollForConversion(&adc1, 1000);
  adc_vri = HAL_ADC_GetValue(&adc1);
  HAL_ADC_Stop(&adc1);
  // swv_printf("ADC VRI = %lu\n", adc_vri);
  // VREFINT cal = 1656, VREFINT read = 1542, VRI read = 3813
  // -> VRI = 3813/4095 * (1656/1542 * 3 V) = 3.00 V

  vri_mV = (uint32_t)(3000ULL * adc_vri * (*VREFINT_CAL_ADDR) / (4095 * adc_vrefint));
  // swv_printf("VRI = %lu mV\n", vri_mV);
}
  sense_vri();

  // ======== SPI ========
  // GPIO ports
  // SPI1_SCK (PA5), SPI1_MISO (PA6), SPI1_MOSI (PA7)
  gpio_init.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Alternate = GPIO_AF0_SPI1;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio_init);
  // Output FLASH_CSN (PB9)
  HAL_GPIO_Init(GPIOB, &(GPIO_InitTypeDef){
    .Pin = GPIO_PIN_9,
    .Mode = GPIO_MODE_OUTPUT_PP,
  });
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 1);
  // Output EP_CSN (PA4), EP_DCC (PA6 (Rev. 4) / PA1 (Rev. 5))
  // EP_NRST (PA11) is initialised earlier
  gpio_init.Pin = PIN_EP_NCS | PIN_EP_DCC;
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NCS, 1);
  // while (HAL_GPIO_ReadPin(GPIOA, PIN_EP_NCS) == 0) { }
  HAL_GPIO_WritePin(GPIOA, PIN_EP_DCC, 0);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NRST, 0);
  // Input EP_BUSY (PA12)
  gpio_init.Pin = PIN_EP_BUSY;
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &gpio_init);

  __HAL_RCC_SPI1_CLK_ENABLE();
  spi1.Instance = SPI1;
  spi1.Init.Mode = SPI_MODE_MASTER;
  spi1.Init.Direction = SPI_DIRECTION_2LINES;
  spi1.Init.CLKPolarity = SPI_POLARITY_LOW; // CPOL = 0
  spi1.Init.CLKPhase = SPI_PHASE_1EDGE;     // CPHA = 0
  spi1.Init.NSS = SPI_NSS_SOFT;
  spi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  spi1.Init.TIMode = SPI_TIMODE_DISABLE;
  spi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  spi1.Init.DataSize = SPI_DATASIZE_8BIT;
  spi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;  // APB / 2 = 2 MHz
  HAL_SPI_Init(&spi1);
  __HAL_SPI_ENABLE(&spi1);

  // Deep sleep
  epd_cmd(0x10, 0x01);

  TIM14->CCR1 = TIM3->CCR1 = TIM17->CCR1 = 0;

  // Flash test
  uint8_t jedec[3], flash_uid[4];
  flash_id(jedec, flash_uid);
  // Manufacturer = 0xef (Winbond)
  // Memory type = 0x40
  // Capacity = 0x15 (2^21 B = 2 MiB = 16 Mib)
  // swv_printf("MF = %02x\nID = %02x %02x\nUID = %02x%02x%02x%02x\n",
  //   jedec[0], jedec[1], jedec[2], flash_uid[0], flash_uid[1], flash_uid[2], flash_uid[3]);
#define MANUFACTURE 0
#if MANUFACTURE
  flash_test_write_breakpoint();
#endif

  // ======== Entropy accumulation ========
  // 0~7 initialised with RAM data, see start of `main()`
  // Device signature
  pool[ 8] ^= *(uint32_t *)(UID_BASE + 0);
  pool[ 9] ^= *(uint32_t *)(UID_BASE + 4);
  pool[10] ^= *(uint32_t *)(UID_BASE + 8);
  pool[11] ^= LL_RCC_HSI_GetCalibration();
  pool[12] ^= *TEMPSENSOR_CAL1_ADDR;
  pool[13] ^= *TEMPSENSOR_CAL2_ADDR;
  pool[14] ^= *VREFINT_CAL_ADDR;
  pool[15] ^= 0 | ((uint32_t)flash_uid[0] << 24) | ((uint32_t)flash_uid[1] << 16)
                | ((uint32_t)flash_uid[2] <<  8) | ((uint32_t)flash_uid[3] <<  0);
  // Voltages
  pool[16] ^= adc_vrefint;
  pool[17] ^= adc_vri;

while (0) {
  uint32_t t0 = HAL_GetTick();
  entropy_adc(pool, 20);
  uint32_t t1 = HAL_GetTick();
  entropy_jitter(pool, 20);
  uint32_t t2 = HAL_GetTick();
  swv_printf("%u %u\n", t1 - t0, t2 - t1);  // 8~9 0~1
}

redraw:
  magical_started_at = HAL_GetTick();
  magical_intensity = 65536 / 8;
  __HAL_RCC_TIM3_CLK_ENABLE();

  entropy_adc(pool, 20);
  entropy_jitter(pool, 20);
  mix(pool, 20, 0);

  // swv_printf("tick = %u\n", HAL_GetTick()); // tick = 18

  // ======== Accumulate entropy while the magical lights flash! ========
  const uint32_t sample_interval = 40;
  uint32_t btn_released_at = 0;
  uint32_t n_rounds = 0;
  uint32_t time_spent = 0;
  uint32_t last_sample = HAL_GetTick() - sample_interval;
  while (btn_released_at == 0 || HAL_GetTick() - btn_released_at < 2000) {
    if (btn_released_at == 0) {
      if (!btn_active) btn_released_at = HAL_GetTick();
    } else {
      uint32_t t = HAL_GetTick() - btn_released_at;
      if (t >= 1500)
        magical_intensity = 65536 / 8 * (2000 - t) / 500 * (2000 - t) / 500;
    }
    if (HAL_GetTick() - last_sample >= sample_interval) {
      uint32_t t0 = HAL_GetTick();
      entropy_adc(pool, 20);
      entropy_jitter(pool, 20);
      pool[0] ^= btn_entropy;
      mix(pool, 20, ++n_rounds);
      entropy_jitter(pool, 20);
      last_sample += 40;
      time_spent += (HAL_GetTick() - t0);
    }
    sleep_delay(1);
  }
  time_spent /= n_rounds;

  magical_intensity = 0;
  TIM14->CCR1 = TIM3->CCR1 = TIM17->CCR1 = 0;
  spin_delay(4000);
  __HAL_RCC_TIM3_CLK_DISABLE();

  // Random!
  uint8_t card_id = draw_card(pool, sizeof pool / sizeof pool[0]);
  // XXX: Debug use only
if (1) {
  static const uint8_t candidate_cards[] = {8, 9, 32, 34};
  card_id = candidate_cards[card_id % (sizeof candidate_cards)] - 1;
}

  // ======== Drive display ========
  __attribute__ ((section (".noinit")))
  static uint8_t pixels[200 * 200 / 8];

  uint8_t side;
  flash_read(FILE_ADDR___cards_bin + card_id * 13001 + 10000, &side, 1);
  int offs = (side == 0 ? 0 : 200 / 8 * 160);

// For inspection
if (0) {
  for (int i = 0; i < 20; i++) pool[i] = 0;
  entropy_jitter(pool, 20);
}

void display_image(int stage)
{
if (stage == -1) {
  // For testing
  epd_reset(false, false);
  for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] = 0xff;
  static char s[64];
  snprintf(s, sizeof s, "%08lx %08lx\n%08lx %08lx", pool[0], pool[1], pool[2], pool[3]);
  static uint16_t s16[64];
  for (int i = 0; i < 64; i++) s16[i] = s[i];
  print_string(pixels, s16, 3, 3);
  epd_write_ram(pixels);
  epd_display(false);

} else if (stage == 0 || stage == 3) {
  if (stage == 0)
    epd_reset(false, false && vri_mV < 2400);
    // XXX: Power-save mode results in dimmed shadows (which is preferred)
    // Maybe we really need to investigate the custom waveform
  else
    epd_reset(true, false);

  // Read image
  flash_read(FILE_ADDR___cards_bin + card_id * 13001, pixels, 200 * 200 / 8);
  // Write pixel data
  if (stage == 3) {
    for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] ^= 0xff;
    epd_write_ram_prev(pixels);
    for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] ^= 0xff;
  }
  epd_write_ram(pixels);
  if (stage == 0)
    epd_display(false);
  else
    epd_display(true);

  // Greyscale
  epd_set_grey();
  if (stage == 0)
    epd_write_ram_prev(pixels);
  flash_read_and(FILE_ADDR___cards_bin + card_id * 13001 + 5000, pixels, 200 * 200 / 8);
  if (stage == 3) {
    for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] ^= 0xff;
    epd_write_ram_prev(pixels);
    for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] ^= 0xff;
  }
  epd_write_ram(pixels);
  epd_display(true);
  epd_sleep();

} else if (stage == 1) {
  epd_reset(true, true);

  flash_read(FILE_ADDR___cards_bin + card_id * 13001, pixels, 200 * 200 / 8);
  // Alpha
  flash_read_clear(FILE_ADDR___cards_bin + card_id * 13001 + 11001, pixels + offs, 200 * 40 / 8);
  // Colour
  flash_read_xor(FILE_ADDR___cards_bin + card_id * 13001 + 10001, pixels + offs, 200 * 40 / 8);
  // "Previous image" buffer; hence inverted for the name region
  epd_write_ram_prev(pixels);

  // Xor by alpha mask
  flash_read_xor(FILE_ADDR___cards_bin + card_id * 13001 + 11001, pixels + offs, 200 * 40 / 8);
  // Write pixel data
  epd_write_ram(pixels);
  epd_display(true);
  epd_sleep();

} else if (stage == 2) {
  epd_reset(true, false);
  // Clear
  for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] = 0xff;
  // Alpha
  flash_read_set(FILE_ADDR___cards_bin + card_id * 13001 + 11001, pixels + offs, 200 * 40 / 8);
  // Colour
  flash_read_xor(FILE_ADDR___cards_bin + card_id * 13001 + 10001, pixels + offs, 200 * 40 / 8);

  // Print text
  uint16_t cmt_text[40];
  flash_read(FILE_ADDR___cards_bin + card_id * 13001 + 12001, (uint8_t *)cmt_text, 80);
  for (int i = 0; i < 39; i++) cmt_text[i] = __builtin_bswap16(cmt_text[i]);
  cmt_text[39] = 0;
  print_string(pixels, cmt_text, 3 + (side == 0 ? 40 : 0), 3);

  // Print string
  uint16_t voltage_str[] = {'0', '.', '0', '0', '0', ' ', 'V', '\0'};
  // char16_t voltage_str[] = u"0.000 V";
  voltage_str[0] = '0' + vri_mV / 1000;
  voltage_str[2] = '0' + vri_mV / 100 % 10;
  voltage_str[3] = '0' + vri_mV / 10 % 10;
  voltage_str[4] = '0' + vri_mV % 10;
  print_string(pixels, voltage_str, 88, 3);

  uint16_t n_rounds_str[] = {' ', ' ', ' ', ' ', ' ', ' ', 'r', 'o', 'u', 'n', 'd', 's', '\0'};
  for (int i = 4, n = n_rounds; i >= 0 && n > 0; i--, n /= 10)
    n_rounds_str[i] = '0' + n % 10;
  print_string(pixels, n_rounds_str, 105, 3);

  uint16_t time_str[] = {' ', ' ', ' ', ' ', ' ', ' ', 'm', 's', '\0'};
  for (int i = 4, n = time_spent; i >= 0 && n > 0; i--, n /= 10)
    time_str[i] = '0' + n % 10;
  print_string(pixels, time_str, 122, 3);

  uint16_t btn_entropy_str[9] = { 0 };
  for (int i = 0; i < 8; i++)
    btn_entropy_str[i] = "0123456789abcdef"[(pool[0] >> ((7 - i) * 4)) % 16];
  print_string(pixels, btn_entropy_str, 139, 3);

  for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] ^= 0xff;
  epd_write_ram_prev(pixels);
  for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] ^= 0xff;
  epd_write_ram(pixels);
  epd_display(true);
  epd_sleep();
}
}

if (0) {
  display_image(-1);
  HAL_GPIO_WritePin(GPIOA, PIN_PWR_LATCH, 0);
  while (1) { }
}

  for (int i = 0; ; i = (i == 3 ? 1 : i + 1)) {
    display_image(i);
    // Blink green
    for (int i = 0; i < 1; i++) {
      TIM17->CCR1 = 500; sleep_delay(100);
      TIM17->CCR1 = 0; sleep_delay(100);
    }

    if (stop_wait_button(50, 1000)) {
      sense_vri();
      goto redraw;
    }
  }

  HAL_GPIO_WritePin(GPIOA, PIN_PWR_LATCH, 0);
  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  while (1) { }
}

void SysTick_Handler()
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();

  static uint32_t debounce = 0;
  uint32_t btn_state = !(GPIOA->IDR & PIN_BUTTON);
  if (btn_active != btn_state) {
    if (++debounce == 5) {
      btn_active = btn_state;
      debounce = 0;
    }
  } else {
    debounce = 0;
  }
}

void EXTI2_3_IRQHandler()
{
  if (stopped) {
    setup_clocks();
    stopped = false;
  }
#define rotl32(_x, _n) (((_x) << (_n)) | ((_x) >> (32 - (_n))))
  btn_entropy = rotl32(btn_entropy, 27) + ((TIM3->CNT << 16) | TIM16->CCR1);
  __HAL_GPIO_EXTI_CLEAR_IT(PIN_BUTTON); // Clears both rising and falling signals
}

void TIM3_IRQHandler()
{
  // Clear interrupt flag
  TIM3->SR = ~TIM_FLAG_UPDATE;

/*
from math import *
N=360
print(', '.join('%d' % round(8000*(1+sin(i/N*2*pi))) for i in range(N)))
*/
#define N 360
  static const uint16_t sin_lut[N] = {
8000, 8140, 8279, 8419, 8558, 8697, 8836, 8975, 9113, 9251, 9389, 9526, 9663, 9800, 9935, 10071, 10205, 10339, 10472, 10605, 10736, 10867, 10997, 11126, 11254, 11381, 11507, 11632, 11756, 11878, 12000, 12120, 12239, 12357, 12474, 12589, 12702, 12815, 12925, 13035, 13142, 13248, 13353, 13456, 13557, 13657, 13755, 13851, 13945, 14038, 14128, 14217, 14304, 14389, 14472, 14553, 14632, 14709, 14784, 14857, 14928, 14997, 15064, 15128, 15190, 15250, 15308, 15364, 15417, 15469, 15518, 15564, 15608, 15650, 15690, 15727, 15762, 15795, 15825, 15853, 15878, 15902, 15922, 15940, 15956, 15970, 15981, 15989, 15995, 15999, 16000, 15999, 15995, 15989, 15981, 15970, 15956, 15940, 15922, 15902, 15878, 15853, 15825, 15795, 15762, 15727, 15690, 15650, 15608, 15564, 15518, 15469, 15417, 15364, 15308, 15250, 15190, 15128, 15064, 14997, 14928, 14857, 14784, 14709, 14632, 14553, 14472, 14389, 14304, 14217, 14128, 14038, 13945, 13851, 13755, 13657, 13557, 13456, 13353, 13248, 13142, 13035, 12925, 12815, 12702, 12589, 12474, 12357, 12239, 12120, 12000, 11878, 11756, 11632, 11507, 11381, 11254, 11126, 10997, 10867, 10736, 10605, 10472, 10339, 10205, 10071, 9935, 9800, 9663, 9526, 9389, 9251, 9113, 8975, 8836, 8697, 8558, 8419, 8279, 8140, 8000, 7860, 7721, 7581, 7442, 7303, 7164, 7025, 6887, 6749, 6611, 6474, 6337, 6200, 6065, 5929, 5795, 5661, 5528, 5395, 5264, 5133, 5003, 4874, 4746, 4619, 4493, 4368, 4244, 4122, 4000, 3880, 3761, 3643, 3526, 3411, 3298, 3185, 3075, 2965, 2858, 2752, 2647, 2544, 2443, 2343, 2245, 2149, 2055, 1962, 1872, 1783, 1696, 1611, 1528, 1447, 1368, 1291, 1216, 1143, 1072, 1003, 936, 872, 810, 750, 692, 636, 583, 531, 482, 436, 392, 350, 310, 273, 238, 205, 175, 147, 122, 98, 78, 60, 44, 30, 19, 11, 5, 1, 0, 1, 5, 11, 19, 30, 44, 60, 78, 98, 122, 147, 175, 205, 238, 273, 310, 350, 392, 436, 482, 531, 583, 636, 692, 750, 810, 872, 936, 1003, 1072, 1143, 1216, 1291, 1368, 1447, 1528, 1611, 1696, 1783, 1872, 1962, 2055, 2149, 2245, 2343, 2443, 2544, 2647, 2752, 2858, 2965, 3075, 3185, 3298, 3411, 3526, 3643, 3761, 3880, 4000, 4122, 4244, 4368, 4493, 4619, 4746, 4874, 5003, 5133, 5264, 5395, 5528, 5661, 5795, 5929, 6065, 6200, 6337, 6474, 6611, 6749, 6887, 7025, 7164, 7303, 7442, 7581, 7721, 7860
  };
  static uint16_t i = 0;
  i += 1;
  uint32_t intensity = magical_intensity;
  uint32_t into_time = HAL_GetTick() - magical_started_at;
  if (into_time < 200)
    intensity = intensity * into_time / 200;
  TIM14->CCR1 = ((uint32_t)sin_lut[i % N] * intensity) >> 16;
  TIM3->CCR1 = ((uint32_t)sin_lut[(i + N / 3) % N] * intensity) >> 16;
  TIM17->CCR1 = ((uint32_t)sin_lut[(i + N * 2 / 3) % N] * intensity) >> 16;
}

void RTC_TAMP_IRQHandler()
{
  if (__HAL_RTC_WAKEUPTIMER_GET_FLAG(&rtc, RTC_FLAG_WUTF))
    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&rtc, RTC_FLAG_WUTF);
  else return;

  HAL_GPIO_Init(GPIOB, &(GPIO_InitTypeDef){
    .Pin = PIN_LED_R,
    .Mode = GPIO_MODE_OUTPUT_PP,
  });
  for (int i = 0; ; i++) {
    GPIOB->BSRR = PIN_LED_R <<  0; spin_delay(16000 * 40);
    GPIOB->BSRR = PIN_LED_R << 16; spin_delay(16000 * 40);
    if (i == 3) HAL_GPIO_WritePin(GPIOA, PIN_PWR_LATCH, 0);
  }
}

void NMI_Handler() { while (1) { } }
void HardFault_Handler() { while (1) { } }
void SVC_Handler() { while (1) { } }
void PendSV_Handler() { while (1) { } }
void WWDG_IRQHandler() { while (1) { } }
void FLASH_IRQHandler() { while (1) { } }
void RCC_IRQHandler() { while (1) { } }
void EXTI0_1_IRQHandler() { while (1) { } }
void EXTI4_15_IRQHandler() { while (1) { } }
void DMA1_Channel1_IRQHandler() { while (1) { } }
void DMA1_Channel2_3_IRQHandler() { while (1) { } }
void DMA1_Ch4_5_DMAMUX1_OVR_IRQHandler() { while (1) { } }
void ADC1_IRQHandler() { while (1) { } }
void TIM1_BRK_UP_TRG_COM_IRQHandler() { while (1) { } }
void TIM1_CC_IRQHandler() { while (1) { } }
void TIM14_IRQHandler() { while (1) { } }
void TIM16_IRQHandler() { while (1) { } }
void TIM17_IRQHandler() { while (1) { } }
void I2C1_IRQHandler() { while (1) { } }
void I2C2_IRQHandler() { while (1) { } }
void SPI1_IRQHandler() { while (1) { } }
void SPI2_IRQHandler() { while (1) { } }
void USART1_IRQHandler() { while (1) { } }
void USART2_IRQHandler() { while (1) { } }
