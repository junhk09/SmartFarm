/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include "cmsis_os.h"

/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* USER CODE BEGIN PM */
/* USER CODE END PM */

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim4;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart6;

osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t BT_TaskHandle;
const osThreadAttr_t BT_Task_attributes = {
  .name = "BT_Task",
  .stack_size = 512 * 4,   /* 256→512 스택 증가 */
  .priority = (osPriority_t) osPriorityHigh,
};
osThreadId_t Sensor_TaskHandle;
const osThreadAttr_t Sensor_Task_attributes = {
  .name = "Sensor_Task",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t Actuator_TaskHandle;
const osThreadAttr_t Actuator_Task_attributes = {
  .name = "Actuator_Task",
  .stack_size = 512 * 4,   /* 256→512 스택 증가 */
  .priority = (osPriority_t) osPriorityLow,
};
osMessageQueueId_t xCommandQueueHandle;
const osMessageQueueAttr_t xCommandQueue_attributes = {
  .name = "xCommandQueue"
};
osMessageQueueId_t xSensorQueueHandle;
const osMessageQueueAttr_t xSensorQueue_attributes = {
  .name = "xSensorQueue"
};

/* USER CODE BEGIN PV */
static uint8_t          bt_rx_byte;
static uint8_t          bt_buf[128];
static uint8_t          bt_proc_buf[128]; /* 처리용 별도 버퍼 */
static uint16_t         bt_len   = 0;
static volatile uint8_t bt_ready = 0;

/* 액추에이터 상태 전역 (led_str 배열 전역으로) */
static const char *led_str[4] = {"OFF","R","G","B"};
/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART6_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM4_Init(void);
void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);
void StartTask04(void *argument);

/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/* 디버그 출력 (USART2 → ST-Link → PC) */
static void dbg(const char *msg)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 100);
}

/* USART6 인터럽트 콜백 — HC-06 BT 수신 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART6) return;

    if (bt_rx_byte == '\n' || bt_len >= 126) {
        bt_buf[bt_len] = '\0';
        bt_len   = 0;
        bt_ready = 1;   /* 수신 완료 플래그 */
    } else if (bt_rx_byte != '\r') {
        bt_buf[bt_len++] = bt_rx_byte;
    }
    HAL_UART_Receive_IT(&huart6, &bt_rx_byte, 1);
}

/* USER CODE END 0 */

int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART2_UART_Init();
  MX_USART6_UART_Init();
  MX_TIM1_Init();
  MX_TIM4_Init();

  /* USER CODE BEGIN 2 */

  /* PWM 시작 */
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);

  /* TIM1 Advanced Timer MOE 활성화 — 없으면 PWM 출력 안 됨 */
  __HAL_TIM_MOE_ENABLE(&htim1);

  /* USART6 인터럽트 우선순위 (FreeRTOS 범위 내: 5~15) */
//  HAL_NVIC_SetPriority(USART6_IRQn, 5, 0);
//  HAL_NVIC_EnableIRQ(USART6_IRQn);


  /* 초기 LED: 초록 (정상) */
  HAL_GPIO_WritePin(GPIOC, LED_R_Pin|LED_B_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, LED_G_Pin,           GPIO_PIN_SET);

  /* HC-06 BT 수신 인터럽트 시작 */
  HAL_UART_Receive_IT(&huart6, &bt_rx_byte, 1);

  dbg("=== SmartFarm STM32 Start ===\r\n");


  /* USER CODE END 2 */

  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* USER CODE END RTOS_TIMERS */

  xCommandQueueHandle = osMessageQueueNew(16, sizeof(uint8_t), &xCommandQueue_attributes);
  xSensorQueueHandle  = osMessageQueueNew(16, sizeof(uint8_t), &xSensorQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* USER CODE END RTOS_QUEUES */

  defaultTaskHandle   = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
  BT_TaskHandle       = osThreadNew(StartTask02,      NULL, &BT_Task_attributes);
  Sensor_TaskHandle   = osThreadNew(StartTask03,      NULL, &Sensor_Task_attributes);
  Actuator_TaskHandle = osThreadNew(StartTask04,      NULL, &Actuator_Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* USER CODE END RTOS_EVENTS */

  osKernelStart();

  while (1)
  {
    /* USER CODE BEGIN 3 */
    /* USER CODE END 3 */
  }
}

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                   |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) Error_Handler();
}

static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  hadc1.Instance                   = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode          = DISABLE;
  hadc1.Init.ContinuousConvMode    = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion       = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  sConfig.Channel      = ADC_CHANNEL_0;
  sConfig.Rank         = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();
}

static void MX_TIM1_Init(void)
{
  TIM_ClockConfigTypeDef         sClockSourceConfig   = {0};
  TIM_MasterConfigTypeDef        sMasterConfig        = {0};
  TIM_OC_InitTypeDef             sConfigOC            = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  htim1.Instance               = TIM1;
  htim1.Init.Prescaler         = 99;
  htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim1.Init.Period            = 65535;
  htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK) Error_Handler();

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK) Error_Handler();
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK) Error_Handler();

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK) Error_Handler();

  sConfigOC.OCMode       = TIM_OCMODE_PWM1;
  sConfigOC.Pulse        = 0;
  sConfigOC.OCPolarity   = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode   = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState  = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK) Error_Handler();

  sBreakDeadTimeConfig.OffStateRunMode  = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel        = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime         = 0;
  sBreakDeadTimeConfig.BreakState       = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity    = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK) Error_Handler();
  HAL_TIM_MspPostInit(&htim1);
}

static void MX_TIM4_Init(void)
{
  TIM_ClockConfigTypeDef  sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig      = {0};
  TIM_OC_InitTypeDef      sConfigOC          = {0};

  htim4.Instance               = TIM4;
  htim4.Init.Prescaler         = 99;
  htim4.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim4.Init.Period            = 999;
  htim4.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK) Error_Handler();

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK) Error_Handler();
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK) Error_Handler();

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK) Error_Handler();

  sConfigOC.OCMode     = TIM_OCMODE_PWM1;
  sConfigOC.Pulse      = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_4) != HAL_OK) Error_Handler();
  HAL_TIM_MspPostInit(&htim4);
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance          = USART2;
  huart2.Init.BaudRate     = 115200;
  huart2.Init.WordLength   = UART_WORDLENGTH_8B;
  huart2.Init.StopBits     = UART_STOPBITS_1;
  huart2.Init.Parity       = UART_PARITY_NONE;
  huart2.Init.Mode         = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

static void MX_USART6_UART_Init(void)
{
  huart6.Instance          = USART6;
  huart6.Init.BaudRate     = 9600;   /* HC-06 기본 Baud Rate */
  huart6.Init.WordLength   = UART_WORDLENGTH_8B;
  huart6.Init.StopBits     = UART_STOPBITS_1;
  huart6.Init.Parity       = UART_PARITY_NONE;
  huart6.Init.Mode         = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK) Error_Handler();
}

static void MX_DMA_Init(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOC, LED_R_Pin|LED_G_Pin|LED_B_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, PUMP_RELAY_Pin|MOTOR_EN_Pin, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin  = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin   = LED_R_Pin|LED_G_Pin|LED_B_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  GPIO_InitStruct.Pin   = LD2_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin   = PUMP_RELAY_Pin|MOTOR_EN_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/* =====================================================
 *  FreeRTOS 태스크
 * ===================================================== */

void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  for (;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/*
 * BT_Task — USART6(HC-06) 수신 → xCommandQueue 전달
 *
 * 서버가 보내는 형식: CMD:LED:R\n  CMD:MOTOR:ON\n
 * 복합 명령:          CMD:BUZZER:ON|CMD:LED:R\n
 *
 * ★ 핵심: strstr로 부분문자열 검색하므로
 *   "CMD:LED:R" 에서 "LED:R" 찾기 가능
 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  char    *token;
  uint8_t  msg_type;

  for (;;)
  {
    /* ── 안전한 플래그 체크 ── */
    taskENTER_CRITICAL();
    uint8_t ready = bt_ready;
    if (ready) {
        memcpy(bt_proc_buf, bt_buf, sizeof(bt_buf));
        bt_ready = 0;
    }
    taskEXIT_CRITICAL();

    if (ready)
    {
      dbg("[BT RX] ");
      dbg((char*)bt_proc_buf);
      dbg("\r\n");

      /* 파이프(|)로 복수 명령 분리 */
      token = strtok((char*)bt_proc_buf, "|");
      while (token != NULL)
      {
        msg_type = 0;

        /* ★ strstr로 부분문자열 검색 — 형식 무관하게 동작 */
        if      (strstr(token, "MOTOR:ON"))   msg_type = 0x01;
        else if (strstr(token, "MOTOR:OFF"))  msg_type = 0x02;
        else if (strstr(token, "BUZZER:ON"))  msg_type = 0x03;
        else if (strstr(token, "BUZZER:OFF")) msg_type = 0x04;
        else if (strstr(token, "LED:OFF"))    msg_type = 0x08; /* OFF 먼저 */
        else if (strstr(token, "LED:R"))      msg_type = 0x05;
        else if (strstr(token, "LED:G"))      msg_type = 0x06;
        else if (strstr(token, "LED:B"))      msg_type = 0x07;
        else if (strstr(token, "PUMP:ON"))    msg_type = 0x09;
        else if (strstr(token, "PUMP:OFF"))   msg_type = 0x0A;
        else if (strstr(token, "STATUS"))     msg_type = 0x0B;

        if (msg_type)
          osMessageQueuePut(xCommandQueueHandle, &msg_type, 0, 0);

        token = strtok(NULL, "|");
      }
    }
    osDelay(10);
  }
  /* USER CODE END StartTask02 */
}

/* Sensor_Task — ADC 가변저항 읽기 → 모터 속도 실시간 조절 */
void StartTask03(void *argument)
{
  /* USER CODE BEGIN StartTask03 */
  uint16_t adc_val = 0;
  uint32_t pulse   = 0;
  uint8_t  spd_msg = 0;

  for (;;)
  {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 5) == HAL_OK)
    {
      adc_val = HAL_ADC_GetValue(&hadc1);
      pulse   = (uint32_t)adc_val * 65535 / 4095;
      __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, pulse);

      spd_msg = (uint8_t)(adc_val * 100 / 4095);
      /* 큐가 꽉 차면 버림 (non-blocking) */
      osMessageQueuePut(xSensorQueueHandle, &spd_msg, 0, 0);
    }
    HAL_ADC_Stop(&hadc1);
    osDelay(100);
  }
  /* USER CODE END StartTask03 */
}

/*
 * Actuator_Task — xCommandQueue 수신 → 액추에이터 제어
 * ★ led_str[] 전역 선언으로 이동 (switch 내 로컬 선언 제거)
 */
void StartTask04(void *argument)
{
  /* USER CODE BEGIN StartTask04 */
  uint8_t cmd;
  uint8_t spd      = 0;
  char    status[128];
  char    dbg_buf[64];

  uint8_t motor_on  = 0;
  uint8_t pump_on   = 0;
  uint8_t buzzer_on = 0;
  uint8_t led_state = 2;   /* 초기: 초록 */

  for (;;)
  {
    if (osMessageQueueGet(xCommandQueueHandle, &cmd, NULL, 100) == osOK)
    {
      switch (cmd)
      {
        case 0x01: /* MOTOR ON */
          motor_on = 1;
          HAL_GPIO_WritePin(GPIOB, MOTOR_EN_Pin, GPIO_PIN_SET);
          dbg("[ACT] Motor ON\r\n");
          break;

        case 0x02: /* MOTOR OFF */
          motor_on = 0;
          HAL_GPIO_WritePin(GPIOB, MOTOR_EN_Pin, GPIO_PIN_RESET);
          __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
          dbg("[ACT] Motor OFF\r\n");
          break;

        case 0x03: /* BUZZER ON */
          buzzer_on = 1;
          __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 500);
          dbg("[ACT] Buzzer ON\r\n");
          break;

        case 0x04: /* BUZZER OFF */
          buzzer_on = 0;
          __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_4, 0);
          dbg("[ACT] Buzzer OFF\r\n");
          break;

        case 0x05: /* LED R */
          led_state = 1;
          HAL_GPIO_WritePin(GPIOC, LED_R_Pin, GPIO_PIN_SET);
          HAL_GPIO_WritePin(GPIOC, LED_G_Pin|LED_B_Pin, GPIO_PIN_RESET);
          dbg("[ACT] LED R\r\n");
          break;

        case 0x06: /* LED G */
          led_state = 2;
          HAL_GPIO_WritePin(GPIOC, LED_G_Pin, GPIO_PIN_SET);
          HAL_GPIO_WritePin(GPIOC, LED_R_Pin|LED_B_Pin, GPIO_PIN_RESET);
          dbg("[ACT] LED G\r\n");
          break;

        case 0x07: /* LED B */
          led_state = 3;
          HAL_GPIO_WritePin(GPIOC, LED_B_Pin, GPIO_PIN_SET);
          HAL_GPIO_WritePin(GPIOC, LED_R_Pin|LED_G_Pin, GPIO_PIN_RESET);
          dbg("[ACT] LED B\r\n");
          break;

        case 0x08: /* LED OFF */
          led_state = 0;
          HAL_GPIO_WritePin(GPIOC,
              LED_R_Pin|LED_G_Pin|LED_B_Pin, GPIO_PIN_RESET);
          dbg("[ACT] LED OFF\r\n");
          break;

        case 0x09: /* PUMP ON */
          pump_on = 1;
          HAL_GPIO_WritePin(GPIOB, PUMP_RELAY_Pin, GPIO_PIN_SET);
          dbg("[ACT] Pump ON\r\n");
          break;

        case 0x0A: /* PUMP OFF */
          pump_on = 0;
          HAL_GPIO_WritePin(GPIOB, PUMP_RELAY_Pin, GPIO_PIN_RESET);
          dbg("[ACT] Pump OFF\r\n");
          break;

        case 0x0B: /* STATUS */
          dbg("[ACT] Status request\r\n");
          break;

        default:
          snprintf(dbg_buf, sizeof(dbg_buf),
                   "[ACT] Unknown cmd: 0x%02X\r\n", cmd);
          dbg(dbg_buf);
          break;
      }

      /* 최신 속도값 가져오기 (없으면 이전값 유지) */
      uint8_t tmp_spd = 0;
      if (osMessageQueueGet(xSensorQueueHandle, &tmp_spd, NULL, 0) == osOK)
          spd = tmp_spd;

      /* 상태 라즈베리파이로 전송 */
      snprintf(status, sizeof(status),
               "STATUS:MOTOR:%s:SPD:%u:PUMP:%s:LED:%s:BUZZ:%s\n",
               motor_on  ? "ON"  : "OFF",
               spd,
               pump_on   ? "ON"  : "OFF",
               led_str[led_state & 3],
               buzzer_on ? "ON"  : "OFF");
      HAL_UART_Transmit(&huart6, (uint8_t*)status, strlen(status), 200);
      dbg("[STATUS] "); dbg(status);
    }
  }
  /* USER CODE END StartTask04 */
}

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1) {}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif
