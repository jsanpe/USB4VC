
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  ** This notice applies to any and all portions of this file
  * that are not between comment pairs USER CODE BEGIN and
  * USER CODE END. Other portions of this file, whether
  * inserted by the user or by software development tools
  * are owned by their respective copyright owners.
  *
  * COPYRIGHT(c) 2023 STMicroelectronics
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f0xx_hal.h"

/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdlib.h>
#include "delay_us.h"
#include "shared.h"
#include "helpers.h"
#include "quad_encoder.h"
#include "m0110a.h"
#include "lisa_kb.h"
#include "adb.h"

#define PROTOCOL_STATUS_NOT_AVAILABLE 0
#define PROTOCOL_STATUS_ENABLED 1
#define PROTOCOL_STATUS_DISABLED 2
#define PROTOCOL_LOOKUP_SIZE 16

/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
IWDG_HandleTypeDef hiwdg;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim14;
TIM_HandleTypeDef htim16;
TIM_HandleTypeDef htim17;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/*
0.0.1
working beta

0.1.0
20230416
initial release
added watchdog timer
*/

const uint8_t board_id = 3;
const uint8_t version_major = 0;
const uint8_t version_minor = 1;
const uint8_t version_patch = 0;
uint8_t hw_revision = 0;

uint8_t spi_transmit_buf[SPI_BUF_SIZE];
uint8_t spi_recv_buf[SPI_BUF_SIZE];
kb_buf my_kb_buf;
m0110a_cmd_buf my_m0110a_buf;

mouse_buf my_mouse_buf;
mouse_event latest_mouse_event;
uint8_t spi_error_occured;
uint8_t protocol_status_lookup[PROTOCOL_LOOKUP_SIZE];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM16_Init(void);
static void MX_TIM17_Init(void);
static void MX_TIM14_Init(void);
static void MX_IWDG_Init(void);

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (unsigned char *)&ch, 1, 10);
    return ch;
}

int16_t byte_to_int16_t(uint8_t lsb, uint8_t msb)
{
  return (int16_t)((msb << 8) | lsb);
}

void protocol_status_lookup_init(void)
{
  memset(protocol_status_lookup, PROTOCOL_STATUS_NOT_AVAILABLE, PROTOCOL_LOOKUP_SIZE);
  protocol_status_lookup[PROTOCOL_ADB_KB] = PROTOCOL_STATUS_ENABLED;
  protocol_status_lookup[PROTOCOL_ADB_MOUSE] = PROTOCOL_STATUS_ENABLED;
  protocol_status_lookup[PROTOCOL_M0100_MOUSE] = PROTOCOL_STATUS_DISABLED;
  protocol_status_lookup[PROTOCOL_M0110_KB] = PROTOCOL_STATUS_DISABLED;
  protocol_status_lookup[PROTOCOL_LISA_KB] = PROTOCOL_STATUS_DISABLED;
}

uint8_t is_protocol_enabled(uint8_t this_protocol)
{
  if(this_protocol >= PROTOCOL_LOOKUP_SIZE)
    return 0;
  return protocol_status_lookup[this_protocol] == PROTOCOL_STATUS_ENABLED;
}

void handle_protocol_switch(uint8_t spi_byte)
{
  uint8_t index = spi_byte & 0x7f;
  uint8_t onoff = spi_byte & 0x80;

  if(index >= PROTOCOL_LOOKUP_SIZE)
    return;
  // trying to change a protocol that is not available on this board
  if(protocol_status_lookup[index] == PROTOCOL_STATUS_NOT_AVAILABLE)
    return;
  kb_buf_reset(&my_kb_buf);
  mouse_buf_reset(&my_mouse_buf);
  // switching protocol ON
  if(onoff && protocol_status_lookup[index] == PROTOCOL_STATUS_DISABLED)
  {
    switch(index)
    {
      case PROTOCOL_ADB_KB:
        break;

      case PROTOCOL_ADB_MOUSE:
        break;

      case PROTOCOL_M0100_MOUSE:
        quad_enable();
        break;

      case PROTOCOL_M0110_KB:
        m0110a_reset(&my_m0110a_buf);
        break;

      case PROTOCOL_LISA_KB:
        lisa_kb_reset();
        break;
    }
    protocol_status_lookup[index] = PROTOCOL_STATUS_ENABLED;
  }
  // switching protocol OFF
  else if((onoff == 0) && protocol_status_lookup[index] == PROTOCOL_STATUS_ENABLED)
  {
    switch(index)
    {
      case PROTOCOL_ADB_KB:
        break;

      case PROTOCOL_ADB_MOUSE:
        break;

      case PROTOCOL_M0100_MOUSE:
        quad_disable();
        break;

      case PROTOCOL_M0110_KB:
        m0110a_reset(&my_m0110a_buf);
        break;

      case PROTOCOL_LISA_KB:
        lisa_kb_reset();
        break;
    }
    protocol_status_lookup[index] = PROTOCOL_STATUS_DISABLED;
  }
}

void parse_spi_buf(uint8_t* spibuf)
{
  if(spibuf[SPI_BUF_INDEX_MSG_TYPE] == SPI_MOSI_MSG_TYPE_MOUSE_EVENT)
  {
    latest_mouse_event.movement_x = byte_to_int16_t(spibuf[4], spibuf[5]);
    latest_mouse_event.movement_y = 1 * byte_to_int16_t(spibuf[6], spibuf[7]);
    latest_mouse_event.button_left = spibuf[13];
    latest_mouse_event.button_right = spibuf[14];
    latest_mouse_event.button_middle = spibuf[15];
    latest_mouse_event.button_side = spibuf[16];
    latest_mouse_event.button_extra = spibuf[17];
    mouse_buf_add(&my_mouse_buf, &latest_mouse_event);
  }
  else if(spibuf[SPI_BUF_INDEX_MSG_TYPE] == SPI_MOSI_MSG_TYPE_KEYBOARD_EVENT)
  {
    kb_buf_add(&my_kb_buf, spibuf[4], spibuf[6]);
  }
  else if(spibuf[SPI_BUF_INDEX_MSG_TYPE] == SPI_MOSI_MSG_TYPE_INFO_REQUEST)
  {
    memset(spi_transmit_buf, 0, SPI_BUF_SIZE);
    spi_transmit_buf[SPI_BUF_INDEX_MAGIC] = SPI_MISO_MAGIC;
    spi_transmit_buf[SPI_BUF_INDEX_SEQNUM] = spibuf[SPI_BUF_INDEX_SEQNUM];
    spi_transmit_buf[SPI_BUF_INDEX_MSG_TYPE] = SPI_MISO_MSG_TYPE_INFO_REQUEST;
    spi_transmit_buf[3] = board_id;
    spi_transmit_buf[4] = hw_revision;
    spi_transmit_buf[5] = version_major;
    spi_transmit_buf[6] = version_minor;
    spi_transmit_buf[7] = version_patch;
    uint8_t curr_index = 8;
    for (int i = 0; i < PROTOCOL_LOOKUP_SIZE; i++)
    {
      if(protocol_status_lookup[i] == PROTOCOL_STATUS_NOT_AVAILABLE)
        continue;
      else if(protocol_status_lookup[i] == PROTOCOL_STATUS_DISABLED)
        spi_transmit_buf[curr_index] = i;
      else if(protocol_status_lookup[i] == PROTOCOL_STATUS_ENABLED)
        spi_transmit_buf[curr_index] = i | 0x80;
      curr_index++;
    }
  }
  else if(spibuf[SPI_BUF_INDEX_MSG_TYPE] == SPI_MOSI_MSG_TYPE_SET_PROTOCOL)
  {
    for (int i = 3; i < SPI_BUF_SIZE; ++i)
    {
      if(spibuf[i] == 0)
        break;
      handle_protocol_switch(spibuf[i]);
    }
  }
}

volatile uint32_t ACT_LED_off_ts;
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
  HAL_GPIO_WritePin(ACT_LED_GPIO_Port, ACT_LED_Pin, GPIO_PIN_SET);
  spi_error_occured = (spi_recv_buf[0] != SPI_MOSI_MAGIC);
  parse_spi_buf(spi_recv_buf);
  if(spi_recv_buf[SPI_BUF_INDEX_MSG_TYPE] == SPI_MOSI_MSG_TYPE_REQ_ACK)
    HAL_GPIO_WritePin(SLAVE_REQ_GPIO_Port, SLAVE_REQ_Pin, GPIO_PIN_RESET);
  HAL_SPI_TransmitReceive_IT(&hspi1, spi_transmit_buf, spi_recv_buf, SPI_BUF_SIZE);
  ACT_LED_off_ts = micros() + 10000;
}

const char boot_message[] = "USB4VC Protocol Card\nApple Lisa, Mac, and ADB\ndekuNukem 2023";

/*

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

REMEMBER TO ENABLE ARR PRELOAD ON TIMER 16

AND DISABLE INTERRUPT DURING TRANSMISION

!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

Configure GPIO pins : M0110_CLK_Pin

*/
uint8_t m0110a_inquiry_active;
uint32_t m0110a_last_inquiry;
uint8_t buffered_code, buffered_value;

void m0110a_write_1b_from_buf(void)
{
  uint8_t m0100a_byte;
  m0110a_cmd_buf_peek(&my_m0110a_buf, &m0100a_byte);
  m0110a_cmd_buf_pop(&my_m0110a_buf);
  m0110a_write(m0100a_byte);
}

void m0110a_update(void)
{
  static uint8_t m0110a_host_cmd, m0110a_status;

  m0110a_status = m0110a_read_host_cmd(&m0110a_host_cmd, 600);
  if(m0110a_status != M0110A_OK)
    return;

  if(m0110a_host_cmd == 0x10) // inquiry
  {
    m0110a_inquiry_active = 1;
    m0110a_last_inquiry = HAL_GetTick();
  }
  else if(m0110a_host_cmd == 0x14) // instant
  {
    if(m0110a_cmd_buf_is_empty(&my_m0110a_buf) == 0)
      m0110a_write_1b_from_buf();
    else
      m0110a_write(0x7b);
  }
  else if(m0110a_host_cmd == 0x16) // model number
    m0110a_write(0xb);
  else if(m0110a_host_cmd == 0x36) // test
    m0110a_write(0x7d);
  // printf("r%x", m0110a_host_cmd);
}

void m0100a_handle_inquiry(void)
{
  if(READ_SPI_CS() == GPIO_PIN_RESET)
    return;
  // "If no key transition has occurred after 0.25 seconds
  // the keyboard sends back a Null response to let the computer know it's still there"
  if(m0110a_inquiry_active && HAL_GetTick() - m0110a_last_inquiry > 250)
  {
    m0110a_write(0x7b);
    m0110a_inquiry_active = 0;
  }
  else if(m0110a_inquiry_active && m0110a_cmd_buf_is_empty(&my_m0110a_buf) == 0)
  {
    m0110a_write_1b_from_buf();
    m0110a_inquiry_active = 0;
  }
  else if(m0110a_inquiry_active && kb_buf_peek(&my_kb_buf, &buffered_code, &buffered_value) == 0)
  {
    m0110a_cmd_buf_reset(&my_m0110a_buf);
    if(make_m0110a_scancode(buffered_code, buffered_value, &my_m0110a_buf) == M0110A_OK)
    {
      m0110a_write_1b_from_buf();
      m0110a_inquiry_active = 0;
    }
    kb_buf_pop(&my_kb_buf);
  }
}

void lisa_init(lisa_handle* lisa)
{
  lisa->data_port = GPIOA;
  lisa->data_pin = GPIO_PIN_3;
  lisa->pwr_port = GPIOB;
  lisa->pwr_pin = GPIO_PIN_0;
  lisa_buf_init();
}

void run_lisa_kb(void)
{
  if(protocol_status_lookup[PROTOCOL_LISA_KB] == PROTOCOL_STATUS_DISABLED)
    return;
  if(HAL_GPIO_ReadPin(LISA_DET_GPIO_Port, LISA_DET_Pin) == GPIO_PIN_RESET)
    return;
  lisa_kb_update();
  if(kb_buf_peek(&my_kb_buf, &buffered_code, &buffered_value) != 0)
    return;
  lisa_buf_add(buffered_code, buffered_value);
  kb_buf_pop(&my_kb_buf);
}

void run_mac_kb(void)
{
  if(protocol_status_lookup[PROTOCOL_M0110_KB] == PROTOCOL_STATUS_DISABLED)
    return;
  if(HAL_GPIO_ReadPin(M0110_DET_GPIO_Port, M0110_DET_Pin) == GPIO_PIN_RESET)
    return;
  if(HAL_GPIO_ReadPin(M0110_CLK_GPIO_Port, M0110_CLK_Pin) == GPIO_PIN_RESET)
    return;
  m0110a_update();
  m0100a_handle_inquiry();
}

uint8_t int16_to_uint6(int16_t value)
{
  if(abs(value) <= 3)
    return (uint8_t)value;
  value = value >> 1;
  if(value >= 63)
    value = 63;
  if(value <= -63)
    value = -63;
  return (uint8_t)value;
}

void adb_mouse_update(void)
{
  if(mouse_buf_peek(&my_mouse_buf) == NULL)
    return;

  get_consolidated_mouse_event(&my_mouse_buf, &consolidated_mouse_event);
  // mouse buffer now empty

  uint16_t response = 0;
  if(consolidated_mouse_event.button_left == 0)
    response |= 0x8000;
  if(consolidated_mouse_event.button_right == 0)
    response |= 0x80;
  response |= int16_to_uint6(consolidated_mouse_event.movement_x) & 0x7f;
  response |= (int16_to_uint6(consolidated_mouse_event.movement_y) & 0x7f) << 8;
  adb_send_response_16b(response);
}

uint8_t capslock_counter;
void adb_keyboard_update(void)
{
  uint8_t buffered_code, buffered_value, adb_code;
  uint16_t response = 0x8080;
  if(kb_buf_peek(&my_kb_buf, &buffered_code, &buffered_value) == 0)
  {
    if(buffered_code >= EV_TO_ADB_LOOKUP_SIZE)
      goto adb_kb_end;
    adb_code = linux_ev_to_adb_lookup[buffered_code];
    if(adb_code == ADB_KEY_UNKNOWN)
      goto adb_kb_end;
    /*
    adb mac keyboard capslock key seems to behave differently than PC keyboards
    On PC: Capslock down then up = Caps lock on, Capslock down then up again = Caps lock off
    on adb mac: Capslock down = Capslock on, Capslock up = Caps lock off
    looks like mac keyboard uses a locking capslock, so need to convert the PC code to suit
    */
    if(adb_code == ADB_KEY_CAPSLOCK)
    {
      if(buffered_value != 1)
        goto adb_kb_end;
      capslock_counter++;
      buffered_value = capslock_counter % 2;
    }
    if(buffered_value)
      response &= 0x7fff;
    response |= ((uint16_t)adb_code & 0x7f) << 8;
    adb_send_response_16b(response);
    adb_kb_end:
    kb_buf_pop(&my_kb_buf);

    if(buffered_code == KEY_DELETE)
    {
      if(buffered_value == 0)
        adb_kb_reg2 |= 0x4000;
      else
        adb_kb_reg2 &= 0xbfff;
    }
    if(buffered_code == KEY_CAPSLOCK)
    {
      if(buffered_value == 0)
        adb_kb_reg2 |= 0x2000;
      else
        adb_kb_reg2 &= 0xdfff;
    }
    if(buffered_code == KEY_LEFTCTRL || buffered_code == KEY_RIGHTCTRL)
    {
      if(buffered_value == 0)
        adb_kb_reg2 |= 0x800;
      else
        adb_kb_reg2 &= 0xf7ff;
    }
    if(buffered_code == KEY_LEFTSHIFT || buffered_code == KEY_RIGHTSHIFT)
    {
      if(buffered_value == 0)
        adb_kb_reg2 |= 0x400;
      else
        adb_kb_reg2 &= 0xfbff;
    }
    if(buffered_code == KEY_LEFTALT || buffered_code == KEY_RIGHTALT)
    {
      if(buffered_value == 0)
        adb_kb_reg2 |= 0x200;
      else
        adb_kb_reg2 &= 0xfdff;
    }
    if(buffered_code == KEY_LEFTMETA || buffered_code == KEY_RIGHTMETA)
    {
      if(buffered_value == 0)
        adb_kb_reg2 |= 0x100;
      else
        adb_kb_reg2 &= 0xfeff;
    }
    // if(buffered_code == KEY_NUMLOCK)
    // {
    //   if(buffered_value == 0)
    //     adb_kb_reg2 |= 0x80;
    //   else
    //     adb_kb_reg2 &= 0xff7f;
    // }
    // if(buffered_code == KEY_SCROLLLOCK)
    // {
    //   if(buffered_value == 0)
    //     adb_kb_reg2 |= 0x40;
    //   else
    //     adb_kb_reg2 &= 0xffbf;
    // }
  }
}

uint8_t adb_data, adb_status, this_addr, kb_srq, mouse_srq;

void run_adb(void)
{
  if(IS_ADB_HOST_PRESENT() == 0)
    return;

  adb_kb_enabled = is_protocol_enabled(PROTOCOL_ADB_KB);
  adb_mouse_enabled = is_protocol_enabled(PROTOCOL_ADB_MOUSE);

  if(adb_kb_enabled == 0 && adb_mouse_enabled == 0)
  {
    adb_reset();
    return;
  }

  adb_status = adb_recv_cmd(&adb_data);
  adb_rw_in_progress = 0;
  if(adb_status == ADB_LINE_STATUS_RESET)
  {
    adb_reset();
  }
  else if(adb_status != ADB_OK)
  {
    adb_reset();
    return;
  }

  // we are just at the end of a ADB command, check address, assert SRQ if needed
  this_addr = adb_data >> 4;
  if(!kb_buf_is_empty(&my_kb_buf))
  {
    kb_srq = (this_addr != adb_kb_current_addr);
  }
  else if(!mouse_buf_is_empty(&my_mouse_buf))
  {
    mouse_srq = (this_addr != adb_mouse_current_addr);
  }
  else
  {
    kb_srq = 0;
    mouse_srq = 0;
  }

  if((kb_srq && adb_kb_enabled) || (mouse_srq && adb_mouse_enabled))
    send_srq();
  else
    adb_wait_until_change(ADB_DEFAULT_TIMEOUT_US);

  adb_status = parse_adb_cmd(adb_data);
  if(adb_status == ADB_MOUSE_POLL && adb_mouse_enabled)
    adb_mouse_update();
  else if(adb_status == ADB_KB_POLL && adb_kb_enabled)
    adb_keyboard_update();
  else if(adb_status == ADB_KB_POLL_REG2 && adb_kb_enabled)
    adb_send_response_16b(adb_kb_reg2);
  else if(adb_status == ADB_KB_CHANGE_LED)
  {
    // kb reg2 top 13 bits is button status, 1 = released 0 = pressed
    // bottom 3 bits is LED, 1 = off, 0 = on
    memset(spi_transmit_buf, 0, SPI_BUF_SIZE);
    spi_transmit_buf[SPI_BUF_INDEX_MAGIC] = SPI_MISO_MAGIC;
    spi_transmit_buf[SPI_BUF_INDEX_MSG_TYPE] = SPI_MISO_MSG_TYPE_KB_LED_REQUEST;
    if(!(adb_kb_reg2 & 0x4)) // scroll lock LED
      spi_transmit_buf[3] = 1;
    if(!(adb_kb_reg2 & 0x1))  // num lock LED
      spi_transmit_buf[4] = 1;
    if(!(adb_kb_reg2 & 0x2))  // caps lock LED
      spi_transmit_buf[5] = 1;
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  *
  * @retval None
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration----------------------------------------------------------*/

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
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  MX_TIM16_Init();
  MX_TIM17_Init();
  MX_TIM14_Init();
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */
  HAL_GPIO_WritePin(ERR_LED_GPIO_Port, ERR_LED_Pin, GPIO_PIN_SET);
  HAL_IWDG_Refresh(&hiwdg);
  printf("%s\nrev%d v%d.%d.%d\n", boot_message, hw_revision, version_major, version_minor, version_patch);
  delay_us_init(&htim2);
  protocol_status_lookup_init();

  m0110a_cmd_buf_init(&my_m0110a_buf);
  kb_buf_init(&my_kb_buf);
  mouse_buf_init(&my_mouse_buf);
  memset(spi_transmit_buf, 0, SPI_BUF_SIZE);
  HAL_SPI_TransmitReceive_IT(&hspi1, spi_transmit_buf, spi_recv_buf, SPI_BUF_SIZE);

  lisa_init(&lh);
  adb_init(ADB_DATA_GPIO_Port, ADB_DATA_Pin, ADB_PWR_GPIO_Port, ADB_PWR_Pin);
  quad_init(&my_mouse_buf, &htim17, &htim16, &htim14, GPIOA, GPIO_PIN_8, GPIOB, GPIO_PIN_14, GPIOB, GPIO_PIN_15, GPIOB, GPIO_PIN_12);
  if(is_protocol_enabled(PROTOCOL_M0100_MOUSE))
    quad_enable();
  else
    quad_disable();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  HAL_GPIO_WritePin(ERR_LED_GPIO_Port, ERR_LED_Pin, GPIO_PIN_RESET);
	while (1)
  {
    HAL_IWDG_Refresh(&hiwdg);
    HAL_GPIO_WritePin(ERR_LED_GPIO_Port, ERR_LED_Pin, spi_error_occured);
    if(micros() > ACT_LED_off_ts)
      HAL_GPIO_WritePin(ACT_LED_GPIO_Port, ACT_LED_Pin, GPIO_PIN_RESET);
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
    run_adb();
    run_mac_kb();
    run_lisa_kb();
    HAL_SPI_TransmitReceive_IT(&hspi1, spi_transmit_buf, spi_recv_buf, SPI_BUF_SIZE);
  }
  /* USER CODE END 3 */

}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{

  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_PeriphCLKInitTypeDef PeriphClkInit;

    /**Initializes the CPU, AHB and APB busses clocks
    */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Initializes the CPU, AHB and APB busses clocks
    */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI48;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_SYSCLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

    /**Configure the Systick interrupt time
    */
  HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);

    /**Configure the Systick
    */
  HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

  /* SysTick_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* IWDG init function */
static void MX_IWDG_Init(void)
{

  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
  hiwdg.Init.Window = 4095;
  hiwdg.Init.Reload = 4095;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* SPI1 init function */
static void MX_SPI1_Init(void)
{

  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_INPUT;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* TIM2 init function */
static void MX_TIM2_Init(void)
{

  TIM_ClockConfigTypeDef sClockSourceConfig;
  TIM_MasterConfigTypeDef sMasterConfig;

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 47;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* TIM14 init function */
static void MX_TIM14_Init(void)
{

  htim14.Instance = TIM14;
  htim14.Init.Prescaler = 47;
  htim14.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim14.Init.Period = 65535;
  htim14.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim14.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim14) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* TIM16 init function */
static void MX_TIM16_Init(void)
{

  htim16.Instance = TIM16;
  htim16.Init.Prescaler = 47;
  htim16.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim16.Init.Period = 65535;
  htim16.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim16.Init.RepetitionCounter = 0;
  htim16.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim16) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* TIM17 init function */
static void MX_TIM17_Init(void)
{

  htim17.Instance = TIM17;
  htim17.Init.Prescaler = 47;
  htim17.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim17.Init.Period = 10000;
  htim17.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim17.Init.RepetitionCounter = 0;
  htim17.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim17) != HAL_OK)
  {
    _Error_Handler(__FILE__, __LINE__);
  }

}

/* USART1 init function */
static void MX_USART1_UART_Init(void)
{

  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
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
    _Error_Handler(__FILE__, __LINE__);
  }

}

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
static void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct;

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, SLAVE_REQ_Pin|PCARD_BUSY_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LISA_DATA_Pin|QMOUSE_X2_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, M0110_DATA_Pin|M0110_CLK_Pin|QMOUSE_Y2_Pin|QMOUSE_BUTTON_Pin
                          |QMOUSE_X1_Pin|QMOUSE_Y1_Pin|ADB_PWR_Pin|ADB_DATA_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ACT_LED_GPIO_Port, ACT_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ERR_LED_GPIO_Port, ERR_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : SLAVE_REQ_Pin PCARD_BUSY_Pin */
  GPIO_InitStruct.Pin = SLAVE_REQ_Pin|PCARD_BUSY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : LISA_DATA_Pin QMOUSE_X2_Pin */
  GPIO_InitStruct.Pin = LISA_DATA_Pin|QMOUSE_X2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : LISA_DET_Pin M0110_DET_Pin QMOUSE_DET_Pin ADB_DET_Pin */
  GPIO_InitStruct.Pin = LISA_DET_Pin|M0110_DET_Pin|QMOUSE_DET_Pin|ADB_DET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : M0110_DATA_Pin M0110_CLK_Pin QMOUSE_Y2_Pin QMOUSE_BUTTON_Pin
                           QMOUSE_X1_Pin QMOUSE_Y1_Pin ADB_PWR_Pin ADB_DATA_Pin */
  GPIO_InitStruct.Pin = M0110_DATA_Pin|M0110_CLK_Pin|QMOUSE_Y2_Pin|QMOUSE_BUTTON_Pin
                          |QMOUSE_X1_Pin|QMOUSE_Y1_Pin|ADB_PWR_Pin|ADB_DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : ACT_LED_Pin */
  GPIO_InitStruct.Pin = ACT_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ACT_LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : ERR_LED_Pin */
  GPIO_InitStruct.Pin = ERR_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ERR_LED_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  file: The file name as string.
  * @param  line: The line in file as a number.
  * @retval None
  */
void _Error_Handler(char *file, int line)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while(1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
