/* Includes ------------------------------------------------------------------*/
#include "testip.h"
#include "main.h"

/* External variables --------------------------------------------------------*/
extern uint8_t rxPool[RX_BUF_CNT][RX_BUF_SIZE];
extern uint32_t counterRx;
extern uint32_t cycStart;
extern uint32_t cycEnd;

/* Overridden function definitions -------------------------------------------*/
void HAL_ETH_RxAllocateCallback(uint8_t **buff) {
    static uint32_t idx = 0;

    *buff = rxPool[idx];
    idx = (idx + 1) % RX_BUF_CNT;
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length) {
    *pStart = buff;
    *pEnd   = buff + Length;
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *heth) {
	counterRx++;
	void *rx_start;
	if (HAL_ETH_ReadData(heth, &rx_start) == HAL_OK)
	{
	    uint8_t *frame = (uint8_t *)rx_start;
	    TESTIP_ProcessETHFrame(frame);
	}
}

void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *heth) {
	cycEnd = DWT->CYCCNT;
	uint32_t totCyc = cycEnd - cycStart;
	volatile uint8_t a = 0;
}
