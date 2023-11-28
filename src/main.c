#include <stm32g0xx_hal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define PIN_LED_ACT   GPIO_PIN_1
#define PIN_PWR_LATCH GPIO_PIN_3
#define PIN_EP_NCS    GPIO_PIN_4
#define PIN_EP_DCC    GPIO_PIN_6
#define PIN_EP_NRST   GPIO_PIN_11
#define PIN_EP_BUSY   GPIO_PIN_12

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
static inline void epd_waitbusy()
{
  while (HAL_GPIO_ReadPin(GPIOA, PIN_EP_BUSY) == GPIO_PIN_SET) { }
}

// ffmpeg -i ~/Downloads/089-Sunset-2.png -f rawvideo -pix_fmt gray8 - 2>/dev/null | ./rle
static const uint8_t image[] = {
  0,255,0,255,0,255,0,139,1,144,2,53,1,144,2,53,1,144,2,52,2,144,2,52,2,144,2,52,3,143,2,52,3,142,3,52,3,142,3,52,3,142,3,52,3,142,3,52,3,142,3,52,3,142,3,52,3,142,3,52,3,142,3,52,3,142,3,52,3,142,3,47,2,4,1,143,3,47,4,2,1,143,3,49,4,144,3,20,3,27,4,143,3,18,3,30,4,142,3,16,4,32,4,141,3,15,3,35,4,140,3,14,3,37,4,139,3,13,3,39,3,139,3,13,3,39,4,138,3,12,3,41,3,138,3,12,2,43,3,137,3,12,2,20,2,21,3,137,3,12,2,20,3,21,2,137,3,12,1,22,2,21,2,137,3,36,2,21,1,137,2,25,1,11,3,158,2,25,2,10,3,158,2,6,1,18,2,11,2,158,2,6,1,18,2,11,2,158,2,5,2,18,2,11,2,16,1,141,2,5,2,18,2,11,2,15,3,1,1,138,2,4,3,17,3,11,2,15,3,1,2,137,2,4,2,18,3,29,2,2,2,142,2,18,2,30,3,1,2,141,3,17,3,30,3,2,2,140,2,18,3,15,2,13,3,2,2,139,3,17,3,16,2,13,4,1,3,138,2,18,3,16,3,13,3,2,2,137,3,17,4,16,3,13,3,2,2,137,2,18,3,18,3,12,4,1,3,135,3,17,3,19,3,13,2,3,2,135,3,17,3,19,3,18,2,134,3,17,3,20,3,18,2,6,1,127,3,40,4,17,3,5,2,125,3,42,3,17,3,6,1,125,3,42,3,17,3,6,2,124,3,10,2,30,3,17,3,6,2,123,3,11,2,30,3,17,3,7,2,122,3,11,2,50,3,7,3,121,3,10,2,51,2,9,2,120,3,11,2,51,2,9,2,120,3,11,2,51,2,9,3,119,3,11,2,50,3,10,2,118,4,11,2,50,3,10,2,118,3,12,2,50,2,11,3,117,3,11,3,49,3,11,3,117,3,11,3,49,2,12,3,116,4,11,3,48,3,12,3,116,3,12,3,48,2,13,3,116,3,12,3,63,3,116,3,12,3,63,3,116,3,12,3,13,1,49,3,116,3,12,3,13,3,26,3,18,2,117,3,12,3,14,4,22,4,138,3,12,3,15,6,17,5,139,3,13,2,17,8,10,7,140,3,34,21,142,3,37,16,144,3,41,8,148,4,197,3,197,4,197,4,255,0,255,0,255,0,255,0,42,2,198,2,136,1,24,2,35,2,136,1,24,2,35,2,136,1,24,2,35,2,135,2,24,2,14,1,20,3,134,2,24,2,14,1,20,3,134,2,23,3,14,2,19,3,134,2,23,2,15,2,19,3,134,2,23,2,15,2,19,3,134,3,22,2,15,2,19,3,134,3,22,2,15,2,19,3,134,3,21,3,15,3,18,3,134,3,21,3,15,3,18,3,134,3,21,3,15,3,18,3,134,3,21,3,15,3,18,3,134,3,21,3,16,3,17,3,10,16,24,6,3,26,49,3,21,3,16,3,17,3,7,19,23,36,49,3,21,3,16,3,17,3,134,3,21,3,16,4,16,3,134,3,21,3,17,3,16,3,26,59,49,3,21,3,17,3,16,3,23,62,49,3,20,4,17,4,15,3,25,57,52,3,20,4,18,3,15,3,134,3,20,4,18,4,14,3,134,3,20,4,19,3,14,3,134,3,20,4,19,4,13,3,134,3,20,3,21,4,12,3,134,3,20,3,21,4,12,3,134,3,20,3,22,4,19,1,128,3,20,3,23,4,18,2,127,3,20,3,23,5,18,2,126,3,20,3,24,5,17,2,126,3,20,3,25,4,17,3,125,3,20,3,26,4,17,2,125,3,20,3,27,3,17,3,124,3,20,3,47,3,124,3,20,3,48,2,124,3,20,3,48,3,123,3,20,3,48,3,123,3,20,3,49,2,123,3,20,3,48,3,123,3,20,3,47,4,123,3,20,3,45,5,124,3,20,3,26,10,8,5,125,3,20,3,23,16,3,6,126,3,19,4,21,25,128,3,19,4,18,8,10,9,129,3,19,4,16,7,16,3,132,3,19,4,13,7,154,3,20,3,10,7,26,3,128,3,20,2,9,5,31,4,126,3,31,2,36,4,124,3,70,4,123,3,71,4,122,3,72,4,121,3,74,4,119,3,75,3,119,3,75,4,118,3,76,4,117,3,19,1,57,4,116,3,19,1,58,4,116,2,18,2,59,3,116,2,18,2,59,4,115,2,18,2,60,4,114,2,18,2,61,3,114,1,18,3,61,4,132,2,63,3,132,2,63,4,131,2,64,3,131,2,64,4,129,3,65,3,129,3,65,4,128,3,66,3,128,3,66,4,127,3,67,3,127,3,55,1,11,4,126,3,55,1,12,3,126,3,55,2,11,3,126,3,54,3,11,4,125,3,54,3,12,3,125,3,54,2,3,2,8,4,124,3,54,2,3,2,9,3,124,3,54,2,3,3,8,4,123,3,53,3,4,2,9,3,123,3,53,3,4,2,10,2,122,4,52,3,5,3,10,1,122,4,52,3,5,3,133,3,61,2,134,3,197,3,197,3,69,2,126,3,68,2,127,3,67,3,127,3,66,3,128,3,65,4,128,3,63,5,129,3,54,13,130,3,53,13,131,3,51,4,142,3,50,3,255,0,255,0,255,0,255,0,255,0,255,0,255,0,255,0,237,
};
static const uint8_t image_overlay[] = {
  0,206,9,189,13,187,3,6,5,184,2,11,4,183,2,7,2,4,3,1,2,178,3,5,4,5,7,176,2,5,4,7,8,57,1,116,2,4,4,9,3,1,4,56,2,115,2,4,3,10,3,2,4,54,3,115,2,18,3,2,4,53,3,30,1,84,3,17,3,4,3,52,3,29,3,83,3,18,3,3,3,52,3,29,3,83,3,18,3,4,2,52,3,29,3,84,2,19,3,3,3,51,3,29,3,84,3,18,3,3,2,52,3,5,2,19,6,84,4,11,1,6,3,20,4,3,1,5,2,9,3,9,3,4,5,4,1,3,2,6,7,85,3,10,2,6,3,19,6,2,2,4,3,2,3,3,3,9,3,3,6,3,3,1,4,4,8,86,3,9,2,7,3,17,3,1,2,3,3,3,3,2,3,2,4,9,3,3,3,1,2,3,8,3,4,1,4,87,3,8,2,7,3,17,3,2,2,2,3,3,3,2,3,2,5,8,3,2,3,2,3,3,8,2,3,2,4,87,4,7,2,8,3,15,3,2,4,2,3,1,3,3,3,2,5,8,3,2,3,2,3,3,4,1,3,1,4,2,4,88,5,15,3,15,3,2,4,2,7,3,3,2,5,8,3,2,3,1,5,2,4,1,3,1,3,2,5,90,5,14,3,14,3,2,4,3,6,3,3,1,6,8,3,2,3,1,6,1,3,2,3,1,3,1,6,90,8,12,3,13,9,4,5,3,10,2,3,3,3,2,6,1,3,1,3,2,3,2,10,89,11,9,4,13,9,3,5,4,5,1,4,1,3,3,4,2,5,2,1,2,3,2,3,2,6,1,3,90,12,8,5,11,5,1,2,5,4,4,5,1,4,8,2,4,3,7,1,4,1,4,3,4,1,92,4,2,8,7,4,24,3,5,3,3,2,136,3,5,7,6,5,21,4,159,7,4,5,21,4,162,5,3,3,23,4,164,4,2,4,22,4,165,4,1,6,20,4,166,10,20,3,168,3,1,3,22,3,169,3,1,2,22,3,169,3,1,3,21,3,170,3,1,1,22,3,171,1,255,0,255,0,57,
};
static void decode(uint8_t *buf, const uint8_t *image, size_t size)
{
  int n_bits = 0;
  uint8_t cur_byte = 0;
  for (size_t i = 0; i < size; i++) {
    for (int j = 0; j < image[i]; j++) {
      cur_byte = (cur_byte << 1) | (i & 1);
      if (++n_bits == 8) {
        *(buf++) = cur_byte;
        n_bits = 0;
      }
    }
  }
}

static void epd_reset(bool partial)
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
  epd_cmd(0x3C, partial ? 0x80 : 0x05);
  // Temperature Sensor Control
  epd_cmd(0x18, 0x80);  // Internal sensor
  // Display Update Control 2; Master Activation
  // 0xB1: Load LUT with DISPLAY Mode 1
  epd_cmd(0x22, partial ? 0xB9 : 0xB1);
  epd_cmd(0x20);
}

int main()
{
  HAL_Init();

  // ======== GPIO ========
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef gpio_init;

  gpio_init.Pin = PIN_PWR_LATCH;
  gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, PIN_PWR_LATCH, GPIO_PIN_RESET);

  gpio_init.Pin = PIN_LED_ACT;
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, PIN_LED_ACT, GPIO_PIN_RESET);

  // SWD (PA13, PA14)
  gpio_init.Pin = GPIO_PIN_13 | GPIO_PIN_14;
  gpio_init.Mode = GPIO_MODE_AF_PP; // Pass over control to AF peripheral
  gpio_init.Alternate = GPIO_AF0_SWJ;
  gpio_init.Pull = GPIO_PULLUP;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio_init);

  // Clocks
  RCC_OscInitTypeDef osc_init = { 0 };
  osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc_init.HSIState = RCC_HSI_ON;
  osc_init.PLL.PLLState = RCC_PLL_OFF;
  HAL_RCC_OscConfig(&osc_init);

  RCC_ClkInitTypeDef clk_init = { 0 };
  clk_init.ClockType =
    RCC_CLOCKTYPE_SYSCLK |
    RCC_CLOCKTYPE_HCLK |
    RCC_CLOCKTYPE_PCLK1;
  clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk_init.APB1CLKDivider = RCC_HCLK_DIV1;
  HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_2);

  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

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

  while (1) {
    HAL_ADC_Start(&adc1);
    HAL_ADC_PollForConversion(&adc1, 1000);
    uint32_t adc_value = HAL_ADC_GetValue(&adc1);
    HAL_ADC_Stop(&adc1);
    swv_printf("ADC ref = %lu\n", *VREFINT_CAL_ADDR);
    swv_printf("ADC = %lu\n", adc_value);
    // ref = 1667, read = 1550 -> VDD = 1667/1550 * 3 V = 3.226 V
  }

  // ======== SPI ========
  // GPIO ports
  // SPI1_SCK (PA5), SPI1_MOSI (PA7)
  gpio_init.Pin = GPIO_PIN_5 | GPIO_PIN_7;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Alternate = GPIO_AF0_SPI1;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio_init);
  // Output EP_NCS (PA4), EP_DCC (PA6), EP_NRST (PA11)
  gpio_init.Pin = PIN_EP_NCS | PIN_EP_DCC | PIN_EP_NRST;
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NCS, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_DCC, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NRST, GPIO_PIN_RESET);
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
  spi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  HAL_SPI_Init(&spi1);
  __HAL_SPI_ENABLE(&spi1);

/*
  // ======== Drive display ========
  static uint8_t pixels[200 * 200 / 8];

  epd_reset(false);
  // Set RAM X-address Start / End position
  epd_cmd(0x44, 0x00, 0x18);  // 0x18 = 200 / 8 - 1
  epd_cmd(0x45, 0xC7, 0x00, 0x00, 0x00);  // 0xC7 = 200 - 1
  // Set starting RAM location
  epd_cmd(0x4E, 0x00);
  epd_cmd(0x4F, 0xC7, 0x00);
  // Write pixel data
  decode(pixels, image, sizeof image);
  _epd_cmd(0x24, pixels, sizeof pixels);
  // Display
  epd_cmd(0x22, 0xC7);  // DISPLAY with DISPLAY Mode 1
  epd_cmd(0x20);
  epd_waitbusy();
  // Deep sleep
  epd_cmd(0x10, 0x01);
  // RAM does not need to be retained if the entire image is re-sent
  // Results in unstable display?
  // epd_cmd(0x10, 0x03);

  for (int i = 0; i < 5; i++) {
    HAL_GPIO_WritePin(GPIOA, PIN_LED_ACT, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOA, PIN_LED_ACT, GPIO_PIN_SET);
    HAL_Delay(100);
  }

  epd_reset(true);
#if 0   // Not correct, only displays top part, RAM not retained?
  // Set RAM X-address Start / End position
  epd_cmd(0x44, 0x00, 0x18);  // 0x18 = 200 / 8 - 1
  epd_cmd(0x45, 0x27, 0x00, 0x00, 0x00);  // 0x27 = 40 - 1
  // Set starting RAM location
  epd_cmd(0x4E, 0x00);
  epd_cmd(0x4F, 0x27, 0x00);
  // Write pixel data
  decode(pixels, image, sizeof image);
  decode(pixels + 200 / 8 * 160, image_overlay, sizeof image_overlay);
  _epd_cmd(0x24, pixels + 200 / 8 * 160, (sizeof pixels) - 200 / 8 * 160);
#else
  // Set RAM X-address Start / End position
  epd_cmd(0x44, 0x00, 0x18);  // 0x18 = 200 / 8 - 1
  epd_cmd(0x45, 0xC7, 0x00, 0x00, 0x00);  // 0xC7 = 200 - 1
  // Set starting RAM location
  epd_cmd(0x4E, 0x00);
  epd_cmd(0x4F, 0xC7, 0x00);
  // Write pixel data
  decode(pixels, image, sizeof image);
  decode(pixels + 200 / 8 * 160, image_overlay, sizeof image_overlay);
  _epd_cmd(0x24, pixels, sizeof pixels);
#endif
  // Display
  epd_cmd(0x22, 0xCF);  // DISPLAY with DISPLAY Mode 2
  epd_cmd(0x20);
  epd_waitbusy();
  // Deep sleep
  epd_cmd(0x10, 0x03);
*/

  while (true) {
    swv_printf("blink!\n");
    HAL_GPIO_WritePin(GPIOA, PIN_LED_ACT, GPIO_PIN_RESET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(GPIOA, PIN_LED_ACT, GPIO_PIN_SET);
    HAL_Delay(200);
  }
}

void SysTick_Handler()
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
}
