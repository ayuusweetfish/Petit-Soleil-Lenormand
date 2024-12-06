#include <stm32g0xx_hal.h>
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef RELEASE
#define _release_inline
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
__attribute__ ((format(printf, 1, 2)))
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
#else
#define _release_inline inline
#define swv_printf(...)
#endif

static SPI_HandleTypeDef spi1;

#pragma GCC push_options
#pragma GCC optimize("O3")
static inline void spi_transmit(const uint8_t *data, size_t size)
{
  HAL_SPI_Transmit(&spi1, (uint8_t *)data, size, 1000); return;
}

static inline void spi_receive(uint8_t *data, size_t size)
{
  HAL_SPI_Receive(&spi1, data, size, 1000); return;
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

void flash_power_down()
{
  uint8_t op_power_down[] = {0xB9};
  flash_cmd(op_power_down);
}

int main()
{
  HAL_Init();

  // ======== GPIO ========
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  // SWD (PA13, PA14)
  HAL_GPIO_Init(GPIOA, &(GPIO_InitTypeDef){
    .Pin = GPIO_PIN_13 | GPIO_PIN_14,
    .Mode = GPIO_MODE_AF_PP,
    .Alternate = GPIO_AF0_SWJ,
    .Pull = GPIO_PULLUP,
    .Speed = GPIO_SPEED_FREQ_HIGH,
  });

  HAL_Delay(2000);

  // ======== SPI ========
  // GPIO ports
  // SPI1_SCK (PA5), SPI1_MISO (PA6), SPI1_MOSI (PA7)
  HAL_GPIO_Init(GPIOA, &(GPIO_InitTypeDef){
    .Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7,
    .Mode = GPIO_MODE_AF_PP,
    .Alternate = GPIO_AF0_SPI1,
    .Pull = GPIO_NOPULL,
    .Speed = GPIO_SPEED_FREQ_HIGH,
  });
  // Output FLASH_CSN (PB9)
  HAL_GPIO_Init(GPIOB, &(GPIO_InitTypeDef){
    .Pin = GPIO_PIN_9,
    .Mode = GPIO_MODE_OUTPUT_PP,
  });
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, 1);

  __HAL_RCC_SPI1_CLK_ENABLE();
  spi1 = (SPI_HandleTypeDef){
    .Instance = SPI1,
    .Init = {
      .Mode = SPI_MODE_MASTER,
      .Direction = SPI_DIRECTION_2LINES,
      .CLKPolarity = SPI_POLARITY_LOW,
      .CLKPhase = SPI_PHASE_1EDGE,
      .NSS = SPI_NSS_SOFT,
      .FirstBit = SPI_FIRSTBIT_MSB,
      .TIMode = SPI_TIMODE_DISABLE,
      .CRCCalculation = SPI_CRCCALCULATION_DISABLE,
      .DataSize = SPI_DATASIZE_8BIT,
      .BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2,
    },
  };
  HAL_SPI_Init(&spi1);
  __HAL_SPI_ENABLE(&spi1);

  flash_power_down();

  // __HAL_RCC_GPIOA_CLK_DISABLE();
  // __HAL_RCC_GPIOB_CLK_DISABLE();
  // 3,6 µA

  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_SuspendTick();
  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
  while (1) { }
}

void SysTick_Handler()
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
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
void EXTI2_3_IRQHandler() { while (1) { } }
void EXTI4_15_IRQHandler() { while (1) { } }
void DMA1_Channel1_IRQHandler() { while (1) { } }
void DMA1_Channel2_3_IRQHandler() { while (1) { } }
void DMA1_Ch4_5_DMAMUX1_OVR_IRQHandler() { while (1) { } }
void ADC1_IRQHandler() { while (1) { } }
void TIM1_BRK_UP_TRG_COM_IRQHandler() { while (1) { } }
void TIM1_CC_IRQHandler() { while (1) { } }
void TIM3_IRQHandler() { while (1) { } }
void TIM14_IRQHandler() { while (1) { } }
void TIM16_IRQHandler() { while (1) { } }
void TIM17_IRQHandler() { while (1) { } }
void I2C1_IRQHandler() { while (1) { } }
void I2C2_IRQHandler() { while (1) { } }
void SPI1_IRQHandler() { while (1) { } }
void SPI2_IRQHandler() { while (1) { } }
void USART1_IRQHandler() { while (1) { } }
void USART2_IRQHandler() { while (1) { } }
