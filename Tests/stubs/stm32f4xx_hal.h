#ifndef TEST_STM32F4XX_HAL_H
#define TEST_STM32F4XX_HAL_H

#include <stdint.h>

typedef enum
{
  HAL_OK = 0,
  HAL_ERROR,
  HAL_BUSY,
  HAL_TIMEOUT
} HAL_StatusTypeDef;

typedef enum
{
  HAL_SPI_STATE_RESET = 0,
  HAL_SPI_STATE_READY,
  HAL_SPI_STATE_BUSY,
  HAL_SPI_STATE_ERROR
} HAL_SPI_StateTypeDef;

typedef struct
{
  HAL_SPI_StateTypeDef state;
  uint32_t error;
} SPI_HandleTypeDef;

typedef struct
{
  uint32_t unused;
} GPIO_TypeDef;

typedef enum
{
  GPIO_PIN_RESET = 0,
  GPIO_PIN_SET = 1
} GPIO_PinState;

#define HAL_SPI_ERROR_NONE 0U
#define GPIOH ((GPIO_TypeDef *)0x1)
#define GPIO_PIN_7 (1U << 7)

HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *spi,
                                       const uint8_t *data,
                                       uint16_t size);
HAL_SPI_StateTypeDef HAL_SPI_GetState(const SPI_HandleTypeDef *spi);
uint32_t HAL_SPI_GetError(const SPI_HandleTypeDef *spi);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *spi);
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *port,
                       uint16_t pin,
                       GPIO_PinState state);

#endif
