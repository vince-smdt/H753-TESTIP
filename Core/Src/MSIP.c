#include "MSIP.h"

/* Private includes ----------------------------------------------------------*/
#include "string.h"
#include "stm32h7xx_hal_def.h"
#include "main.h"

/* Private macros ------------------------------------------------------------*/
#define htons(x) __builtin_bswap16(x) // Converts unsigned short integer from host byte order (little endian) to network byte order (big endian)
#define ntohs(x) __builtin_bswap16(x) // Converts unsigned short integer from network byte order (big endian) to host byte order (little endian)

/* Private defines -----------------------------------------------------------*/
#define ETHERTYPE_IPv4  0x0800
#define ETHERTYPE_ARP   0x0806

/* Private variables ---------------------------------------------------------*/
static uint8_t ipaddr[4] = {192, 168, 0, 100};
static ETH_BufferTypeDef Txbuffer;

/* External variables --------------------------------------------------------*/
extern ETH_TxPacketConfig TxConfig;
extern ETH_HandleTypeDef heth;
extern uint8_t Tx_Buff[TX_BUF_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void __ProcessIPv4Packet(uint8_t *payload);
static void __ProcessARPPacket(uint8_t *payload);
static void __ProcessUnhandledPacket(uint8_t *payload);
static uint8_t* __PrepareETHFrame(uint8_t dst[6], uint8_t src[6], uint16_t ethertype);
static HAL_StatusTypeDef __SendETHFrame(uint8_t *buffer, uint16_t length);

/* Private function definitions  ---------------------------------------------*/
void MSIP_ProcessETHFrame(uint8_t *frame) {
	ETH_FrameHeader *header = (ETH_FrameHeader*) frame;
	uint16_t ethertype = ntohs(header->ethertype);
	uint8_t *payload = frame + sizeof(ETH_FrameHeader);

	static uint32_t packetsReceived = 0;
	packetsReceived++;

	switch (ethertype) {
	case ETHERTYPE_IPv4:
		__ProcessIPv4Packet(payload);
		break;

	case ETHERTYPE_ARP:
		__ProcessARPPacket(payload);
		break;

	default:
		__ProcessUnhandledPacket(payload);
		break;
	}
}

static inline void __ProcessIPv4Packet(uint8_t *payload) {
	IPv4_Packet *rxPacket = (IPv4_Packet*) payload;
}

static inline void __ProcessARPPacket(uint8_t *payload) {
	ARP_Packet *rxPacket = (ARP_Packet*) payload;
	uint16_t oper = ntohs(rxPacket->oper);

	if (oper != 1) {
		return; // Only process ARP requests
	}

	ARP_Packet *txPacket = (ARP_Packet*) __PrepareETHFrame(rxPacket->sha, heth.Init.MACAddr, htons(ETHERTYPE_ARP));
	txPacket->htype = htons(1);
	txPacket->ptype = htons(0x0800);
	txPacket->hlen  = 6;
	txPacket->plen  = 4;
	txPacket->oper  = htons(2);
	memcpy(txPacket->sha, heth.Init.MACAddr, 6);
	memcpy(txPacket->spa, ipaddr, 4);
	memcpy(txPacket->tha, rxPacket->sha, 6);
	memcpy(txPacket->tpa, rxPacket->spa, 4);

	__SendETHFrame((uint8_t*) Tx_Buff, sizeof(ETH_FrameHeader) + sizeof(ARP_Packet));
}

static inline void __ProcessUnhandledPacket(uint8_t *payload) {
	volatile uint8_t a = 0;
}

static inline uint8_t* __PrepareETHFrame(uint8_t dst[6], uint8_t src[6], uint16_t ethertype) {
	ETH_FrameHeader *header = (ETH_FrameHeader*) Tx_Buff;
	memcpy(header->dst, dst, 6);
	memcpy(header->src, src, 6);
	header->ethertype = ethertype;
	return Tx_Buff + sizeof(ETH_FrameHeader);
}

static inline HAL_StatusTypeDef __SendETHFrame(uint8_t *buffer, uint16_t length) {
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
