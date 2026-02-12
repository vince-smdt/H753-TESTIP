#include "MSIP.h"

#include "string.h"
#include "stm32h7xx_hal_def.h"

static uint8_t ipaddr[4] = {192, 168, 0, 100};
static ETH_BufferTypeDef Txbuffer;

extern ETH_TxPacketConfig TxConfig;
extern ETH_HandleTypeDef heth;
extern uint8_t Tx_Buff[1536]; // TODO: temporary magic number

static void __ProcessARPPacket(uint8_t *payload);
static void __ProcessUnhandledPacket(uint8_t *payload);
static uint8_t* __PrepareETHFrame(uint8_t dst[6], uint8_t src[6], uint16_t ethertype);
static HAL_StatusTypeDef __ETH_SendFrame_IT(uint8_t *buffer, uint16_t length);

// Byte-swapping (endianness reversal)
static inline uint16_t swap16(uint16_t value) {
    return (value << 8) | (value >> 8);
}

void MSIP_ProcessETHFrame(uint8_t *frame) {
	ETH_FrameHeader *header = (ETH_FrameHeader*) frame;
	uint16_t ethertype = swap16(header->ethertype);
	uint8_t *payload = frame + sizeof(ETH_FrameHeader);

	static uint32_t packetsReceived = 0;
	packetsReceived++;

	switch (ethertype) {
	case 0x0806:
		__ProcessARPPacket(payload);
		break;

	default:
		__ProcessUnhandledPacket(payload);
		break;
	}
}

static inline void __ProcessARPPacket(uint8_t *payload) {
	ARP_Packet *rxPacket = (ARP_Packet*) payload;
	uint16_t oper = swap16(rxPacket->oper);

	if (oper != 1) {
		return; // Only handle ARP requests
	}

	ARP_Packet *txPacket = (ARP_Packet*) __PrepareETHFrame(rxPacket->sha, heth.Init.MACAddr, swap16(0x0806));
	txPacket->htype = swap16(1);
	txPacket->ptype = swap16(0x0800);
	txPacket->hlen  = 6;
	txPacket->plen  = 4;
	txPacket->oper  = swap16(2);
	memcpy(txPacket->sha, heth.Init.MACAddr, 6);
	memcpy(txPacket->spa, ipaddr, 4);
	memcpy(txPacket->tha, rxPacket->sha, 6);
	memcpy(txPacket->tpa, rxPacket->spa, 4);

	__ETH_SendFrame_IT((uint8_t*) Tx_Buff, sizeof(ETH_FrameHeader) + sizeof(ARP_Packet));
}

static inline void __ProcessUnhandledPacket(uint8_t *payload) {
	volatile uint8_t a = 2;
}

static inline uint8_t* __PrepareETHFrame(uint8_t dst[6], uint8_t src[6], uint16_t ethertype) {
	ETH_FrameHeader *header = (ETH_FrameHeader*) Tx_Buff;
	memcpy(header->dst, dst, 6);
	memcpy(header->src, src, 6);
	header->ethertype = ethertype;
	return Tx_Buff + sizeof(ETH_FrameHeader);
}

static inline HAL_StatusTypeDef __ETH_SendFrame_IT(uint8_t *buffer, uint16_t length) {
    Txbuffer.buffer = buffer;
    Txbuffer.len = length;
    Txbuffer.next = NULL;

    memset(&TxConfig, 0, sizeof(TxConfig));
    TxConfig.Attributes   = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
    TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
    TxConfig.CRCPadCtrl   = ETH_CRC_PAD_INSERT;
    TxConfig.Length       = length;
    TxConfig.TxBuffer     = &Txbuffer;

    return HAL_ETH_Transmit_IT(&heth, &TxConfig);
}
