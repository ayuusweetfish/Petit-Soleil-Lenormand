#include <stm32g0xx_hal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define PIN_LED_R     GPIO_PIN_6
#define PIN_LED_G     GPIO_PIN_7
#define PIN_LED_B     GPIO_PIN_1
#define PIN_EP_NCS    GPIO_PIN_4
#define PIN_EP_DCC    GPIO_PIN_6
#define PIN_EP_NRST   GPIO_PIN_11
#define PIN_EP_BUSY   GPIO_PIN_12
#define PIN_BUTTON    GPIO_PIN_3

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
TIM_HandleTypeDef tim14, tim16, tim17;

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

void sleep_delay(uint32_t ticks)
{
  uint32_t t0 = HAL_GetTick();
  while (HAL_GetTick() - t0 < ticks) {
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
  }
}

int main()
{
  HAL_Init();

  // ======== GPIO ========
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitTypeDef gpio_init;

  gpio_init.Pin = PIN_LED_R | PIN_LED_G | PIN_LED_B;
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &gpio_init);
  HAL_GPIO_WritePin(GPIOB, PIN_LED_R | PIN_LED_G | PIN_LED_B, GPIO_PIN_RESET);

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
  osc_init.PLL.PLLState = RCC_PLL_ON;
  osc_init.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  osc_init.PLL.PLLM = RCC_PLLM_DIV2;
  osc_init.PLL.PLLN = 8;
  osc_init.PLL.PLLP = RCC_PLLP_DIV2;
  osc_init.PLL.PLLR = RCC_PLLR_DIV2;
  HAL_RCC_OscConfig(&osc_init);

  RCC_ClkInitTypeDef clk_init = { 0 };
  clk_init.ClockType =
    RCC_CLOCKTYPE_SYSCLK |
    RCC_CLOCKTYPE_HCLK |
    RCC_CLOCKTYPE_PCLK1;
  clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;  // 64 MHz
  clk_init.AHBCLKDivider = RCC_SYSCLK_DIV16;    // 4 MHz
  clk_init.APB1CLKDivider = RCC_HCLK_DIV1;      // 4 MHz
  HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_2);

  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

  // ======== Button ========
  gpio_init.Pin = PIN_BUTTON;
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &gpio_init);

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

  HAL_ADC_Start(&adc1);
  HAL_ADC_PollForConversion(&adc1, 1000);
  uint32_t adc_value = HAL_ADC_GetValue(&adc1);
  HAL_ADC_Stop(&adc1);
  swv_printf("ADC ref = %lu\n", *VREFINT_CAL_ADDR);
  swv_printf("ADC = %lu\n", adc_value);
  // ref = 1667, read = 1550 -> VDD = 1667/1550 * 3 V = 3.226 V

  // ======== LED Timers ========
  // APB1 = 64 MHz
  // period = 4 kHz = 16000 cycles

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
      .Period = 16000 - 1,
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

  // LED Red, TIM16
  gpio_init.Pin = GPIO_PIN_6;
  gpio_init.Mode = GPIO_MODE_AF_PP;
  gpio_init.Alternate = GPIO_AF2_TIM16;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio_init);
  __HAL_RCC_TIM16_CLK_ENABLE();
  tim16 = (TIM_HandleTypeDef){
    .Instance = TIM16,
    .Init = {
      .Prescaler = 1 - 1,
      .CounterMode = TIM_COUNTERMODE_UP,
      .Period = 16000 - 1,
      .ClockDivision = TIM_CLOCKDIVISION_DIV1,
      .RepetitionCounter = 0,
    },
  };
  HAL_TIM_PWM_Init(&tim16);
  TIM_OC_InitTypeDef tim16_ch1_oc_init = {
    .OCMode = TIM_OCMODE_PWM2,
    .Pulse = 0, // to be filled
    .OCNPolarity = TIM_OCNPOLARITY_LOW,  // Output is TIM16_CH1N
  };
  HAL_TIM_PWM_ConfigChannel(&tim16, &tim16_ch1_oc_init, TIM_CHANNEL_1);
  HAL_TIMEx_PWMN_Start(&tim16, TIM_CHANNEL_1);

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
      .Period = 16000 - 1,
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

/*
from math import *
N=1800
print(', '.join('%d' % round(8000*(1+sin(i/N*2*pi))) for i in range(N)))
*/
#define N 1800
  const uint16_t sin_lut[N] = {
8000, 8028, 8056, 8084, 8112, 8140, 8168, 8195, 8223, 8251, 8279, 8307, 8335, 8363, 8391, 8419, 8447, 8474, 8502, 8530, 8558, 8586, 8614, 8642, 8669, 8697, 8725, 8753, 8781, 8808, 8836, 8864, 8892, 8919, 8947, 8975, 9003, 9030, 9058, 9086, 9113, 9141, 9169, 9196, 9224, 9251, 9279, 9307, 9334, 9362, 9389, 9417, 9444, 9472, 9499, 9526, 9554, 9581, 9609, 9636, 9663, 9691, 9718, 9745, 9772, 9800, 9827, 9854, 9881, 9908, 9935, 9962, 9990, 10017, 10044, 10071, 10098, 10124, 10151, 10178, 10205, 10232, 10259, 10286, 10312, 10339, 10366, 10392, 10419, 10446, 10472, 10499, 10525, 10552, 10578, 10605, 10631, 10657, 10684, 10710, 10736, 10762, 10789, 10815, 10841, 10867, 10893, 10919, 10945, 10971, 10997, 11023, 11049, 11074, 11100, 11126, 11152, 11177, 11203, 11228, 11254, 11279, 11305, 11330, 11356, 11381, 11406, 11431, 11457, 11482, 11507, 11532, 11557, 11582, 11607, 11632, 11657, 11682, 11706, 11731, 11756, 11780, 11805, 11830, 11854, 11878, 11903, 11927, 11952, 11976, 12000, 12024, 12048, 12072, 12096, 12120, 12144, 12168, 12192, 12216, 12239, 12263, 12287, 12310, 12334, 12357, 12381, 12404, 12427, 12450, 12474, 12497, 12520, 12543, 12566, 12589, 12611, 12634, 12657, 12680, 12702, 12725, 12747, 12770, 12792, 12815, 12837, 12859, 12881, 12903, 12925, 12947, 12969, 12991, 13013, 13035, 13056, 13078, 13099, 13121, 13142, 13164, 13185, 13206, 13227, 13248, 13270, 13290, 13311, 13332, 13353, 13374, 13394, 13415, 13436, 13456, 13476, 13497, 13517, 13537, 13557, 13577, 13597, 13617, 13637, 13657, 13677, 13696, 13716, 13735, 13755, 13774, 13793, 13813, 13832, 13851, 13870, 13889, 13908, 13926, 13945, 13964, 13982, 14001, 14019, 14038, 14056, 14074, 14092, 14110, 14128, 14146, 14164, 14182, 14200, 14217, 14235, 14252, 14270, 14287, 14304, 14321, 14338, 14355, 14372, 14389, 14406, 14423, 14439, 14456, 14472, 14489, 14505, 14521, 14537, 14553, 14569, 14585, 14601, 14617, 14632, 14648, 14663, 14679, 14694, 14709, 14725, 14740, 14755, 14770, 14784, 14799, 14814, 14828, 14843, 14857, 14872, 14886, 14900, 14914, 14928, 14942, 14956, 14970, 14983, 14997, 15010, 15024, 15037, 15050, 15064, 15077, 15090, 15103, 15115, 15128, 15141, 15153, 15166, 15178, 15190, 15203, 15215, 15227, 15239, 15250, 15262, 15274, 15285, 15297, 15308, 15320, 15331, 15342, 15353, 15364, 15375, 15386, 15396, 15407, 15417, 15428, 15438, 15448, 15459, 15469, 15479, 15488, 15498, 15508, 15518, 15527, 15536, 15546, 15555, 15564, 15573, 15582, 15591, 15600, 15608, 15617, 15626, 15634, 15642, 15650, 15659, 15667, 15675, 15682, 15690, 15698, 15705, 15713, 15720, 15727, 15735, 15742, 15749, 15756, 15762, 15769, 15776, 15782, 15789, 15795, 15801, 15807, 15813, 15819, 15825, 15831, 15837, 15842, 15848, 15853, 15858, 15863, 15869, 15874, 15878, 15883, 15888, 15893, 15897, 15902, 15906, 15910, 15914, 15918, 15922, 15926, 15930, 15933, 15937, 15940, 15944, 15947, 15950, 15953, 15956, 15959, 15962, 15964, 15967, 15970, 15972, 15974, 15976, 15979, 15981, 15982, 15984, 15986, 15988, 15989, 15990, 15992, 15993, 15994, 15995, 15996, 15997, 15998, 15998, 15999, 15999, 16000, 16000, 16000, 16000, 16000, 16000, 16000, 15999, 15999, 15998, 15998, 15997, 15996, 15995, 15994, 15993, 15992, 15990, 15989, 15988, 15986, 15984, 15982, 15981, 15979, 15976, 15974, 15972, 15970, 15967, 15964, 15962, 15959, 15956, 15953, 15950, 15947, 15944, 15940, 15937, 15933, 15930, 15926, 15922, 15918, 15914, 15910, 15906, 15902, 15897, 15893, 15888, 15883, 15878, 15874, 15869, 15863, 15858, 15853, 15848, 15842, 15837, 15831, 15825, 15819, 15813, 15807, 15801, 15795, 15789, 15782, 15776, 15769, 15762, 15756, 15749, 15742, 15735, 15727, 15720, 15713, 15705, 15698, 15690, 15682, 15675, 15667, 15659, 15650, 15642, 15634, 15626, 15617, 15608, 15600, 15591, 15582, 15573, 15564, 15555, 15546, 15536, 15527, 15518, 15508, 15498, 15488, 15479, 15469, 15459, 15448, 15438, 15428, 15417, 15407, 15396, 15386, 15375, 15364, 15353, 15342, 15331, 15320, 15308, 15297, 15285, 15274, 15262, 15250, 15239, 15227, 15215, 15203, 15190, 15178, 15166, 15153, 15141, 15128, 15115, 15103, 15090, 15077, 15064, 15050, 15037, 15024, 15010, 14997, 14983, 14970, 14956, 14942, 14928, 14914, 14900, 14886, 14872, 14857, 14843, 14828, 14814, 14799, 14784, 14770, 14755, 14740, 14725, 14709, 14694, 14679, 14663, 14648, 14632, 14617, 14601, 14585, 14569, 14553, 14537, 14521, 14505, 14489, 14472, 14456, 14439, 14423, 14406, 14389, 14372, 14355, 14338, 14321, 14304, 14287, 14270, 14252, 14235, 14217, 14200, 14182, 14164, 14146, 14128, 14110, 14092, 14074, 14056, 14038, 14019, 14001, 13982, 13964, 13945, 13926, 13908, 13889, 13870, 13851, 13832, 13813, 13793, 13774, 13755, 13735, 13716, 13696, 13677, 13657, 13637, 13617, 13597, 13577, 13557, 13537, 13517, 13497, 13476, 13456, 13436, 13415, 13394, 13374, 13353, 13332, 13311, 13290, 13270, 13248, 13227, 13206, 13185, 13164, 13142, 13121, 13099, 13078, 13056, 13035, 13013, 12991, 12969, 12947, 12925, 12903, 12881, 12859, 12837, 12815, 12792, 12770, 12747, 12725, 12702, 12680, 12657, 12634, 12611, 12589, 12566, 12543, 12520, 12497, 12474, 12450, 12427, 12404, 12381, 12357, 12334, 12310, 12287, 12263, 12239, 12216, 12192, 12168, 12144, 12120, 12096, 12072, 12048, 12024, 12000, 11976, 11952, 11927, 11903, 11878, 11854, 11830, 11805, 11780, 11756, 11731, 11706, 11682, 11657, 11632, 11607, 11582, 11557, 11532, 11507, 11482, 11457, 11431, 11406, 11381, 11356, 11330, 11305, 11279, 11254, 11228, 11203, 11177, 11152, 11126, 11100, 11074, 11049, 11023, 10997, 10971, 10945, 10919, 10893, 10867, 10841, 10815, 10789, 10762, 10736, 10710, 10684, 10657, 10631, 10605, 10578, 10552, 10525, 10499, 10472, 10446, 10419, 10392, 10366, 10339, 10312, 10286, 10259, 10232, 10205, 10178, 10151, 10124, 10098, 10071, 10044, 10017, 9990, 9962, 9935, 9908, 9881, 9854, 9827, 9800, 9772, 9745, 9718, 9691, 9663, 9636, 9609, 9581, 9554, 9526, 9499, 9472, 9444, 9417, 9389, 9362, 9334, 9307, 9279, 9251, 9224, 9196, 9169, 9141, 9113, 9086, 9058, 9030, 9003, 8975, 8947, 8919, 8892, 8864, 8836, 8808, 8781, 8753, 8725, 8697, 8669, 8642, 8614, 8586, 8558, 8530, 8502, 8474, 8447, 8419, 8391, 8363, 8335, 8307, 8279, 8251, 8223, 8195, 8168, 8140, 8112, 8084, 8056, 8028, 8000, 7972, 7944, 7916, 7888, 7860, 7832, 7805, 7777, 7749, 7721, 7693, 7665, 7637, 7609, 7581, 7553, 7526, 7498, 7470, 7442, 7414, 7386, 7358, 7331, 7303, 7275, 7247, 7219, 7192, 7164, 7136, 7108, 7081, 7053, 7025, 6997, 6970, 6942, 6914, 6887, 6859, 6831, 6804, 6776, 6749, 6721, 6693, 6666, 6638, 6611, 6583, 6556, 6528, 6501, 6474, 6446, 6419, 6391, 6364, 6337, 6309, 6282, 6255, 6228, 6200, 6173, 6146, 6119, 6092, 6065, 6038, 6010, 5983, 5956, 5929, 5902, 5876, 5849, 5822, 5795, 5768, 5741, 5714, 5688, 5661, 5634, 5608, 5581, 5554, 5528, 5501, 5475, 5448, 5422, 5395, 5369, 5343, 5316, 5290, 5264, 5238, 5211, 5185, 5159, 5133, 5107, 5081, 5055, 5029, 5003, 4977, 4951, 4926, 4900, 4874, 4848, 4823, 4797, 4772, 4746, 4721, 4695, 4670, 4644, 4619, 4594, 4569, 4543, 4518, 4493, 4468, 4443, 4418, 4393, 4368, 4343, 4318, 4294, 4269, 4244, 4220, 4195, 4170, 4146, 4122, 4097, 4073, 4048, 4024, 4000, 3976, 3952, 3928, 3904, 3880, 3856, 3832, 3808, 3784, 3761, 3737, 3713, 3690, 3666, 3643, 3619, 3596, 3573, 3550, 3526, 3503, 3480, 3457, 3434, 3411, 3389, 3366, 3343, 3320, 3298, 3275, 3253, 3230, 3208, 3185, 3163, 3141, 3119, 3097, 3075, 3053, 3031, 3009, 2987, 2965, 2944, 2922, 2901, 2879, 2858, 2836, 2815, 2794, 2773, 2752, 2730, 2710, 2689, 2668, 2647, 2626, 2606, 2585, 2564, 2544, 2524, 2503, 2483, 2463, 2443, 2423, 2403, 2383, 2363, 2343, 2323, 2304, 2284, 2265, 2245, 2226, 2207, 2187, 2168, 2149, 2130, 2111, 2092, 2074, 2055, 2036, 2018, 1999, 1981, 1962, 1944, 1926, 1908, 1890, 1872, 1854, 1836, 1818, 1800, 1783, 1765, 1748, 1730, 1713, 1696, 1679, 1662, 1645, 1628, 1611, 1594, 1577, 1561, 1544, 1528, 1511, 1495, 1479, 1463, 1447, 1431, 1415, 1399, 1383, 1368, 1352, 1337, 1321, 1306, 1291, 1275, 1260, 1245, 1230, 1216, 1201, 1186, 1172, 1157, 1143, 1128, 1114, 1100, 1086, 1072, 1058, 1044, 1030, 1017, 1003, 990, 976, 963, 950, 936, 923, 910, 897, 885, 872, 859, 847, 834, 822, 810, 797, 785, 773, 761, 750, 738, 726, 715, 703, 692, 680, 669, 658, 647, 636, 625, 614, 604, 593, 583, 572, 562, 552, 541, 531, 521, 512, 502, 492, 482, 473, 464, 454, 445, 436, 427, 418, 409, 400, 392, 383, 374, 366, 358, 350, 341, 333, 325, 318, 310, 302, 295, 287, 280, 273, 265, 258, 251, 244, 238, 231, 224, 218, 211, 205, 199, 193, 187, 181, 175, 169, 163, 158, 152, 147, 142, 137, 131, 126, 122, 117, 112, 107, 103, 98, 94, 90, 86, 82, 78, 74, 70, 67, 63, 60, 56, 53, 50, 47, 44, 41, 38, 36, 33, 30, 28, 26, 24, 21, 19, 18, 16, 14, 12, 11, 10, 8, 7, 6, 5, 4, 3, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 14, 16, 18, 19, 21, 24, 26, 28, 30, 33, 36, 38, 41, 44, 47, 50, 53, 56, 60, 63, 67, 70, 74, 78, 82, 86, 90, 94, 98, 103, 107, 112, 117, 122, 126, 131, 137, 142, 147, 152, 158, 163, 169, 175, 181, 187, 193, 199, 205, 211, 218, 224, 231, 238, 244, 251, 258, 265, 273, 280, 287, 295, 302, 310, 318, 325, 333, 341, 350, 358, 366, 374, 383, 392, 400, 409, 418, 427, 436, 445, 454, 464, 473, 482, 492, 502, 512, 521, 531, 541, 552, 562, 572, 583, 593, 604, 614, 625, 636, 647, 658, 669, 680, 692, 703, 715, 726, 738, 750, 761, 773, 785, 797, 810, 822, 834, 847, 859, 872, 885, 897, 910, 923, 936, 950, 963, 976, 990, 1003, 1017, 1030, 1044, 1058, 1072, 1086, 1100, 1114, 1128, 1143, 1157, 1172, 1186, 1201, 1216, 1230, 1245, 1260, 1275, 1291, 1306, 1321, 1337, 1352, 1368, 1383, 1399, 1415, 1431, 1447, 1463, 1479, 1495, 1511, 1528, 1544, 1561, 1577, 1594, 1611, 1628, 1645, 1662, 1679, 1696, 1713, 1730, 1748, 1765, 1783, 1800, 1818, 1836, 1854, 1872, 1890, 1908, 1926, 1944, 1962, 1981, 1999, 2018, 2036, 2055, 2074, 2092, 2111, 2130, 2149, 2168, 2187, 2207, 2226, 2245, 2265, 2284, 2304, 2323, 2343, 2363, 2383, 2403, 2423, 2443, 2463, 2483, 2503, 2524, 2544, 2564, 2585, 2606, 2626, 2647, 2668, 2689, 2710, 2730, 2752, 2773, 2794, 2815, 2836, 2858, 2879, 2901, 2922, 2944, 2965, 2987, 3009, 3031, 3053, 3075, 3097, 3119, 3141, 3163, 3185, 3208, 3230, 3253, 3275, 3298, 3320, 3343, 3366, 3389, 3411, 3434, 3457, 3480, 3503, 3526, 3550, 3573, 3596, 3619, 3643, 3666, 3690, 3713, 3737, 3761, 3784, 3808, 3832, 3856, 3880, 3904, 3928, 3952, 3976, 4000, 4024, 4048, 4073, 4097, 4122, 4146, 4170, 4195, 4220, 4244, 4269, 4294, 4318, 4343, 4368, 4393, 4418, 4443, 4468, 4493, 4518, 4543, 4569, 4594, 4619, 4644, 4670, 4695, 4721, 4746, 4772, 4797, 4823, 4848, 4874, 4900, 4926, 4951, 4977, 5003, 5029, 5055, 5081, 5107, 5133, 5159, 5185, 5211, 5238, 5264, 5290, 5316, 5343, 5369, 5395, 5422, 5448, 5475, 5501, 5528, 5554, 5581, 5608, 5634, 5661, 5688, 5714, 5741, 5768, 5795, 5822, 5849, 5876, 5902, 5929, 5956, 5983, 6010, 6038, 6065, 6092, 6119, 6146, 6173, 6200, 6228, 6255, 6282, 6309, 6337, 6364, 6391, 6419, 6446, 6474, 6501, 6528, 6556, 6583, 6611, 6638, 6666, 6693, 6721, 6749, 6776, 6804, 6831, 6859, 6887, 6914, 6942, 6970, 6997, 7025, 7053, 7081, 7108, 7136, 7164, 7192, 7219, 7247, 7275, 7303, 7331, 7358, 7386, 7414, 7442, 7470, 7498, 7526, 7553, 7581, 7609, 7637, 7665, 7693, 7721, 7749, 7777, 7805, 7832, 7860, 7888, 7916, 7944, 7972
  };
  while (1) {
    TIM14->CCR1 = TIM16->CCR1 = TIM17->CCR1 = 0;
    while (HAL_GPIO_ReadPin(GPIOA, PIN_BUTTON) == 1)
      sleep_delay(100);
    for (int i = 0; i < N * 3; i++) {
      TIM14->CCR1 = sin_lut[i % N] / 40;
      TIM16->CCR1 = sin_lut[(i + N / 3) % N] / 40;
      TIM17->CCR1 = sin_lut[(i + N * 2 / 3) % N] / 40;
      for (int i = 0; i < 100; i++) asm volatile ("nop");
    }
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
  spi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;  // APB / 2 = 500 kHz
  HAL_SPI_Init(&spi1);
  __HAL_SPI_ENABLE(&spi1);

if (0) {
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
    HAL_GPIO_WritePin(GPIOB, PIN_LED_R, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOB, PIN_LED_R, GPIO_PIN_SET);
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
}

  for (int i = 0; i < 10; i++) {
    swv_printf("blink!\n");
/*
    HAL_GPIO_WritePin(GPIOB, PIN_LED_R, GPIO_PIN_SET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(GPIOB, PIN_LED_R, GPIO_PIN_RESET);
    HAL_Delay(200);
*/
  }
  HAL_GPIO_WritePin(GPIOB, PIN_LED_R, GPIO_PIN_SET);
  HAL_Delay(1000);
}

void SysTick_Handler()
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
}
