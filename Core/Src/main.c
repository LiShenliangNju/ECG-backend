/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"
#include "delay.h"
#include "IIR.h"
#include "ecg_acquisition.h"
#include "ecg_display.h"
#include "ecg_measure.h"
#include "ecg_rpeak.h"
#include "ecg_signal.h"
#include "heart_rate.h"
#include "key_input.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  ECG_FAULT_SYSTEM_INIT = 0U,
  ECG_FAULT_GPIO_INIT,
  ECG_FAULT_DMA_INIT,
  ECG_FAULT_SPI_INIT,
  ECG_FAULT_LCD_INIT,
  ECG_FAULT_ADC_INIT,
  ECG_FAULT_TIM_INIT,
  ECG_FAULT_CLOCK_CONFIG,
  ECG_FAULT_ACQUISITION_START,
  ECG_FAULT_DMA_OVERRUN,
  ECG_FAULT_BLOCK_RELEASE,
  ECG_FAULT_ACQUISITION_API
} ECG_FaultReason;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static ECG_RPeakDetector g_rpeak_detector;
static ECG_HeartRate g_heart_rate;
static ECG_MeasureState g_measure_state;
static ECG_KeyInput g_key1;
static ECG_KeyInput g_key2;
static ECG_DownsamplerState g_downsampler;
static uint8_t g_signal_initialized = 0U;
static float32_t g_filtered_buffer[ECG_DMA_HALF_LENGTH];
static float32_t g_downsampled_buffer[ECG_DMA_HALF_LENGTH / ECG_DOWNSAMPLE_FACTOR];
static uint32_t g_sample_index = 0U;
static uint8_t g_lcd_initialized = 0U;
static uint8_t g_acquisition_started = 0U;
volatile ECG_FaultReason g_ecg_fault_reason = ECG_FAULT_SYSTEM_INIT;
volatile uint32_t g_ecg_fault_detail;
volatile uint32_t g_ecg_shutdown_status;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
static void ECG_InitializeTopLevelModules(void);
static void ECG_ProcessSampleBlock(const ECG_SampleBlock *block);
static void ECG_UpdateKeyThresholds(void);
static void ECG_StopWithFault(ECG_FaultReason reason, uint32_t detail);
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  g_ecg_fault_reason = ECG_FAULT_GPIO_INIT;
  MX_GPIO_Init();
  g_ecg_fault_reason = ECG_FAULT_DMA_INIT;
  MX_DMA_Init();
  g_ecg_fault_reason = ECG_FAULT_SPI_INIT;
  MX_SPI5_Init();
  delay_init(100U);
  g_ecg_fault_reason = ECG_FAULT_LCD_INIT;
  LCD_Init();
  g_lcd_initialized = 1U;

  ECG_DisplayState display_state;
  ECG_Display_Init(&display_state);
  ECG_Display_DrawStaticFrame(&display_state);

  g_ecg_fault_reason = ECG_FAULT_ADC_INIT;
  MX_ADC1_Init();
  g_ecg_fault_reason = ECG_FAULT_TIM_INIT;
  MX_TIM2_Init();
  ECG_InitializeTopLevelModules();
  g_ecg_fault_reason = ECG_FAULT_SYSTEM_INIT;

  /* USER CODE BEGIN 2 */
  const HAL_StatusTypeDef acquisition_status = ECG_Acquisition_Start();
  if (acquisition_status != HAL_OK)
  {
    ECG_StopWithFault(ECG_FAULT_ACQUISITION_START,
                      (uint32_t)acquisition_status);
  }
  g_acquisition_started = 1U;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    ECG_SampleBlock block;
    const ECG_AcquisitionResult block_result =
        ECG_Acquisition_TryGetBlock(&block);

    if (block_result == ECG_ACQUISITION_RESULT_BLOCK_READY)
    {
      ECG_ProcessSampleBlock(&block);
      if (ECG_Acquisition_ReleaseBlock(block.half) == 0U)
      {
        ECG_StopWithFault(ECG_FAULT_BLOCK_RELEASE, (uint32_t)block.half);
      }
      HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
    }
    else if (block_result == ECG_ACQUISITION_RESULT_FAULT_LATCHED)
    {
      ECG_StopWithFault(ECG_FAULT_DMA_OVERRUN,
                        ECG_Acquisition_GetOverrunCount());
    }
    else if (block_result == ECG_ACQUISITION_RESULT_BLOCK_ACTIVE)
    {
      ECG_StopWithFault(ECG_FAULT_ACQUISITION_API,
                        (uint32_t)block_result);
    }
    else if (block_result != ECG_ACQUISITION_RESULT_NO_BLOCK)
    {
      ECG_StopWithFault(ECG_FAULT_ACQUISITION_API,
                        (uint32_t)block_result);
    }

    ECG_UpdateKeyThresholds();

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 200;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    ECG_StopWithFault(ECG_FAULT_CLOCK_CONFIG, 1U);
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    ECG_StopWithFault(ECG_FAULT_CLOCK_CONFIG, 2U);
  }
}

/* USER CODE BEGIN 4 */
static void ECG_StopWithFault(ECG_FaultReason reason, uint32_t detail)
{
  g_ecg_fault_reason = reason;
  g_ecg_fault_detail = detail;
  Error_Handler();
}

static void ECG_InitializeTopLevelModules(void)
{
  ECG_RPeak_Init(&g_rpeak_detector);
  ECG_HeartRate_Init(&g_heart_rate);
  ECG_Measure_Init(&g_measure_state);
  ECG_KeyInput_Init(&g_key1);
  ECG_KeyInput_Init(&g_key2);
  ECG_Signal_Downsampler_Init(&g_downsampler);
  IIR_Notch_Init();
  g_signal_initialized = 1U;
}

static void ECG_ProcessSampleBlock(const ECG_SampleBlock *block)
{
  if ((block == NULL) || (block->samples == NULL) || (block->sample_count == 0U))
  {
    return;
  }

  uint32_t converted_count = 0U;
  const uint32_t block_start = block->first_sample_index;
  const uint32_t block_end = block_start + block->sample_count;

  for (uint32_t index = 0U; index < block->sample_count; ++index)
  {
    const uint32_t sample_index = block_start + index;
    float32_t millivolts = 0.0f;
    ECG_Signal_AdcCodeToMillivolts(block->samples[index], &millivolts);
    g_filtered_buffer[index] = millivolts;
    g_sample_index = sample_index;
    ECG_Measure_ProcessSample(&g_measure_state, sample_index, millivolts);
  }

  IIR_Notch_Process(g_filtered_buffer, block->sample_count);

  converted_count = ECG_Signal_DownsampleAverage(&g_downsampler,
                                                g_filtered_buffer,
                                                block->sample_count,
                                                g_downsampled_buffer,
                                                (uint32_t)(sizeof(g_downsampled_buffer) / sizeof(g_downsampled_buffer[0])));

  for (uint32_t index = 0U; index < converted_count; ++index)
  {
    const uint32_t sample_index_1k = (block_start / ECG_DOWNSAMPLE_FACTOR) + index;
    ECG_RPeakEvent event;
    if (ECG_RPeak_ProcessSample(&g_rpeak_detector,
                                g_downsampled_buffer[index],
                                sample_index_1k,
                                &event) != 0U)
    {
      ECG_HeartRateResult heart_rate_result;
      if (ECG_HeartRate_OnPeak(&g_heart_rate, event.sample_index, &heart_rate_result) != 0U)
      {
        ECG_BeatMeasurement measurement;
        if (ECG_Measure_OnRPeak(&g_measure_state, event.sample_index, &measurement) != 0U)
        {
          (void)measurement;
        }
      }
    }
  }

  (void)block_end;
}

static void ECG_UpdateKeyThresholds(void)
{
  const uint32_t now_ms = HAL_GetTick();
  ECG_KeyEvent key1_event = ECG_KeyInput_Update(&g_key1, now_ms,
                                               (uint8_t)(HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET));
  ECG_KeyEvent key2_event = ECG_KeyInput_Update(&g_key2, now_ms,
                                               (uint8_t)(HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin) == GPIO_PIN_RESET));

  if (key1_event != ECG_KEY_EVENT_NONE)
  {
    ECG_RPeak_SetThreshold(&g_rpeak_detector,
                           g_rpeak_detector.threshold_mv +
                               (key1_event == ECG_KEY_EVENT_LONG_PRESS || key1_event == ECG_KEY_EVENT_REPEAT ?
                                ECG_KEY_LONG_STEP_MV : ECG_KEY_SHORT_STEP_MV));
  }

  if (key2_event != ECG_KEY_EVENT_NONE)
  {
    ECG_RPeak_SetThreshold(&g_rpeak_detector,
                           g_rpeak_detector.threshold_mv -
                               (key2_event == ECG_KEY_EVENT_LONG_PRESS || key2_event == ECG_KEY_EVENT_REPEAT ?
                                ECG_KEY_LONG_STEP_MV : ECG_KEY_SHORT_STEP_MV));
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  static const uint8_t system_init_message[] = "SYSTEM INIT ERROR";
  static const uint8_t gpio_init_message[] = "GPIO INIT ERROR";
  static const uint8_t dma_init_message[] = "DMA INIT ERROR";
  static const uint8_t spi_init_message[] = "SPI INIT ERROR";
  static const uint8_t lcd_init_message[] = "LCD INIT ERROR";
  static const uint8_t adc_init_message[] = "ADC INIT ERROR";
  static const uint8_t tim_init_message[] = "TIM INIT ERROR";
  static const uint8_t clock_message[] = "CLOCK CONFIG ERROR";
  static const uint8_t acquisition_start_message[] = "ACQ START FAILED";
  static const uint8_t overrun_message[] = "DMA OVERRUN";
  static const uint8_t release_message[] = "BLOCK RELEASE ERROR";
  static const uint8_t api_message[] = "ACQ API ERROR";
  const uint8_t *fault_message = system_init_message;

  if (g_acquisition_started != 0U)
  {
    if (HAL_TIM_Base_Stop(&htim2) != HAL_OK)
    {
      g_ecg_shutdown_status |= 1U;
    }
    if (HAL_ADC_Stop_DMA(&hadc1) != HAL_OK)
    {
      g_ecg_shutdown_status |= 2U;
    }
    g_acquisition_started = 0U;
  }

  switch (g_ecg_fault_reason)
  {
    case ECG_FAULT_GPIO_INIT:
      fault_message = gpio_init_message;
      break;
    case ECG_FAULT_DMA_INIT:
      fault_message = dma_init_message;
      break;
    case ECG_FAULT_SPI_INIT:
      fault_message = spi_init_message;
      break;
    case ECG_FAULT_LCD_INIT:
      fault_message = lcd_init_message;
      break;
    case ECG_FAULT_ADC_INIT:
      fault_message = adc_init_message;
      break;
    case ECG_FAULT_TIM_INIT:
      fault_message = tim_init_message;
      break;
    case ECG_FAULT_CLOCK_CONFIG:
      fault_message = clock_message;
      break;
    case ECG_FAULT_ACQUISITION_START:
      fault_message = acquisition_start_message;
      break;
    case ECG_FAULT_DMA_OVERRUN:
      fault_message = overrun_message;
      break;
    case ECG_FAULT_BLOCK_RELEASE:
      fault_message = release_message;
      break;
    case ECG_FAULT_ACQUISITION_API:
      fault_message = api_message;
      break;
    case ECG_FAULT_SYSTEM_INIT:
    default:
      fault_message = system_init_message;
      break;
  }

  if (g_lcd_initialized != 0U)
  {
    LCD_Fill(0U, 0U, ECG_DISPLAY_WIDTH, ECG_DISPLAY_HEIGHT, BLACK);
    LCD_ShowString(12U, 48U, (const uint8_t *)"SYSTEM HALTED",
                   RED, BLACK, 2U, 0U);
    LCD_ShowString(12U, 88U, fault_message, WHITE, BLACK, 2U, 0U);
    if (g_ecg_shutdown_status == 0U)
    {
      LCD_ShowString(12U, 144U, (const uint8_t *)"RESET BOARD",
                     YELLOW, BLACK, 2U, 0U);
    }
    else
    {
      LCD_ShowString(12U, 144U, (const uint8_t *)"STOP FAILED",
                     YELLOW, BLACK, 2U, 0U);
    }
  }

  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
