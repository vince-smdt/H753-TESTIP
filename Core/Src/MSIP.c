#include "MSIP.h"

/* Private includes ----------------------------------------------------------*/
#include "string.h"
#include "stm32h7xx_hal_def.h"
#include "main.h"

/* Private macros ------------------------------------------------------------*/
#define htons(x) __builtin_bswap16(x) 	// Converts unsigned short integer from host byte order (little endian) to network byte order (big endian)
#define ntohs(x) __builtin_bswap16(x) 	// Converts unsigned short integer from network byte order (big endian) to host byte order (little endian)
#define htonl(x) __builtin_bswap32(x)	// Converts unsigned long integer from host byte order (little endian) to network byte order (big endian)
#define ntohl(x) __builtin_bswap32(x)	// Converts unsigned long integer from network byte order (big endian) to host byte order (little endian)
#define MAKE_IPV4_ADDR(b1, b2, b3, b4) ((uint32_t)(b4) | ((uint32_t)(b3) << 8) | ((uint32_t)(b2) << 16) | ((uint32_t)(b1) << 24))

/* Private defines -----------------------------------------------------------*/
#define ETHERTYPE_IPV4  	0x0800
#define ETHERTYPE_ARP   	0x0806
#define IPV4_PROTOCOL_ICMP	1
#define ICMP_TYPE_ECHO		8
#define ICMP_CODE_ECHO		0

/* Private variables ---------------------------------------------------------*/
static uint32_t ipaddr = MAKE_IPV4_ADDR(192, 168, 0, 100);
static ETH_BufferTypeDef Txbuffer;
static uint8_t TxPoolBufferIdx = 0;

/* External variables --------------------------------------------------------*/
extern ETH_TxPacketConfig TxConfig;
extern ETH_HandleTypeDef heth;
extern uint8_t txPool[TX_BUF_CNT][TX_BUF_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void __ProcessIPV4Packet(uint8_t *packet);
static void __ProcessICMPPacket(uint8_t *ipv4Packet, uint8_t *icmpPacket);
static void __ProcessICMPEchoPacket(uint8_t *ipv4Packet, uint8_t *icmpPacket);
static void __ProcessUnhandledICMPPacket(uint8_t *ipv4Packet, uint8_t *icmpPacket);
static void __ProcessUnhandledIPV4Packet(uint8_t *packet);
static void __ProcessARPPacket(uint8_t *packet);
static void __ProcessUnhandledPacket(uint8_t *packet);
static uint8_t* __GetNextTxBuffer();
static uint8_t* __PrepareETHFrame(uint8_t* txBuffer, uint8_t dst[6], uint16_t ethertype);
static HAL_StatusTypeDef __SendETHFrame(uint8_t *buffer, uint16_t length);

/* Private function definitions ----------------------------------------------*/
void MSIP_ProcessETHFrame(uint8_t *frame) {
	ETH_FrameHeader *header = (ETH_FrameHeader*) frame;
	uint16_t ethertype = ntohs(header->ethertype);
	uint8_t *payload = frame + sizeof(ETH_FrameHeader);

	static uint32_t packetsReceived = 0;
	packetsReceived++;

	switch (ethertype) {
	case ETHERTYPE_IPV4:
		__ProcessIPV4Packet(payload);
		break;

	case ETHERTYPE_ARP:
		__ProcessARPPacket(payload);
		break;

	default:
		__ProcessUnhandledPacket(payload);
		break;
	}
}

static inline void __ProcessIPV4Packet(uint8_t *packet) {
	IPV4_Packet *rxPacket = (IPV4_Packet*) packet;
	uint32_t dest = ntohl(rxPacket->dest);

	if (dest != ipaddr) {
		return; // Not addressed to this host
	}

	if (rxPacket->ihl > 5) {
		return; // Options currently not supported
	}

	uint8_t* payload = packet + sizeof(IPV4_Packet);

	switch (rxPacket->protocol) {
	case IPV4_PROTOCOL_ICMP:
		__ProcessICMPPacket(packet, payload);
		break;

	default:
		__ProcessUnhandledIPV4Packet(packet);
		break;
	}
}

static inline void __ProcessICMPPacket(uint8_t *ipv4Packet, uint8_t *icmpPacket) {
	ICMP_Packet *rxIcmpPacket = (ICMP_Packet*) icmpPacket;

	switch (rxIcmpPacket->type) {
	case ICMP_TYPE_ECHO:
		__ProcessICMPEchoPacket(ipv4Packet, icmpPacket);
		break;

	default:
		__ProcessUnhandledICMPPacket(ipv4Packet, icmpPacket);
		break;
	}
}

static inline void __ProcessICMPEchoPacket(uint8_t *ipv4Packet, uint8_t *icmpPacket) {
	ICMP_Echo_Packet *rxIcmpEchoPacket = (ICMP_Echo_Packet*) icmpPacket;

	if (rxIcmpEchoPacket->code != 0) {
		return; // Invalid code for echo message
	}
}

static inline void __ProcessUnhandledICMPPacket(uint8_t *ipv4Packet, uint8_t *icmpPacket) {
	volatile uint8_t a = 0;
}

static inline void __ProcessUnhandledIPV4Packet(uint8_t *packet) {
	volatile uint8_t a = 0;
}

static inline void __ProcessARPPacket(uint8_t *packet) {
	ARP_Packet *rxPacket = (ARP_Packet*) packet;
	uint32_t tpa = ntohl(rxPacket->tpa);
	uint16_t oper = ntohs(rxPacket->oper);

	if (tpa != ipaddr) {
		return; // Not addressed to this host
	}

	if (oper != 1) {
		return; // Only process ARP requests
	}

	// Send ARP reply
	uint8_t* txBuffer = __GetNextTxBuffer();

	ARP_Packet *txPacket = (ARP_Packet*) __PrepareETHFrame(txBuffer, rxPacket->sha, htons(ETHERTYPE_ARP));
	txPacket->htype = htons(1);
	txPacket->ptype = htons(0x0800);
	txPacket->hlen  = 6;
	txPacket->plen  = 4;
	txPacket->oper  = htons(2);
	memcpy(txPacket->sha, heth.Init.MACAddr, 6);
	txPacket->spa = htonl(ipaddr);
	memcpy(txPacket->tha, rxPacket->sha, 6);
	txPacket->tpa = rxPacket->spa;

	__SendETHFrame(txBuffer, sizeof(ETH_FrameHeader) + sizeof(ARP_Packet));
}

static inline void __ProcessUnhandledPacket(uint8_t *packet) {
	volatile uint8_t a = 0;
}

static inline uint8_t* __GetNextTxBuffer() {
	uint8_t *buffer = txPool[TxPoolBufferIdx];
	TxPoolBufferIdx = (TxPoolBufferIdx + 1) % TX_BUF_CNT;
	return buffer;
}

static inline uint8_t* __PrepareETHFrame(uint8_t* txBuffer, uint8_t dst[6], uint16_t ethertype) {
	ETH_FrameHeader *header = (ETH_FrameHeader*) txBuffer;
	memcpy(header->dst, dst, 6);
	memcpy(header->src, heth.Init.MACAddr, 6);
	header->ethertype = ethertype;
	return txBuffer + sizeof(ETH_FrameHeader);
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
