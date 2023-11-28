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
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pin = PIN_EP_NCS | PIN_EP_DCC | PIN_EP_NRST;
  HAL_GPIO_Init(GPIOA, &gpio_init);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NCS, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_DCC, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA, PIN_EP_NRST, GPIO_PIN_SET);
  // Input EP_BUSY (PA12)
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pin = PIN_EP_BUSY;
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
