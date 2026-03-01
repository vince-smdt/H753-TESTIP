/* Includes ------------------------------------------------------------------*/
#include "testip.h"
#include "main.h"

/* External variables --------------------------------------------------------*/
extern uint8_t rxPool[RX_BUF_CNT][RX_BUF_SIZE];
extern uint32_t counterRx;

extern uint8_t* rxQueue[RX_BUF_CNT];
extern uint8_t rxQueueWriteIdx;
extern uint8_t rxQueueReadIdx;
extern uint8_t rxQueueSize;

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

	/*void *rx_start;
	if (HAL_ETH_ReadData(heth, &rx_start) == HAL_OK)
	{
	    uint8_t *frame = (uint8_t *)rx_start;
	    TESTIP_ProcessETHFrame(frame);
	}*/

	uint8_t **rxBufPtr = &rxQueue[rxQueueWriteIdx];
	if (HAL_ETH_ReadData(heth, (void**)rxBufPtr) == HAL_OK) {
		rxQueueWriteIdx = (rxQueueWriteIdx + 1) % RX_BUF_CNT;
		rxQueueSize++;
	}

	if (rxQueueSize > RX_BUF_CNT) {
		volatile uint8_t a = 0; // This is bad
	}
}
