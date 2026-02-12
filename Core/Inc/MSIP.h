#ifndef __MSIP_H
#define __MSIP_H

#include "stdint.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_eth.h"

typedef struct __attribute__((packed)) {
	uint8_t dst[6];
	uint8_t src[6];
	uint16_t ethertype;
} ETH_FrameHeader;

typedef struct __attribute__((packed)) {
	uint16_t htype;
	uint16_t ptype;
	uint8_t hlen;
	uint8_t plen;
	uint16_t oper;
	uint8_t sha[6];
	uint8_t spa[4];
	uint8_t tha[6];
	uint8_t tpa[4];
} ARP_Packet;

void MSIP_ProcessETHFrame(uint8_t *frame);

#endif // __MSIP_H
