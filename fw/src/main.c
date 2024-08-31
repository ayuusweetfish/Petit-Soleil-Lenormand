#include <stm32g0xx_hal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../misc/bitmap_font/bitmap_font.h"
#include "../misc/rng/twofish.h"

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

#pragma GCC push_options
#pragma GCC optimize("O3")
static inline void spi_transmit(uint8_t *data, size_t size)
{
  HAL_SPI_Transmit(&spi1, data, size, 1000); return;
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

static inline void _epd_cmd(uint8_t cmd, uint8_t *params, size_t params_size)
{
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NCS, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_DCC, GPIO_PIN_RESET);
  spi_transmit(&cmd, 1);
  if (params_size > 0) {
    HAL_GPIO_WritePin(GPIOA, PIN_EP_DCC, GPIO_PIN_SET);
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

// Requires `n` to be even
// Also mixes in TIM3, if it is enabled
static inline void entropy_adc(uint32_t *out_v, int n)
{
/*
  HAL_GPIO_Init(GPIOA, &(GPIO_InitTypeDef){
    .Pin = GPIO_PIN_12,
    .Mode = GPIO_MODE_ANALOG,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_HIGH,
  });
*/
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
      // v = (v << 8) | (adc_value & 0xff);
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

#pragma GCC optimize("O3")
static inline void entropy_clocks(uint32_t *_s, int n)
{
  uint16_t *s = (uint16_t *)_s;
  // LSI-HSI ratio
  for (int i = 0; i < n * 2; i++) {
    uint16_t last0 = (i == 0 ? s[n - 1] : s[i - 1]);
    uint16_t last1 = (i == 0 ? s[n - 2] : i == 1 ? s[n - 1] : s[i - 2]);
    int ops = 200 + (((last0 << 4) ^ ((uint32_t)(last0 >> 8) * last1)) & 0x7ff);
    spin_delay(ops);
    s[i] += TIM16->CCR1;
  }
  // for (int i = 0; i < n * 2; i++) swv_printf("%04x%c", s[i], i % 20 == 19 ? '\n' : ' ');
  // while (1) { }
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
}

int main()
{
  // Entropy from randomly initialised memory
  uint64_t mem1 = 0, mem2 = 0, mem3 = 0, mem4 = 0;
  for (uint64_t *p = (uint64_t *)0x20000000; p < (uint64_t *)0x20002000; p++) {
    mem1 = mem1 ^ *p;
    mem2 = (mem2 * 17) ^ ((uint64_t)(uint32_t)p + *p);
    if (__builtin_parity((uint32_t)p)) mem4 += *p;
    else mem3 += mem2;
  }

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

/*
  swv_printf("%08x%08x %08x%08x %08x%08x %08x%08x\n",
    (uint32_t)(mem1 >> 32),
    (uint32_t)(mem1 >>  0),
    (uint32_t)(mem2 >> 32),
    (uint32_t)(mem2 >>  0),
    (uint32_t)(mem3 >> 32),
    (uint32_t)(mem3 >>  0),
    (uint32_t)(mem4 >> 32),
    (uint32_t)(mem4 >>  0));
*/

  // ======= Power latch ======
  gpio_init = (GPIO_InitTypeDef){
    .Pin = PIN_PWR_LATCH,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW,
  };
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, PIN_PWR_LATCH, 1);

  // ======= Lights ======
  gpio_init.Pin = PIN_LED_R | PIN_LED_G | PIN_LED_B;
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &gpio_init);
  // HAL_GPIO_WritePin(GPIOB, PIN_LED_R | PIN_LED_G | PIN_LED_B, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, PIN_LED_R | PIN_LED_G | PIN_LED_B, GPIO_PIN_SET);

  // Activate EPD driver (SSD1681) reset signal
  gpio_init = (GPIO_InitTypeDef){
    .Pin = PIN_EP_NRST,
    .Mode = GPIO_MODE_OUTPUT_PP,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_LOW,
  };
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NRST, 0);

  // Clocks
  setup_clocks();
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

  entropy_clocks_start();

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
    .Trigger = EXTI_TRIGGER_FALLING,
    .GPIOSel = EXTI_GPIOA,
  };
  HAL_EXTI_SetConfigLine(&exti_handle, &exti_cfg);

  // Interrupt
  HAL_NVIC_SetPriority(EXTI2_3_IRQn, 1, 0);
  // Will be enabled later

void sleep_wait_button()
{
if (0) {
  while (HAL_GPIO_ReadPin(GPIOA, PIN_BUTTON) == 1) sleep_delay(10);
} else {
  HAL_SuspendTick();
  HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  HAL_ResumeTick();
  HAL_NVIC_DisableIRQ(EXTI2_3_IRQn);
}
}

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

  ADC_ChannelConfTypeDef adc_ch13;
  adc_ch13.Channel = ADC_CHANNEL_VREFINT;
  adc_ch13.Rank = ADC_REGULAR_RANK_1;
  adc_ch13.SamplingTime = ADC_SAMPLETIME_79CYCLES_5; // Stablize
  HAL_ADC_ConfigChannel(&adc1, &adc_ch13);

  HAL_ADCEx_Calibration_Start(&adc1);

  // Wait some time for the voltage to recover?
  sleep_delay(50);

  HAL_ADC_Start(&adc1);
  HAL_ADC_PollForConversion(&adc1, 1000);
  uint32_t adc_vrefint = HAL_ADC_GetValue(&adc1);
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
  uint32_t adc_vri = HAL_ADC_GetValue(&adc1);
  HAL_ADC_Stop(&adc1);
  // swv_printf("ADC VRI = %lu\n", adc_vri);
  // VREFINT cal = 1656, VREFINT read = 1542, VRI read = 3813
  // -> VRI = 3813/4095 * (1656/1542 * 3 V) = 3.00 V

  uint32_t vri_mV = (uint32_t)(3000ULL * adc_vri * (*VREFINT_CAL_ADDR) / (4095 * adc_vrefint));
  // swv_printf("VRI = %lu mV\n", vri_mV);

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
  uint32_t pool[20] = {
    // RAM initialisation
    (uint32_t)(mem1 >> 32),
    (uint32_t)(mem1 >>  0),
    (uint32_t)(mem2 >> 32),
    (uint32_t)(mem2 >>  0),
    (uint32_t)(mem3 >> 32),
    (uint32_t)(mem3 >>  0),
    (uint32_t)(mem4 >> 32),
    (uint32_t)(mem4 >>  0),
    // Device signature
    *(uint32_t *)(UID_BASE + 0),
    *(uint32_t *)(UID_BASE + 4),
    *(uint32_t *)(UID_BASE + 8),
    LL_RCC_HSI_GetCalibration(),
    *TEMPSENSOR_CAL1_ADDR,
    *TEMPSENSOR_CAL2_ADDR,
    *VREFINT_CAL_ADDR,
    0 | ((uint32_t)flash_uid[0] << 24) | ((uint32_t)flash_uid[1] << 16)
      | ((uint32_t)flash_uid[2] <<  8) | ((uint32_t)flash_uid[3] <<  0),
    // Voltages
    adc_vrefint,
    adc_vri,
  };
while (1) {
  uint32_t t0 = HAL_GetTick();
  entropy_adc(pool, 20);
  uint32_t t1 = HAL_GetTick();
  entropy_clocks(pool, 20);
  uint32_t t2 = HAL_GetTick();
  swv_printf("%u %u\n", t1 - t0, t2 - t1);
}
  entropy_clocks_stop();

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

  uint32_t tick = HAL_GetTick();
  for (int i = 0; i < 2; i++) {
    entropy_adc(pool, 20);
    sleep_delay(200);
  }
  sleep_delay(tick + 2000 - HAL_GetTick());

  HAL_NVIC_DisableIRQ(TIM3_IRQn);
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

  epd_reset(false, vri_mV < 2400);
  // Set RAM X-address Start / End position
  epd_cmd(0x44, 0x00, 0x18);  // 0x18 = 200 / 8 - 1
  epd_cmd(0x45, 0xC7, 0x00, 0x00, 0x00);  // 0xC7 = 200 - 1
  // Set starting RAM location
  epd_cmd(0x4E, 0x00);
  epd_cmd(0x4F, 0xC7, 0x00);

  // Read image
  flash_read(FILE_ADDR___cards_bin + card_id * 13001, pixels, 200 * 200 / 8);
  // Write pixel data
  _epd_cmd(0x24, pixels, sizeof pixels);
  // Display
  epd_cmd(0x22, 0xC7);  // DISPLAY with DISPLAY Mode 1
  epd_cmd(0x20);
  epd_waitbusy();

  // Greyscale
  flash_read_and(FILE_ADDR___cards_bin + card_id * 13001 + 5000, pixels, 200 * 200 / 8);
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
  // Write RAM
  _epd_cmd(0x24, pixels, sizeof pixels);
  // Display
  epd_cmd(0x22, 0xCF);  // DISPLAY with DISPLAY Mode 2
  epd_cmd(0x20);
  epd_waitbusy();

  // Deep sleep
  // NOTE: Deep sleep mode 2 (0x10, 0x03) results in unstable display?
  epd_cmd(0x10, 0x01);
  // RAM does not need to be retained if the entire image is re-sent

  // Blink green
  for (int i = 0; i < 1; i++) {
    TIM17->CCR1 = 500; sleep_delay(100);
    TIM17->CCR1 = 0; sleep_delay(100);
  }

  sleep_wait_button();

  epd_reset(true, true);
  // Set RAM X-address Start / End position
  epd_cmd(0x44, 0x00, 0x18);  // 0x18 = 200 / 8 - 1
  epd_cmd(0x45, 0xC7, 0x00, 0x00, 0x00);  // 0xC7 = 200 - 1
  // Set starting RAM location
  epd_cmd(0x4E, 0x00);
  epd_cmd(0x4F, 0xC7, 0x00);

  uint8_t side;
  flash_read(FILE_ADDR___cards_bin + card_id * 13001 + 10000, &side, 1);
  int offs = (side == 0 ? 0 : 200 / 8 * 160);

  flash_read(FILE_ADDR___cards_bin + card_id * 13001, pixels, 200 * 200 / 8);
  // Alpha
  flash_read_clear(FILE_ADDR___cards_bin + card_id * 13001 + 11001, pixels + offs, 200 * 40 / 8);
  // Colour
  flash_read_xor(FILE_ADDR___cards_bin + card_id * 13001 + 10001, pixels + offs, 200 * 40 / 8);
  // "Previous image" buffer; hence inverted for the name region
  _epd_cmd(0x26, pixels, sizeof pixels);

  // Xor by alpha mask
  flash_read_xor(FILE_ADDR___cards_bin + card_id * 13001 + 11001, pixels + offs, 200 * 40 / 8);
  // Write pixel data
  _epd_cmd(0x24, pixels, sizeof pixels);
  // Display
  epd_cmd(0x22, 0xCF);  // DISPLAY with DISPLAY Mode 2
  epd_cmd(0x20);
  epd_waitbusy();
  // Deep sleep
  epd_cmd(0x10, 0x01);

  // Clear blue LED lit up in the EXTI interrupt handler
  TIM14->CCR1 = TIM3->CCR1 = TIM17->CCR1 = 0;

  sleep_wait_button();

  // XXX: DRY!
  epd_reset(true, false);
  // Set RAM X-address Start / End position
  epd_cmd(0x44, 0x00, 0x18);  // 0x18 = 200 / 8 - 1
  epd_cmd(0x45, 0xC7, 0x00, 0x00, 0x00);  // 0xC7 = 200 - 1
  // Set starting RAM location
  epd_cmd(0x4E, 0x00);
  epd_cmd(0x4F, 0xC7, 0x00);
  // Revert to non-power-save mode
  // XXX: Why is this required??
  // Display Update Control 2
  epd_cmd(0x22, 0xB9);  // Load LUT with DISPLAY Mode 2
  // Master Activation
  epd_cmd(0x20);
  epd_waitbusy();
  // VSH1 = 9V, VSH2 = 3V, VSL = -12V
  epd_cmd(0x04, 0x23, 0x94, 0x26);

  // Clear
  for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] = 0x00;
  _epd_cmd(0x26, pixels, sizeof pixels);
  for (int i = 0; i < 200 * 200 / 8; i++) pixels[i] = 0xff;
  // Alpha
  flash_read_set(FILE_ADDR___cards_bin + card_id * 13001 + 11001, pixels + offs, 200 * 40 / 8);
  // Colour
  flash_read_xor(FILE_ADDR___cards_bin + card_id * 13001 + 10001, pixels + offs, 200 * 40 / 8);
  _epd_cmd(0x24, pixels, sizeof pixels);
  // Display
  epd_cmd(0x22, 0xCF);  // DISPLAY with DISPLAY Mode 2
  epd_cmd(0x20);
  epd_waitbusy();

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

  _epd_cmd(0x24, pixels, sizeof pixels);
  // Display
  epd_cmd(0x22, 0xCF);  // DISPLAY with DISPLAY Mode 2
  epd_cmd(0x20);
  epd_waitbusy();
  // Deep sleep
  epd_cmd(0x10, 0x03);

  TIM14->CCR1 = TIM3->CCR1 = TIM17->CCR1 = 0;

  HAL_GPIO_WritePin(GPIOA, PIN_PWR_LATCH, 0);
  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  while (1) { }
}

void SysTick_Handler()
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
}

void EXTI2_3_IRQHandler()
{
  setup_clocks();
  TIM14->CCR1 = 2000; // Display blue
  __HAL_GPIO_EXTI_CLEAR_FALLING_IT(PIN_BUTTON);
}

void TIM3_IRQHandler()
{
  // Clear interrupt flag
  TIM3->SR &= ~TIM_FLAG_UPDATE;

/*
from math import *
N=1800
print(', '.join('%d' % round(8000*(1+sin(i/N*2*pi))) for i in range(N)))
*/
#define N 1800
  static const uint16_t sin_lut[N] = {
8000, 8028, 8056, 8084, 8112, 8140, 8168, 8195, 8223, 8251, 8279, 8307, 8335, 8363, 8391, 8419, 8447, 8474, 8502, 8530, 8558, 8586, 8614, 8642, 8669, 8697, 8725, 8753, 8781, 8808, 8836, 8864, 8892, 8919, 8947, 8975, 9003, 9030, 9058, 9086, 9113, 9141, 9169, 9196, 9224, 9251, 9279, 9307, 9334, 9362, 9389, 9417, 9444, 9472, 9499, 9526, 9554, 9581, 9609, 9636, 9663, 9691, 9718, 9745, 9772, 9800, 9827, 9854, 9881, 9908, 9935, 9962, 9990, 10017, 10044, 10071, 10098, 10124, 10151, 10178, 10205, 10232, 10259, 10286, 10312, 10339, 10366, 10392, 10419, 10446, 10472, 10499, 10525, 10552, 10578, 10605, 10631, 10657, 10684, 10710, 10736, 10762, 10789, 10815, 10841, 10867, 10893, 10919, 10945, 10971, 10997, 11023, 11049, 11074, 11100, 11126, 11152, 11177, 11203, 11228, 11254, 11279, 11305, 11330, 11356, 11381, 11406, 11431, 11457, 11482, 11507, 11532, 11557, 11582, 11607, 11632, 11657, 11682, 11706, 11731, 11756, 11780, 11805, 11830, 11854, 11878, 11903, 11927, 11952, 11976, 12000, 12024, 12048, 12072, 12096, 12120, 12144, 12168, 12192, 12216, 12239, 12263, 12287, 12310, 12334, 12357, 12381, 12404, 12427, 12450, 12474, 12497, 12520, 12543, 12566, 12589, 12611, 12634, 12657, 12680, 12702, 12725, 12747, 12770, 12792, 12815, 12837, 12859, 12881, 12903, 12925, 12947, 12969, 12991, 13013, 13035, 13056, 13078, 13099, 13121, 13142, 13164, 13185, 13206, 13227, 13248, 13270, 13290, 13311, 13332, 13353, 13374, 13394, 13415, 13436, 13456, 13476, 13497, 13517, 13537, 13557, 13577, 13597, 13617, 13637, 13657, 13677, 13696, 13716, 13735, 13755, 13774, 13793, 13813, 13832, 13851, 13870, 13889, 13908, 13926, 13945, 13964, 13982, 14001, 14019, 14038, 14056, 14074, 14092, 14110, 14128, 14146, 14164, 14182, 14200, 14217, 14235, 14252, 14270, 14287, 14304, 14321, 14338, 14355, 14372, 14389, 14406, 14423, 14439, 14456, 14472, 14489, 14505, 14521, 14537, 14553, 14569, 14585, 14601, 14617, 14632, 14648, 14663, 14679, 14694, 14709, 14725, 14740, 14755, 14770, 14784, 14799, 14814, 14828, 14843, 14857, 14872, 14886, 14900, 14914, 14928, 14942, 14956, 14970, 14983, 14997, 15010, 15024, 15037, 15050, 15064, 15077, 15090, 15103, 15115, 15128, 15141, 15153, 15166, 15178, 15190, 15203, 15215, 15227, 15239, 15250, 15262, 15274, 15285, 15297, 15308, 15320, 15331, 15342, 15353, 15364, 15375, 15386, 15396, 15407, 15417, 15428, 15438, 15448, 15459, 15469, 15479, 15488, 15498, 15508, 15518, 15527, 15536, 15546, 15555, 15564, 15573, 15582, 15591, 15600, 15608, 15617, 15626, 15634, 15642, 15650, 15659, 15667, 15675, 15682, 15690, 15698, 15705, 15713, 15720, 15727, 15735, 15742, 15749, 15756, 15762, 15769, 15776, 15782, 15789, 15795, 15801, 15807, 15813, 15819, 15825, 15831, 15837, 15842, 15848, 15853, 15858, 15863, 15869, 15874, 15878, 15883, 15888, 15893, 15897, 15902, 15906, 15910, 15914, 15918, 15922, 15926, 15930, 15933, 15937, 15940, 15944, 15947, 15950, 15953, 15956, 15959, 15962, 15964, 15967, 15970, 15972, 15974, 15976, 15979, 15981, 15982, 15984, 15986, 15988, 15989, 15990, 15992, 15993, 15994, 15995, 15996, 15997, 15998, 15998, 15999, 15999, 16000, 16000, 16000, 16000, 16000, 16000, 16000, 15999, 15999, 15998, 15998, 15997, 15996, 15995, 15994, 15993, 15992, 15990, 15989, 15988, 15986, 15984, 15982, 15981, 15979, 15976, 15974, 15972, 15970, 15967, 15964, 15962, 15959, 15956, 15953, 15950, 15947, 15944, 15940, 15937, 15933, 15930, 15926, 15922, 15918, 15914, 15910, 15906, 15902, 15897, 15893, 15888, 15883, 15878, 15874, 15869, 15863, 15858, 15853, 15848, 15842, 15837, 15831, 15825, 15819, 15813, 15807, 15801, 15795, 15789, 15782, 15776, 15769, 15762, 15756, 15749, 15742, 15735, 15727, 15720, 15713, 15705, 15698, 15690, 15682, 15675, 15667, 15659, 15650, 15642, 15634, 15626, 15617, 15608, 15600, 15591, 15582, 15573, 15564, 15555, 15546, 15536, 15527, 15518, 15508, 15498, 15488, 15479, 15469, 15459, 15448, 15438, 15428, 15417, 15407, 15396, 15386, 15375, 15364, 15353, 15342, 15331, 15320, 15308, 15297, 15285, 15274, 15262, 15250, 15239, 15227, 15215, 15203, 15190, 15178, 15166, 15153, 15141, 15128, 15115, 15103, 15090, 15077, 15064, 15050, 15037, 15024, 15010, 14997, 14983, 14970, 14956, 14942, 14928, 14914, 14900, 14886, 14872, 14857, 14843, 14828, 14814, 14799, 14784, 14770, 14755, 14740, 14725, 14709, 14694, 14679, 14663, 14648, 14632, 14617, 14601, 14585, 14569, 14553, 14537, 14521, 14505, 14489, 14472, 14456, 14439, 14423, 14406, 14389, 14372, 14355, 14338, 14321, 14304, 14287, 14270, 14252, 14235, 14217, 14200, 14182, 14164, 14146, 14128, 14110, 14092, 14074, 14056, 14038, 14019, 14001, 13982, 13964, 13945, 13926, 13908, 13889, 13870, 13851, 13832, 13813, 13793, 13774, 13755, 13735, 13716, 13696, 13677, 13657, 13637, 13617, 13597, 13577, 13557, 13537, 13517, 13497, 13476, 13456, 13436, 13415, 13394, 13374, 13353, 13332, 13311, 13290, 13270, 13248, 13227, 13206, 13185, 13164, 13142, 13121, 13099, 13078, 13056, 13035, 13013, 12991, 12969, 12947, 12925, 12903, 12881, 12859, 12837, 12815, 12792, 12770, 12747, 12725, 12702, 12680, 12657, 12634, 12611, 12589, 12566, 12543, 12520, 12497, 12474, 12450, 12427, 12404, 12381, 12357, 12334, 12310, 12287, 12263, 12239, 12216, 12192, 12168, 12144, 12120, 12096, 12072, 12048, 12024, 12000, 11976, 11952, 11927, 11903, 11878, 11854, 11830, 11805, 11780, 11756, 11731, 11706, 11682, 11657, 11632, 11607, 11582, 11557, 11532, 11507, 11482, 11457, 11431, 11406, 11381, 11356, 11330, 11305, 11279, 11254, 11228, 11203, 11177, 11152, 11126, 11100, 11074, 11049, 11023, 10997, 10971, 10945, 10919, 10893, 10867, 10841, 10815, 10789, 10762, 10736, 10710, 10684, 10657, 10631, 10605, 10578, 10552, 10525, 10499, 10472, 10446, 10419, 10392, 10366, 10339, 10312, 10286, 10259, 10232, 10205, 10178, 10151, 10124, 10098, 10071, 10044, 10017, 9990, 9962, 9935, 9908, 9881, 9854, 9827, 9800, 9772, 9745, 9718, 9691, 9663, 9636, 9609, 9581, 9554, 9526, 9499, 9472, 9444, 9417, 9389, 9362, 9334, 9307, 9279, 9251, 9224, 9196, 9169, 9141, 9113, 9086, 9058, 9030, 9003, 8975, 8947, 8919, 8892, 8864, 8836, 8808, 8781, 8753, 8725, 8697, 8669, 8642, 8614, 8586, 8558, 8530, 8502, 8474, 8447, 8419, 8391, 8363, 8335, 8307, 8279, 8251, 8223, 8195, 8168, 8140, 8112, 8084, 8056, 8028, 8000, 7972, 7944, 7916, 7888, 7860, 7832, 7805, 7777, 7749, 7721, 7693, 7665, 7637, 7609, 7581, 7553, 7526, 7498, 7470, 7442, 7414, 7386, 7358, 7331, 7303, 7275, 7247, 7219, 7192, 7164, 7136, 7108, 7081, 7053, 7025, 6997, 6970, 6942, 6914, 6887, 6859, 6831, 6804, 6776, 6749, 6721, 6693, 6666, 6638, 6611, 6583, 6556, 6528, 6501, 6474, 6446, 6419, 6391, 6364, 6337, 6309, 6282, 6255, 6228, 6200, 6173, 6146, 6119, 6092, 6065, 6038, 6010, 5983, 5956, 5929, 5902, 5876, 5849, 5822, 5795, 5768, 5741, 5714, 5688, 5661, 5634, 5608, 5581, 5554, 5528, 5501, 5475, 5448, 5422, 5395, 5369, 5343, 5316, 5290, 5264, 5238, 5211, 5185, 5159, 5133, 5107, 5081, 5055, 5029, 5003, 4977, 4951, 4926, 4900, 4874, 4848, 4823, 4797, 4772, 4746, 4721, 4695, 4670, 4644, 4619, 4594, 4569, 4543, 4518, 4493, 4468, 4443, 4418, 4393, 4368, 4343, 4318, 4294, 4269, 4244, 4220, 4195, 4170, 4146, 4122, 4097, 4073, 4048, 4024, 4000, 3976, 3952, 3928, 3904, 3880, 3856, 3832, 3808, 3784, 3761, 3737, 3713, 3690, 3666, 3643, 3619, 3596, 3573, 3550, 3526, 3503, 3480, 3457, 3434, 3411, 3389, 3366, 3343, 3320, 3298, 3275, 3253, 3230, 3208, 3185, 3163, 3141, 3119, 3097, 3075, 3053, 3031, 3009, 2987, 2965, 2944, 2922, 2901, 2879, 2858, 2836, 2815, 2794, 2773, 2752, 2730, 2710, 2689, 2668, 2647, 2626, 2606, 2585, 2564, 2544, 2524, 2503, 2483, 2463, 2443, 2423, 2403, 2383, 2363, 2343, 2323, 2304, 2284, 2265, 2245, 2226, 2207, 2187, 2168, 2149, 2130, 2111, 2092, 2074, 2055, 2036, 2018, 1999, 1981, 1962, 1944, 1926, 1908, 1890, 1872, 1854, 1836, 1818, 1800, 1783, 1765, 1748, 1730, 1713, 1696, 1679, 1662, 1645, 1628, 1611, 1594, 1577, 1561, 1544, 1528, 1511, 1495, 1479, 1463, 1447, 1431, 1415, 1399, 1383, 1368, 1352, 1337, 1321, 1306, 1291, 1275, 1260, 1245, 1230, 1216, 1201, 1186, 1172, 1157, 1143, 1128, 1114, 1100, 1086, 1072, 1058, 1044, 1030, 1017, 1003, 990, 976, 963, 950, 936, 923, 910, 897, 885, 872, 859, 847, 834, 822, 810, 797, 785, 773, 761, 750, 738, 726, 715, 703, 692, 680, 669, 658, 647, 636, 625, 614, 604, 593, 583, 572, 562, 552, 541, 531, 521, 512, 502, 492, 482, 473, 464, 454, 445, 436, 427, 418, 409, 400, 392, 383, 374, 366, 358, 350, 341, 333, 325, 318, 310, 302, 295, 287, 280, 273, 265, 258, 251, 244, 238, 231, 224, 218, 211, 205, 199, 193, 187, 181, 175, 169, 163, 158, 152, 147, 142, 137, 131, 126, 122, 117, 112, 107, 103, 98, 94, 90, 86, 82, 78, 74, 70, 67, 63, 60, 56, 53, 50, 47, 44, 41, 38, 36, 33, 30, 28, 26, 24, 21, 19, 18, 16, 14, 12, 11, 10, 8, 7, 6, 5, 4, 3, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 14, 16, 18, 19, 21, 24, 26, 28, 30, 33, 36, 38, 41, 44, 47, 50, 53, 56, 60, 63, 67, 70, 74, 78, 82, 86, 90, 94, 98, 103, 107, 112, 117, 122, 126, 131, 137, 142, 147, 152, 158, 163, 169, 175, 181, 187, 193, 199, 205, 211, 218, 224, 231, 238, 244, 251, 258, 265, 273, 280, 287, 295, 302, 310, 318, 325, 333, 341, 350, 358, 366, 374, 383, 392, 400, 409, 418, 427, 436, 445, 454, 464, 473, 482, 492, 502, 512, 521, 531, 541, 552, 562, 572, 583, 593, 604, 614, 625, 636, 647, 658, 669, 680, 692, 703, 715, 726, 738, 750, 761, 773, 785, 797, 810, 822, 834, 847, 859, 872, 885, 897, 910, 923, 936, 950, 963, 976, 990, 1003, 1017, 1030, 1044, 1058, 1072, 1086, 1100, 1114, 1128, 1143, 1157, 1172, 1186, 1201, 1216, 1230, 1245, 1260, 1275, 1291, 1306, 1321, 1337, 1352, 1368, 1383, 1399, 1415, 1431, 1447, 1463, 1479, 1495, 1511, 1528, 1544, 1561, 1577, 1594, 1611, 1628, 1645, 1662, 1679, 1696, 1713, 1730, 1748, 1765, 1783, 1800, 1818, 1836, 1854, 1872, 1890, 1908, 1926, 1944, 1962, 1981, 1999, 2018, 2036, 2055, 2074, 2092, 2111, 2130, 2149, 2168, 2187, 2207, 2226, 2245, 2265, 2284, 2304, 2323, 2343, 2363, 2383, 2403, 2423, 2443, 2463, 2483, 2503, 2524, 2544, 2564, 2585, 2606, 2626, 2647, 2668, 2689, 2710, 2730, 2752, 2773, 2794, 2815, 2836, 2858, 2879, 2901, 2922, 2944, 2965, 2987, 3009, 3031, 3053, 3075, 3097, 3119, 3141, 3163, 3185, 3208, 3230, 3253, 3275, 3298, 3320, 3343, 3366, 3389, 3411, 3434, 3457, 3480, 3503, 3526, 3550, 3573, 3596, 3619, 3643, 3666, 3690, 3713, 3737, 3761, 3784, 3808, 3832, 3856, 3880, 3904, 3928, 3952, 3976, 4000, 4024, 4048, 4073, 4097, 4122, 4146, 4170, 4195, 4220, 4244, 4269, 4294, 4318, 4343, 4368, 4393, 4418, 4443, 4468, 4493, 4518, 4543, 4569, 4594, 4619, 4644, 4670, 4695, 4721, 4746, 4772, 4797, 4823, 4848, 4874, 4900, 4926, 4951, 4977, 5003, 5029, 5055, 5081, 5107, 5133, 5159, 5185, 5211, 5238, 5264, 5290, 5316, 5343, 5369, 5395, 5422, 5448, 5475, 5501, 5528, 5554, 5581, 5608, 5634, 5661, 5688, 5714, 5741, 5768, 5795, 5822, 5849, 5876, 5902, 5929, 5956, 5983, 6010, 6038, 6065, 6092, 6119, 6146, 6173, 6200, 6228, 6255, 6282, 6309, 6337, 6364, 6391, 6419, 6446, 6474, 6501, 6528, 6556, 6583, 6611, 6638, 6666, 6693, 6721, 6749, 6776, 6804, 6831, 6859, 6887, 6914, 6942, 6970, 6997, 7025, 7053, 7081, 7108, 7136, 7164, 7192, 7219, 7247, 7275, 7303, 7331, 7358, 7386, 7414, 7442, 7470, 7498, 7526, 7553, 7581, 7609, 7637, 7665, 7693, 7721, 7749, 7777, 7805, 7832, 7860, 7888, 7916, 7944, 7972
  };
  static uint16_t i = 0;
  const int SCALE = 8;
  i += 5;
  TIM14->CCR1 = sin_lut[i % N] / SCALE;
  TIM3->CCR1 = sin_lut[(i + N / 3) % N] / SCALE;
  TIM17->CCR1 = sin_lut[(i + N * 2 / 3) % N] / SCALE;
}

void NMI_Handler() { while (1) { } }
void HardFault_Handler() { while (1) { } }
void SVC_Handler() { while (1) { } }
void PendSV_Handler() { while (1) { } }
void WWDG_IRQHandler() { while (1) { } }
void RTC_TAMP_IRQHandler() { while (1) { } }
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
