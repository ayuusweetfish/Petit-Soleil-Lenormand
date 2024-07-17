#include <stm32g0xx_hal.h>

#define PIN_LED_R     GPIO_PIN_6
#define PIN_LED_G     GPIO_PIN_7
#define PIN_LED_B     GPIO_PIN_1

static const int CONSUMPTION = 0;

void setup_clocks()
{
if (CONSUMPTION == 0) {
  HAL_PWREx_EnableLowPowerRunMode();
}

  RCC_OscInitTypeDef osc_init = { 0 };
  osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc_init.HSIState = RCC_HSI_ON;
if (CONSUMPTION <= 1) {
  osc_init.PLL.PLLState = RCC_PLL_OFF;
} else {
  osc_init.PLL.PLLState = RCC_PLL_ON;
  osc_init.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  osc_init.PLL.PLLM = RCC_PLLM_DIV2;
  osc_init.PLL.PLLN = 8;
  osc_init.PLL.PLLP = RCC_PLLP_DIV2;
  osc_init.PLL.PLLR = RCC_PLLR_DIV2;
}
  HAL_RCC_OscConfig(&osc_init);

  RCC_ClkInitTypeDef clk_init = { 0 };
  clk_init.ClockType =
    RCC_CLOCKTYPE_SYSCLK |
    RCC_CLOCKTYPE_HCLK |
    RCC_CLOCKTYPE_PCLK1;
if (CONSUMPTION == 0) {
  clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_HSI; // 16 MHz
  clk_init.AHBCLKDivider = RCC_SYSCLK_DIV16;    // 1 MHz
  clk_init.APB1CLKDivider = RCC_HCLK_DIV16;     // 1 MHz
} else if (CONSUMPTION == 1) {
  clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_HSI; // 16 MHz
  clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;     // 16 MHz
  clk_init.APB1CLKDivider = RCC_HCLK_DIV1;      // 16 MHz
} else {
  clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;  // 64 MHz
  clk_init.AHBCLKDivider = RCC_SYSCLK_DIV16;    // 4 MHz
  clk_init.APB1CLKDivider = RCC_HCLK_DIV1;      // 4 MHz
}
  HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_1);
}

int main()
{
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

  setup_clocks();

  gpio_init.Pin = PIN_LED_R | PIN_LED_G | PIN_LED_B;
  gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
  gpio_init.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &gpio_init);
  HAL_GPIO_WritePin(GPIOB, PIN_LED_R | PIN_LED_G | PIN_LED_B, 0);
  while (1) {
    HAL_Delay(500);
    HAL_GPIO_WritePin(GPIOB, PIN_LED_R, 1);
    HAL_Delay(500);
    HAL_GPIO_WritePin(GPIOB, PIN_LED_G, 1);
    HAL_GPIO_WritePin(GPIOB, PIN_LED_R, 0);
    HAL_Delay(500);
    HAL_GPIO_WritePin(GPIOB, PIN_LED_B, 1);
    HAL_GPIO_WritePin(GPIOB, PIN_LED_G, 0);
    HAL_Delay(500);
    HAL_GPIO_WritePin(GPIOB, PIN_LED_B, 0);
  }
}

void SysTick_Handler()
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
}
