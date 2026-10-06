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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "string.h"
#include "stdbool.h"
#include "fonts.h"
#include "ssd1306.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
//USIAMO DEGLI MNEMONICI PER LE PORTE DELLE VARIE PERIFERICHE
#define ECHOPORT GPIOA
#define TRIGGERPORT GPIOC
#define PARA_PORT GPIOB
//CODIFICA DELLA TIPOLOGIA DI RIFIUTO
#define CARTA	1
#define PLASTICA 2
#define VETRO	3
#define ORGANICO  4
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
uint32_t echo_start =0;
uint32_t echo_stop= 0;
uint32_t distance = 0;

uint32_t soglia=17;

uint8_t res = 0;

uint8_t throwing = 0;

uint8_t wait_inf=0;

uint8_t vetroorg=0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
/* USER CODE BEGIN PFP */
//PROTOTIPI DELLE FUNZIONI UTILIZZATE
void avvio_misura ();
void inferenza();
void throw();
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {

	//QUESTA CALLBACK VIENE ESEGUITA OGNI QUAL VOLTA CHE VI è UN INTERRUZIONE DI TIPO EXTI PERCIO'
	//E' NECESSESARIO DISTIGUERE COSA DOVER GESTIRE

	//GESTIONE DELL'ECCIZIONE CAUSATA DA VARIAZIONE SUL PIN RELATIVO AL SEGNALE DI ECHO DEL SENSORE DI PROSSIMITA'
	//NOTA: INIBIAMO TALE ESECUZIONE NEL CASO IN CUI WAIT_INF SIA UGUALE A 1 (CIOE' STIAMO FACENDO UN INFERENZA)
	if (GPIO_Pin == ECHOPIN_Pin && wait_inf==0) {

		//ECHO PROVOCA INTERRUZIONE SIA SU FRONTE DI SALITA CHE DI DISCESA
		//PERCIO' QUANDO VIENE ESEGUITA QUESTA CALLBACK VERIFICARE SE SI TRATTI DI RISE O DI FALL
		if (HAL_GPIO_ReadPin(ECHOPORT, ECHOPIN_Pin) == GPIO_PIN_SET) {

			//SE E' UN FRONTE DI SALITA ALLORA SI PRENDE IL VALORE DI CONTEGGIO INIZIALE DEL TIMER2
			__HAL_TIM_SET_COUNTER(&htim2, 0);
			echo_start = __HAL_TIM_GET_COUNTER (&htim2);
		} else {

			//SE INVECE E' UN FRONTE DI DISCESA ALLORA SI PRENDE IL VALORE DI CONTEGGIO FINALE
			echo_stop = __HAL_TIM_GET_COUNTER (&htim2);

			//CON QUESTI DUE VALORI E' POSSIBILE DETERMINARE LA DISTANZA MEDIANTE LA SEGUENTE FORMULA
			distance = (echo_stop-echo_start)* 0.034/2;

			if (distance >= 30) distance = 30; //PER CORREGGERE L'OVERFLOW METTIAMO UNA SATURAZIONE

			//STAMPA DI DEBUG
			char msg[100] = {'\0'};
			sprintf(msg, "Dist: %ldcm\n", distance);
			HAL_UART_Transmit(&huart1, (uint8_t*)&msg, strlen(msg), 0xFFFF);

			//NEL CASO IN CUI LA DISTANZA SIA MINORE DELLA SOGLIA SIGNIFICA CHE è STATO RILEVATO UN OGGETTO
			if(distance<soglia){

				//PERCIO' SI AVVIA L'INFEREZA E SI PONE WAIT_INF=1 IN MODO DA STOPPARE LA RILEVAZIONE DI ALTRI RIFIUTI
				wait_inf=1;
				inferenza();
			}

		}
	}
	else if(GPIO_Pin == ACK_Pin){
		//COME DETTO SOPRA TALE CALLBACK PUO' ESSERE INVOCATA PER PIU MOTIVI PERCIO' GLI DISCRIMINIAMO
		//IN QUESTO CASO GESTIAMO L'INTERRUZIONE DOVUTA AL ***FRONTE DI DISCESA*** DEL SEGNALE DI ACK
		//IN TALE CASO UNA COMMUTAZIONE 1->0 DEL SEGNALE DI ACK RAPPRESENTA IL TERMINE DELL'INFERENZA

		//ESSENDO TERMINATA L'INFERENZA POSSIAMO LEGGERE IL RISULTATO, OVVERO LA CODIFICA DEL TIPO DI RIFIUTO
		res=0;
		res |= HAL_GPIO_ReadPin(PARA_PORT, bit0_Pin) << 0; // bit 0
		res |= HAL_GPIO_ReadPin(PARA_PORT, bit1_Pin) << 1; // bit 1
		res |= HAL_GPIO_ReadPin(PARA_PORT, bit2_Pin) << 2; // bit 2

		//RIALZIAMO IL SEGNALE DI REQ PER LE PROSSIME RICHIESTA
		HAL_GPIO_WritePin(PARA_PORT,REQ_Pin,GPIO_PIN_SET);

		//STAMPA DI DEBUG
		char msg[100] = {'\0'};
		sprintf(msg, "INFERENZA TERMINATA: %d\n",res);
		HAL_UART_Transmit(&huart1, (uint8_t*)&msg, strlen(msg), 0xFFFF);

		//NOTA ORA LA TIPOLOGIA DI RIFIUTO POSSIAMO GETTARLO NEL CESTINO CORRISPONDENTE.
		throw();
	}
}



//CALLBACK ESEGUITA A SEGUITO DELLO SCADERE DI UN TIMER
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	char msg[100] = {'\0'};

	//ANCHE IN QUESTO CASO LA CALLBACK è ESEGUITO A CAUSA DI VARI TIMER
	//PERCIò è NECESSARIO DISCRIMINARE QUALE TIMER SIA TERMINATO

	//TIMER 3: TIMER DI 3 SECONDI
	//TALE TIMER VIENE USATO IN 2 MODI DIFFERENTI
	//LA PRIMA VOLTA VIENE AVVIATO DOPO CHE IL PIATTO SI SIA PORTATO NELLA POSIZIONE RELATIVA
	//AL CESTINO CORRETTO, PERCIO' è NECESSARIO ATTENDERE 3 SECONDI (GRAZIE A TIM3) PRIMA DI
	//RIPORTARLO NELLA SUA POSIZIONE DI RIPOSO
	//MA POI VIENE RIATTIVATO POICHE TORNATO IN POSIZIONE DI EQUILIBRIO, LE OSCILLAZIONI POTREBBERO
	//FAR SCATTARE IL SENSORE PERCIO' DOPO ALTRI 3 SECONDI RIPORTIAMO WAIT_INF A 0 PER POTER
	//RICOMINCIARE LE MISURAZIONI E RILEVAZIONI DI OGGETTI.
	if (htim->Instance == TIM3) {

		//PERCIO' SE è SCADUTO IL TIMER TRE SIGNIFICA CHE SONO PASSATI 3 SECONDI, SE THROWING E'
		//PARI A 0 SIGNIFICA CHE è LA PRIMA VOLTA CHE SCATTA
		if (!throwing) {
			//STAMPA DI DEBUG
			sprintf(msg, "raddrizzo piatto %d\n", wait_inf);
			HAL_UART_Transmit(&huart1, (uint8_t*)&msg, strlen(msg), 0xFFFF);
			//FERMIAMO IL TIMER
			HAL_TIM_Base_Stop_IT(&htim3);

			//PORTIAMO IN POSIZIONE DI EQUILIBRIO IL PIATTO
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 500);
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 1389);
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 1389);

			//SETTIAMO A 1 TALE VARIABILE IN MODO TALE CHE LA PORSSIMA VOLTA SI VADA ALLO STEP SUCCESSIVO
			throwing = 1;

			//RIATTIVIAMO IL TIMER 3 PER ALTRI 3 SECONDI PER FAR STABILIZZARE IL PIATTO
			HAL_TIM_Base_Start_IT(&htim3);
		} else {
			//ARRIVATI A QUESTO PUNTO IL PIATTO SI è STABILIZZATO PERCIò è POSSIBILE RIATTIVARE LA RILEVAZIONE

			//STOPPIAMO IL TIMER
			HAL_TIM_Base_Stop_IT(&htim3);

			//RESETTIAMO THROWING
			throwing = 0;

			//SBLOCCHIAMO LA RILEVAZIONE
			wait_inf=0;

			//STAMPA DI DEBUG
			sprintf(msg, "sblocco wait %d\n", wait_inf);
			HAL_UART_Transmit(&huart1, (uint8_t*)&msg, strlen(msg), 0xFFFF);

			//SCRITTURA SULLO SCHERMO
			SSD1306_Clear(1);
			SSD1306_GotoXY(0, 0);
			SSD1306_Puts("THROW", &Font_11x18, 1);
			SSD1306_GotoXY(0, 30);
			SSD1306_Puts("SOMETHING", &Font_11x18, 1);
			SSD1306_UpdateScreen(1);

		}
	}
	else if (htim->Instance == TIM4) {
		//TIMER 4: TIMER DI 1 SECONDO, UTILIZZATO PER I RIFIUTI CHE RICHIEDONO CHE IL PIATTO RUOTI ANCHE.
		//PERCIò PRIMA RUOTA, ATTESA DI UN SECONDO E POI SI INCLINA
		//QUINDI QUANDO TALE CALLBACK è ESEGUITA SIGNIFICA CHE è PASSATO IL SECONDO è POSSIAMO INCLINARE IL PIATTO NEL VERSO GIUSTO

		//STOPPO IL TIMER
		HAL_TIM_Base_Stop_IT(&htim4);

		//INCLINIAMO IL PIATTO
		if(vetroorg==1){
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 944);
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 1833);
		}else if(vetroorg==2){
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 1833);
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 944);
		}
		vetroorg=0;

		//FACCIAMO PARTIRE IL TIMER 3 CHE RIPORTERA' IL PIATTO IN POSIZIONE DI RIPOSO
		HAL_TIM_Base_Start_IT(&htim3);
	}
}


void avvio_misura (){
	//PER AVVIARE UNA MISURAZIONE CON IL SENSORE DI PROSSIMITA' è NECESSARIO STIMOLARLO CON
	//UN IMPULSO DI DURATA 10MS
	HAL_GPIO_WritePin(TRIGGERPORT,TRIGGERPIN_Pin,GPIO_PIN_SET);
	HAL_Delay(10);
	HAL_GPIO_WritePin(TRIGGERPORT,TRIGGERPIN_Pin,GPIO_PIN_RESET);
}

void inferenza(){
	//STAMPA DI DEBUG
	char msg[100] = {'\0'};
	sprintf(msg, "OGGETTO RILEVATO->AVVIO INFERENZA\n");
	HAL_UART_Transmit(&huart1, (uint8_t*)&msg, strlen(msg), 0xFFFF);

	//PER RICHIEDERE L'INFERENZA E' NECESSARIO ABBASSARE IL SEGNALE DI REQ
	HAL_GPIO_WritePin(PARA_PORT,REQ_Pin,GPIO_PIN_RESET);

	//STAMPA SU SCHERMO
	SSD1306_Clear(1);
	SSD1306_GotoXY(0, 0);
	SSD1306_Puts("THINKING...", &Font_11x18, 1);
	SSD1306_UpdateScreen(1);
}

void throw(){
	char msg[100] = {'\0'};
	char stampa[16]={'\0'};
	//IN BASE AL RISULTATO DELL'INFERENZA E' POSSIBILE GETTARE IL RIFIUTO NEL CESTINO CORRETTO
	switch(res){
		case CARTA:{
			sprintf(msg, "CARTA\n");
			sprintf(stampa, "CARTA");
			//PER GETTARE LA CARTA, CHE SI TROVA NEL VERSO DELLA POSIZIONE DI RIPOSO
			//INCLINIAMO QUI IL PIATTO
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 500);
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 944);
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 1833);
			//AVVIAMO IL TIMER 3 PER RIPORTARE IL PIATTO IN POSIZIONE DI EQUILIBRIO
			HAL_TIM_Base_Start_IT(&htim3);
			break;
		}
		case PLASTICA:{
			sprintf(msg, "PLAST/MET\n");
			sprintf(stampa, "PLAST/MET");
			//PER GETTARE LA PLASTICA, CHE SI TROVA NEL VERSO DELLA POSIZIONE DI RIPOSO
			//INCLINIAMO QUI IL PIATTO
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 500);
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 1833);
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 944);
			//AVVIAMO IL TIMER 3 PER RIPORTARE IL PIATTO IN POSIZIONE DI EQUILIBRIO
			HAL_TIM_Base_Start_IT(&htim3);
			break;
		}
		case VETRO:{
			sprintf(msg, "VETRO\n");
			sprintf(stampa, "VETRO");
			//PER IL VETRO INVECE E' NECESSARIO PRIMA RUOTARE IL PIATTO
			//POI AVVIARE TIMER 4 CHE DOPO 1 SECONDO INCLINERà IL PIATTO
			//IL QUALE A SUA VOLTA AVVIA TIMER 3 PER RIPORTARLO IN POSIZIONE DI EQUILIBRIO
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1500);
			vetroorg=1;
			HAL_TIM_Base_Start_IT(&htim4);
			break;
		}
		case ORGANICO:{
			sprintf(msg, "ORGANICO\n");
			sprintf(stampa, "ORGANICO");
			//PER L'ORGANICO INVECE E' NECESSARIO PRIMA RUOTARE IL PIATTO
			//POI AVVIARE TIMER 4 CHE DOPO 1 SECONDO INCLINERà IL PIATTO
			//IL QUALE A SUA VOLTA AVVIA TIMER 3 PER RIPORTARLO IN POSIZIONE DI EQUILIBRIO
			__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1500);
			vetroorg=2;
			HAL_TIM_Base_Start_IT(&htim4);
			break;
		}
		default:{
			//NEL CASO IN CUI ARRIVI UNA CODIFICA DIVERSA DALLE PRECEDENTI
			//SI ASSUME UN ERRORE NELL'INFERENZA
			sprintf(msg, "ERRORE INFERENZA\n");
			sprintf(stampa, "ERROR");
			wait_inf=0;
			break;
		}
	}
	//STAMPA DI DEBUG
	HAL_UART_Transmit(&huart1, (uint8_t*)&msg, strlen(msg), 0xFFFF);
	//STAMPA A SCHERMO
	SSD1306_Clear(1);
	SSD1306_GotoXY(0, 0);
	SSD1306_Puts(stampa, &Font_11x18, 1);
	SSD1306_UpdateScreen(1);
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
  MX_TIM2_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_TIM1_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
  //ALZIAMO IL SEGNALE DI REQ, IL QUALE APPUNTO A RIPOSO E' ALTO
  HAL_GPIO_WritePin(PARA_PORT,REQ_Pin,GPIO_PIN_SET);

  //SETTIAMO A 0 IL TIMER 2 USATO PER GESTIRE IL SEGNALE DI ECHO
  // E FACCIAMOLO PARTIRE
  __HAL_TIM_SET_COUNTER(&htim2, 0);
  HAL_TIM_Base_Start(&htim2);

  //FACCIAMO PARTIRE I CANALI 1, 2 E 3 DEL TIM 1
  //NECESSARI PER LA GENERAZIONE DEI SEGNALI PWM PER IL CONTROLLO DEI SERVO MOTORI
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);


  //SCRITTURA SU SCHERMO
  SSD1306_Init(1);
  SSD1306_Init(2);

  SSD1306_Clear(1);
  SSD1306_GotoXY(0, 0);
  SSD1306_Puts("THROW", &Font_11x18, 1);
  SSD1306_GotoXY(0, 30);
  SSD1306_Puts("SOMETHING", &Font_11x18, 1);
  SSD1306_UpdateScreen(1);

  SSD1306_Clear(2);
  SSD1306_GotoXY(0, 30);
  SSD1306_Puts("CONCESTINO", &Font_11x18, 1);
  SSD1306_UpdateScreen(2);


  //POSIZIONE DI RIPOSO DEL PIATTO
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 500);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 1389);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 1389);

  //NECESSARI PER RISOLVERE UN BUG RELATIVO AI DUE TIMER SEGUENTI
  __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);
  __HAL_TIM_CLEAR_FLAG(&htim4, TIM_FLAG_UPDATE);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	//A MENO CHE NON SI STIA ATTENDENDO LA FINE DI
	//UNA INFERENZA EFFETTUIAMO UNA NUOVA MISURA
	if(!wait_inf){
		avvio_misura();
		HAL_Delay(500);
	}

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None\
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1|RCC_PERIPHCLK_I2C1
                              |RCC_PERIPHCLK_TIM1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  PeriphClkInit.Tim1ClockSelection = RCC_TIM1CLK_HCLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x0010020A;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 71;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 19999;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 71;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 9999;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 21599;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 9999;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 7199;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 38400;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(REQ_GPIO_Port, REQ_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TRIGGERPIN_GPIO_Port, TRIGGERPIN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : REQ_Pin */
  GPIO_InitStruct.Pin = REQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(REQ_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : bit0_Pin bit1_Pin bit2_Pin */
  GPIO_InitStruct.Pin = bit0_Pin|bit1_Pin|bit2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : ACK_Pin */
  GPIO_InitStruct.Pin = ACK_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ACK_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : TRIGGERPIN_Pin */
  GPIO_InitStruct.Pin = TRIGGERPIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(TRIGGERPIN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ECHOPIN_Pin */
  GPIO_InitStruct.Pin = ECHOPIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(ECHOPIN_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
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
