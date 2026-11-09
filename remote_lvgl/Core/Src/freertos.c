/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : freertos.c
 * Description        : Code for freertos applications
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
//  ______   ___   _          _ __     ______ _     
// |  _ \ \ / / | | |        | |\ \   / / ___| |    
// | | | \ V /| |_| |        | | \ \ / / |  _| |    
// | |_| || | |  _  |        | |__\ V /| |_| | |___ 
// |____/ |_| |_| |_|        |_____\_/  \____|_____|

/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "app_vars.h"
#include "app_gui.h"
#include "app_control.h"
#include "app_comm.h"
#include "app_beep.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* 全局实例，方便在 Watch 窗口查看 */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for GUITask */
osThreadId_t GUITaskHandle;
const osThreadAttr_t GUITask_attributes = {
  .name = "GUITask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for ControlTask */
osThreadId_t ControlTaskHandle;
const osThreadAttr_t ControlTask_attributes = {
  .name = "ControlTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for RxTask */
osThreadId_t RxTaskHandle;
const osThreadAttr_t RxTask_attributes = {
  .name = "RxTask",
  .stack_size = 1048 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for BeeperTask */
osThreadId_t BeeperTaskHandle;
const osThreadAttr_t BeeperTask_attributes = {
  .name = "BeeperTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for uartRxQueue */
osMessageQueueId_t uartRxQueueHandle;
const osMessageQueueAttr_t uartRxQueue_attributes = {
  .name = "uartRxQueue"
};
/* Definitions for beepQueue */
osMessageQueueId_t beepQueueHandle;
const osMessageQueueAttr_t beepQueue_attributes = {
  .name = "beepQueue"
};
/* Definitions for UartTxMutex */
osMutexId_t UartTxMutexHandle;
const osMutexAttr_t UartTxMutex_attributes = {
  .name = "UartTxMutex"
};
/* Definitions for BinarySem_UartRx */
osSemaphoreId_t BinarySem_UartRxHandle;
const osSemaphoreAttr_t BinarySem_UartRx_attributes = {
  .name = "BinarySem_UartRx"
};
/* Definitions for Sem_Uart3Tx */
osSemaphoreId_t Sem_Uart3TxHandle;
const osSemaphoreAttr_t Sem_Uart3Tx_attributes = {
  .name = "Sem_Uart3Tx"
};
/* Definitions for WdgEvent */
osEventFlagsId_t WdgEventHandle;
const osEventFlagsAttr_t WdgEvent_attributes = {
  .name = "WdgEvent"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

void Update_Error_Popup(void);
void Update_System_Monitor(void);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartGUITask(void *argument);
void StartControlTask(void *argument);
void StartRxTask(void *argument);
void StartBeeperTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
  /* Run time stack overflow checking is performed if
  configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
  called if a stack overflow is detected. */
  taskDISABLE_INTERRUPTS();
  for (;;)
    ;
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
  /* vApplicationMallocFailedHook() will only be called if
  configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
  function that will get called if a call to pvPortMalloc() fails.
  pvPortMalloc() is called internally by the kernel whenever a task, queue,
  timer or semaphore is created. It is also called by various parts of the
  demo application. If heap_1.c or heap_2.c are used, then the size of the
  heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
  FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
  to query the size of free heap space that remains (although it does not
  provide information on how the remaining heap might be fragmented). */

  // 可以在这里打断点，或者翻转 LED 报警
  taskDISABLE_INTERRUPTS();
  for (;;)
    ;
}
/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of UartTxMutex */
  UartTxMutexHandle = osMutexNew(&UartTxMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of BinarySem_UartRx */
  BinarySem_UartRxHandle = osSemaphoreNew(1, 1, &BinarySem_UartRx_attributes);

  /* creation of Sem_Uart3Tx */
  Sem_Uart3TxHandle = osSemaphoreNew(1, 0, &Sem_Uart3Tx_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of uartRxQueue */
  uartRxQueueHandle = osMessageQueueNew (16, 68, &uartRxQueue_attributes);

  /* creation of beepQueue */
  beepQueueHandle = osMessageQueueNew (4, sizeof(uint8_t), &beepQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of GUITask */
  GUITaskHandle = osThreadNew(StartGUITask, NULL, &GUITask_attributes);

  /* creation of ControlTask */
  ControlTaskHandle = osThreadNew(StartControlTask, NULL, &ControlTask_attributes);

  /* creation of RxTask */
  RxTaskHandle = osThreadNew(StartRxTask, NULL, &RxTask_attributes);

  /* creation of BeeperTask */
  BeeperTaskHandle = osThreadNew(StartBeeperTask, NULL, &BeeperTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* creation of WdgEvent */
  WdgEventHandle = osEventFlagsNew(&WdgEvent_attributes);

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief  Function implementing the defaultTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */

  /* Infinite loop */
  for (;;)
  {
    Update_System_Monitor();
    osDelay(500);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartGUITask */
/**
 * @brief Function implementing the GUITask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartGUITask */
void StartGUITask(void *argument)
{
  /* USER CODE BEGIN StartGUITask */
  /* Infinite loop */
  // 调用应用层入口
  App_GUI_Main();
  /* USER CODE END StartGUITask */
}

/* USER CODE BEGIN Header_StartControlTask */
/**
 * @brief Function implementing the ControlTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartControlTask */
void StartControlTask(void *argument)
{
  /* USER CODE BEGIN StartControlTask */

  /* Infinite loop */
  App_Control_Main();

  /* USER CODE END StartControlTask */
}

/* USER CODE BEGIN Header_StartRxTask */
/**
 * @brief Function implementing the RxTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartRxTask */
void StartRxTask(void *argument)
{
  /* USER CODE BEGIN StartRxTask */

  /* Infinite loop */
  App_Comm_Rx_Main();
  /* USER CODE END StartRxTask */
}

/* USER CODE BEGIN Header_StartBeeperTask */
/**
 * @brief Function implementing the BeeperTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartBeeperTask */
void StartBeeperTask(void *argument)
{
  /* USER CODE BEGIN StartBeeperTask */

  /* Infinite loop */
  App_Beep_Main();
  /* USER CODE END StartBeeperTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */


void Update_System_Monitor(void)
{
  // 1. 获取各个任务的历史最小剩余栈空间 (High Water Mark)
  // 注意：返回值单位是 Stack Type (在STM32上通常是 4字节/Word)
  // 如果返回 0，说明栈溢出了！

  if (ControlTaskHandle != NULL)
  {
    g_SysMonitor.Control_FreeStack = uxTaskGetStackHighWaterMark((TaskHandle_t)ControlTaskHandle);
  }

  if (GUITaskHandle != NULL)
  {
    g_SysMonitor.GUI_FreeStack = uxTaskGetStackHighWaterMark((TaskHandle_t)GUITaskHandle);
  }

  if (defaultTaskHandle != NULL)
  {
    g_SysMonitor.Default_FreeStack = uxTaskGetStackHighWaterMark((TaskHandle_t)defaultTaskHandle);
  }

  // 2. 获取 FreeRTOS 堆内存状态
  g_SysMonitor.FreeHeapSize = xPortGetFreeHeapSize();
  g_SysMonitor.MinEverFreeHeap = xPortGetMinimumEverFreeHeapSize();
}
/* USER CODE END Application */

