/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
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
#include "cmsis_os.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7789.h"
#include <string.h> // 添加这一行，解决 memset 未声明问题
#include <stdio.h>
#include <key.h>
#include <read_adc.h>
#include <beep.h>
#include <stdlib.h>
#include "stm32f4xx_it.h"
#include "pairing_common.h"
#include "pairing_receiver.h"
#include "pairing_remote.h"
#include "E34.h"
#include "status.h"
#include "pairing_receiver.h"
#include <connet_check.h>
#include "heartbeat.h"
#include "A28.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
// 重定向printf到串口（使用USART3，9600 baud）
int __io_putchar(int ch)
{
  HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
  return ch;
}
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

uint16_t Joystick[4] = {0}; // 定义摇杆数组
uint16_t key[8] = {0};      // 定义摇杆数组
uint8_t rx_buffer[UART_RX_BUF_SIZE] = {0};
uint8_t rx_buffer1[UART_RX_BUF_SIZE] = {0};
uint8_t status[24] = {0, 0, 0, 0, 0, 0, 0, 250, 250, 250, 250, 4, 0,0,0,0,0,0,0,0,0,0,0}; // 预设一个初始状态，电压250(25.0V)，电流250(25.0A)，高度4

uint8_t receiver_status[8];
// volatile uint16_t new_address = 0;
volatile uint16_t new_address1 = 0;
volatile uint8_t TESTok = 0;
volatile uint8_t TEST_OK = 0;
uint8_t lock = 1;
uint16_t voltage_int = 0;  // 36.5V -> 365
uint16_t currentA_int = 0; // 1.2A -> 12

// 回传状态
uint8_t Current_receiver_height = 20;
float Current_rpm = 0;
uint8_t Current_Bag = 0;
// 遥控状态
uint8_t grass_bag_status = 0; // 0=关闭, 1=打开
uint8_t target_height = 20;
uint8_t Target_height = 20;
uint8_t Current_height = 20;
uint8_t last_connected = 0;
uint8_t last_height = 20;


#define UART_RX_DMA_BUF_SIZE 64
uint8_t uart_rx_dma_buf[UART_RX_DMA_BUF_SIZE];

// 7. 用于按键频率控制的变量
static uint32_t last_height_adjust_time = 0;

static uint8_t last_collision_status = 255; // 初始化为一个不可能的值(或者1)，确保第一次同步
static uint8_t last_speed_status = 255;     // 初始化为一个不可能的值(或者1)，确保第一次同步
static uint8_t last_hight_status = 255;     // 初始化为一个不可能的值(或者1)，确保第一次同步
static uint8_t last_lock_status = 255;      // 初始化为一个不可能的值(或者1)，确保第一次同步

// [0]=LX, [1]=LY, [2]=RX, [3]=RY, [4]=Battery(Ch5)
uint16_t adc_dma_buffer[5];

Heartbeat_Handle_t g_hb_remote;

static uint32_t last_send_tick = 0;

static uint16_t last_lx = 0, last_ly = 0, last_rx = 0, last_ry = 0;

static uint32_t last_ui_update = 0;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
void SPI_LoopbackTest(void);
// 重定向printf到串口（使用USART3，9600 baud）
// int __io_putchar(int ch)
// {
//   HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
//   return ch;
// }
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    // 清除 ORE 错误
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE) != RESET)
    {
      __HAL_UART_CLEAR_OREFLAG(huart);
      volatile uint32_t temp = huart->Instance->DR;
      (void)temp;
    }
    // 只要出错，马上重启 DMA！
    HAL_UART_Receive_DMA(huart, rx_buffer, 255);
  }
}
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
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  // E34_Mode_GPIO_Init();
  //E34_Set_FullDuplex_Mode();
  A28_Set_FullDuplex_Mode();
  KEY_Init();

  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buffer, 5);
  // HAL_Delay(10);

  // // 初始化心跳
  // Heartbeat_Init(&g_hb_remote);
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
// 串口发送完成中断回调
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    // 判断是不是我们要处理的串口3 (连接无线模块的串口)
    if (huart->Instance == USART3) 
    {
        extern osSemaphoreId_t Sem_Uart3TxHandle;
        if (Sem_Uart3TxHandle != NULL) 
        {
            // 在中断中释放信号量，唤醒 Safe_UART_DMA_Send 中等待的任务
            osSemaphoreRelease(Sem_Uart3TxHandle);
        }
    }
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();

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
