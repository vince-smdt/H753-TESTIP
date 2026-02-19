/* Includes ------------------------------------------------------------------*/
#include "msip.h"
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
#define ETHERTYPE_IPV4  		0x0800
#define ETHERTYPE_ARP   		0x0806
#define IPV4_PROTOCOL_ICMP		1
#define IPV4_PROTOCOL_UDP		17
#define ICMP_TYPE_ECHO_REPLY	0
#define ICMP_TYPE_ECHO			8
#define ICMP_CODE_ECHO			0

/* Private variables ---------------------------------------------------------*/
static uint32_t ipaddr = MAKE_IPV4_ADDR(192, 168, 0, 100);
static ETH_BufferTypeDef Txbuffer;
static uint8_t TxPoolBufferIdx = 0;

/* External variables --------------------------------------------------------*/
extern ETH_TxPacketConfig TxConfig;
extern ETH_HandleTypeDef heth;
extern uint8_t txPool[TX_BUF_CNT][TX_BUF_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void __ProcessIPV4Packet(NetAddr *netAddr, uint8_t *rxIpv4Ptr);
static void __ProcessICMPPacket(NetAddr *netAddr, uint8_t *rxIcmpPtr, uint16_t rxIcmpLength);
static void __ProcessICMPEchoPacket(NetAddr *netAddr, uint8_t *rxIcmpPtr, uint16_t rxIcmpLength);
static void __ProcessUDPPacket(NetAddr *netAddr, uint8_t *rxUdpPtr);
static void __ProcessARPPacket(uint8_t *rxArpPtr);
static uint8_t* __GetNextTxBuffer();
static uint8_t* __PrepareETHFrame(uint8_t* buffer, uint8_t dst[6], uint16_t ethertype);
static uint8_t* __PrepareIPV4Packet(uint8_t* ipv4Buffer, uint8_t protocol, uint32_t dest, uint16_t dataLength);
static HAL_StatusTypeDef __SendETHFrame(uint8_t *buffer, uint16_t length);

/* Private function definitions ----------------------------------------------*/
void MSIP_ProcessETHFrame(uint8_t *frame) {
	ETH_Header *header = (ETH_Header*) frame;
	uint16_t ethertype = ntohs(header->ethertype);
	uint8_t *payload = frame + sizeof(ETH_Header);

	NetAddr netAddr;
	memcpy(netAddr.mac, header->src, 6);

	static uint32_t packetsReceived = 0;
	packetsReceived++;

	switch (ethertype) {
	case ETHERTYPE_IPV4:
		__ProcessIPV4Packet(&netAddr, payload);
		break;

	case ETHERTYPE_ARP:
		__ProcessARPPacket(payload);
		break;

	default:
		break;
	}
}

HAL_StatusTypeDef MSIP_SendUDPPacket(NetAddr *netAddr, uint8_t *payload, uint16_t len) {
	uint8_t *txBuffer = __GetNextTxBuffer();
	uint8_t *txIpv4 = __PrepareETHFrame(txBuffer, netAddr->mac, ETHERTYPE_IPV4);
	uint8_t *txUdp = __PrepareIPV4Packet(txIpv4, IPV4_PROTOCOL_UDP, netAddr->ip, len);
	uint8_t *txUdpData = txUdp + sizeof(UDP_Header);
	uint16_t ethLen = sizeof(ETH_Header) + sizeof(IPV4_Header) + sizeof(UDP_Header) + len;

	memcpy(txUdpData, payload, len);
	return __SendETHFrame(payload, ethLen);
}

static inline void __ProcessIPV4Packet(NetAddr *netAddr, uint8_t *rxIpv4Ptr) {
	IPV4_Header *rxPacket = (IPV4_Header*) rxIpv4Ptr;
	uint32_t dest = ntohl(rxPacket->dest);

	if (dest != ipaddr) {
		return; // Not addressed to this host
	}

	if (rxPacket->ihl > 5) {
		return; // Options currently not supported
	}

	netAddr->ip = rxPacket->src;

	uint8_t* payload = rxIpv4Ptr + sizeof(IPV4_Header);
	uint16_t payloadLength = ntohs(rxPacket->len) - sizeof(IPV4_Header);

	switch (rxPacket->protocol) {
	case IPV4_PROTOCOL_ICMP:
		__ProcessICMPPacket(netAddr, payload, payloadLength);
		break;

	case IPV4_PROTOCOL_UDP:
		__ProcessUDPPacket(netAddr, payload);
		break;

	default:
		break;
	}
}

static inline void __ProcessICMPPacket(NetAddr *netAddr, uint8_t *rxIcmpPtr, uint16_t rxIcmpLength) {
	ICMP_Header *rxIcmpPacket = (ICMP_Header*) rxIcmpPtr;

	switch (rxIcmpPacket->type) {
	case ICMP_TYPE_ECHO:
		__ProcessICMPEchoPacket(netAddr, rxIcmpPtr, rxIcmpLength);
		break;

	default:
		break;
	}
}

static inline void __ProcessICMPEchoPacket(NetAddr *netAddr, uint8_t *rxIcmpPtr, uint16_t rxIcmpLength) {
	ICMP_Echo_Header *rxIcmpEcho = (ICMP_Echo_Header*) rxIcmpPtr;

	if (rxIcmpEcho->code != 0) {
		return; // Invalid code for echo message
	}

	uint8_t* txBuffer = __GetNextTxBuffer();
	uint8_t *txIpv4Ptr = __PrepareETHFrame(txBuffer, netAddr->mac, htons(ETHERTYPE_IPV4));
	uint8_t *txIcmpPtr = __PrepareIPV4Packet(txIpv4Ptr, IPV4_PROTOCOL_ICMP, netAddr->ip, rxIcmpLength);

	memcpy(txIcmpPtr, rxIcmpPtr, rxIcmpLength);

	ICMP_Echo_Header *txIcmpEcho = (ICMP_Echo_Header*) txIcmpPtr;
	txIcmpEcho->type = ICMP_TYPE_ECHO_REPLY;
	txIcmpEcho->checksum = 0;

	uint16_t txFrameLength = sizeof(ETH_Header) + sizeof(IPV4_Header) + rxIcmpLength;

	__SendETHFrame((uint8_t*) txBuffer, txFrameLength);
}

static inline void __ProcessUDPPacket(NetAddr *netAddr, uint8_t *rxUdpPtr) {
	UDP_Header *rxUdp = (UDP_Header*) rxUdpPtr;
	uint16_t dataLength = ntohs(rxUdp->len);
	// TODO: Call user function with pointer to data and data length
}

static inline void __ProcessARPPacket(uint8_t *rxArpPtr) {
	ARP_Header *rxPacket = (ARP_Header*) rxArpPtr;
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
	ARP_Header *txPacket = (ARP_Header*) __PrepareETHFrame(txBuffer, rxPacket->sha, htons(ETHERTYPE_ARP));
	txPacket->htype = htons(1);
	txPacket->ptype = htons(0x0800);
	txPacket->hlen  = 6;
	txPacket->plen  = 4;
	txPacket->oper  = htons(2);
	memcpy(txPacket->sha, heth.Init.MACAddr, 6);
	txPacket->spa = htonl(ipaddr);
	memcpy(txPacket->tha, rxPacket->sha, 6);
	txPacket->tpa = rxPacket->spa;

	__SendETHFrame(txBuffer, sizeof(ETH_Header) + sizeof(ARP_Header));
}

static inline uint8_t* __GetNextTxBuffer() {
	uint8_t *buffer = txPool[TxPoolBufferIdx];
	TxPoolBufferIdx = (TxPoolBufferIdx + 1) % TX_BUF_CNT;
	return buffer;
}

static inline uint8_t* __PrepareETHFrame(uint8_t* buffer, uint8_t dst[6], uint16_t ethertype) {
	ETH_Header *header = (ETH_Header*) buffer;
	memcpy(header->dst, dst, 6);
	memcpy(header->src, heth.Init.MACAddr, 6);
	header->ethertype = ethertype;
	return buffer + sizeof(ETH_Header);
}

static inline uint8_t* __PrepareIPV4Packet(uint8_t* ipv4Buffer, uint8_t protocol, uint32_t dest, uint16_t dataLength) {
	IPV4_Header *packet = (IPV4_Header*) ipv4Buffer;
	packet->version = 4;
	packet->ihl = 5;
	packet->dscp = 0;
	packet->ecn = 0;
	packet->len = htons(sizeof(IPV4_Header) + dataLength);
	packet->id = 0;
	packet->frag = 0;
	packet->ttl = 64;
	packet->protocol = protocol;
	packet->checksum = 0;
	packet->src = htonl(ipaddr);
	packet->dest = dest;
	return ipv4Buffer + sizeof(IPV4_Header);
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
