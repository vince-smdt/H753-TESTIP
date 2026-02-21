/* Includes ------------------------------------------------------------------*/
#include <testip.h>
#include "main.h"

/* External variables --------------------------------------------------------*/
extern uint8_t rxPool[RX_BUF_CNT][RX_BUF_SIZE];

/* Overridden function definitions -------------------------------------------*/
void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
    static uint32_t idx = 0;

    *buff = rxPool[idx];
    idx = (idx + 1) % RX_BUF_CNT;
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
    *pStart = buff;
    *pEnd   = buff + Length;
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth)
{
	void *rx_start;
	if (HAL_ETH_ReadData(heth, &rx_start) == HAL_OK)
	{
	    uint8_t *frame = (uint8_t *)rx_start;
	    TESTIP_ProcessETHFrame(frame);
	}
}
