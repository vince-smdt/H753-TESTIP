/* Includes ------------------------------------------------------------------*/
#include <testip.h>
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
#define ETH_MAX_PAYLOAD_LEN		1500
#define IPV4_MAX_DATA_LEN		1440
#define ETHERTYPE_IPV4  		0x0800
#define ETHERTYPE_ARP   		0x0806
#define IPV4_PROTOCOL_ICMP		1
#define IPV4_PROTOCOL_UDP		17
#define ICMP_TYPE_ECHO_REPLY	0
#define ICMP_TYPE_ECHO			8
#define ICMP_CODE_ECHO			0

/* Private variables ---------------------------------------------------------*/
static uint32_t myIP = MAKE_IPV4_ADDR(192, 168, 0, 100);
static uint16_t myPort = 1;
static ETH_BufferTypeDef Txbuffer;
static uint8_t TxPoolBufferIdx = 0;

/* External variables --------------------------------------------------------*/
extern ETH_TxPacketConfig TxConfig;
extern ETH_HandleTypeDef heth;
extern uint8_t txPool[TX_BUF_CNT][TX_BUF_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void __ProcessIPV4Packet(NetAddr *netAddr, uint8_t *ipv4Buf);
static void __ProcessICMPPacket(NetAddr *netAddr, uint8_t *icmpBuf, uint16_t icmpLen);
static void __ProcessICMPEchoPacket(NetAddr *netAddr, uint8_t *icmpBuf, uint16_t icmpLen);
static void __ProcessUDPPacket(NetAddr *netAddr, uint8_t *udpBuf);
static void __ProcessARPPacket(uint8_t *arpBuf);
static uint8_t* __GetNextTxBuffer();
static uint8_t* __PrepareETHFrame(uint8_t* buf, uint8_t dstMac[6], uint16_t ethertype);
static uint8_t* __PrepareIPV4Packet(uint8_t* ipv4Buf, uint16_t dataLen, uint8_t protocol, uint32_t dstIp);
static HAL_StatusTypeDef __SendETHFrame(uint8_t *buf, uint16_t len);

/* Public function definitions -----------------------------------------------*/
void TESTIP_ProcessETHFrame(uint8_t *frame) {
	ETH_Header *hdr = (ETH_Header*) frame;
	uint16_t ethertype = ntohs(hdr->ethertype);
	uint8_t *payload = frame + sizeof(ETH_Header);

	NetAddr netAddr;
	memcpy(netAddr.mac, hdr->src, 6);

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

HAL_StatusTypeDef TESTIP_SendUDPPacket(NetAddr *netAddr, uint8_t *payload, uint16_t len) {
	uint16_t txUdpLen = sizeof(UDP_Header) + len;
	uint8_t *txBuf = __GetNextTxBuffer();
	uint8_t *txIpv4Buf = __PrepareETHFrame(txBuf, netAddr->mac, ETHERTYPE_IPV4);
	uint8_t *txUdpBuf = __PrepareIPV4Packet(txIpv4Buf, txUdpLen, IPV4_PROTOCOL_UDP, netAddr->ip);
	uint8_t *txUdpData = txUdpBuf + sizeof(UDP_Header);
	uint16_t txBufLen = sizeof(ETH_Header) + sizeof(IPV4_Header) + txUdpLen;

	UDP_Header *txUdp = (UDP_Header*) txUdpBuf;
	txUdp->len = htons(txUdpLen);
	txUdp->srcPort = htons(myPort);
	txUdp->dstPort = netAddr->port;

	memcpy(txUdpData, payload, len);
	return __SendETHFrame(txBuf, txBufLen);
}

/* Callbacks -----------------------------------------------------------------*/
__weak void TESTIP_UDP_RxCpltCallback(NetAddr *netAddr, uint8_t *payload, uint16_t len) {
	/* Prevent unused argument(s) compilation warning */
	UNUSED(heth);
	/* NOTE : This function Should not be modified, when the callback is needed,
	the TESTIP_UDP_RxCpltCallback could be implemented in the user file
	*/
}

/* Private function definitions ----------------------------------------------*/
static inline void __ProcessIPV4Packet(NetAddr *netAddr, uint8_t *ipv4Buf) {
	IPV4_Header *rxIpv4 = (IPV4_Header*) ipv4Buf;

	if (rxIpv4->ihl > 5) {
		return; // Options currently not supported
	}

	uint32_t dstIp = ntohl(rxIpv4->dst);

	if (dstIp != myIP) {
		return; // Not addressed to this host
	}

	uint16_t len = ntohs(rxIpv4->len);

	if (len < sizeof(IPV4_Header)) {
		return; // Packet length is less than the size of the IPv4 header
	}

	if (len > ETH_MAX_PAYLOAD_LEN) {
		return; // Packet length exceeds max ethernet frame payload size
	}

	uint16_t frag = ntohs(rxIpv4->frag);

	if ((frag & IPV4_MF_FLAG) || (frag & IPV4_OFFSET_MASK)) {
		return; // Fragmentation not supported
	}

	netAddr->ip = rxIpv4->src;

	uint8_t* rxDataBuf = ipv4Buf + sizeof(IPV4_Header);
	uint16_t rxDataLen = ntohs(rxIpv4->len) - sizeof(IPV4_Header);

	switch (rxIpv4->protocol) {
	case IPV4_PROTOCOL_ICMP:
		__ProcessICMPPacket(netAddr, rxDataBuf, rxDataLen);
		break;

	case IPV4_PROTOCOL_UDP:
		__ProcessUDPPacket(netAddr, rxDataBuf);
		break;

	default:
		break;
	}
}

static inline void __ProcessICMPPacket(NetAddr *netAddr, uint8_t *icmpBuf, uint16_t icmpLen) {
	ICMP_Header *rxIcmp = (ICMP_Header*) icmpBuf;

	switch (rxIcmp->type) {
	case ICMP_TYPE_ECHO:
		__ProcessICMPEchoPacket(netAddr, icmpBuf, icmpLen);
		break;

	default:
		break;
	}
}

static inline void __ProcessICMPEchoPacket(NetAddr *netAddr, uint8_t *icmpBuf, uint16_t icmpLen) {
	ICMP_Echo_Header *rxIcmpEcho = (ICMP_Echo_Header*) icmpBuf;

	if (rxIcmpEcho->code != 0) {
		return; // Invalid code for echo message
	}

	uint8_t* txBuf = __GetNextTxBuffer();
	uint8_t *txIpv4Buf = __PrepareETHFrame(txBuf, netAddr->mac, ETHERTYPE_IPV4);
	uint8_t *txIcmpBuf = __PrepareIPV4Packet(txIpv4Buf, icmpLen, IPV4_PROTOCOL_ICMP, netAddr->ip);

	memcpy(txIcmpBuf, icmpBuf, icmpLen);

	ICMP_Echo_Header *txIcmpEcho = (ICMP_Echo_Header*) txIcmpBuf;
	txIcmpEcho->type = ICMP_TYPE_ECHO_REPLY;
	txIcmpEcho->checksum = 0;

	uint16_t txBufLen = sizeof(ETH_Header) + sizeof(IPV4_Header) + icmpLen;

	__SendETHFrame((uint8_t*) txBuf, txBufLen);
}

static inline void __ProcessUDPPacket(NetAddr *netAddr, uint8_t *udpBuf) {
	UDP_Header *rxUdp = (UDP_Header*) udpBuf;
	uint16_t len = ntohs(rxUdp->len);

	if (len < sizeof(UDP_Header)) {
		return; // Packet length is less than the size of the UDP header
	}

	if (len > IPV4_MAX_DATA_LEN) {
		return; // Packet length exceeds max IPv4 packet data size
	}

	uint16_t dstPort = ntohs(rxUdp->dstPort);

	if (dstPort != myPort) {
		return; // No application listening on this port
	}

	uint8_t* data = udpBuf + sizeof(UDP_Header);

	netAddr->port = rxUdp->srcPort;

	TESTIP_UDP_RxCpltCallback(netAddr, data, len);
}

static inline void __ProcessARPPacket(uint8_t *arpBuf) {
	ARP_Packet *rxArp = (ARP_Packet*) arpBuf;
	uint32_t tpa = ntohl(rxArp->tpa);
	uint16_t oper = ntohs(rxArp->oper);

	if (tpa != myIP) {
		return; // Not addressed to this host
	}

	if (oper != 1) {
		return; // Only process ARP requests
	}

	// Send ARP reply
	uint8_t* txBuf = __GetNextTxBuffer();
	ARP_Packet *txArp = (ARP_Packet*) __PrepareETHFrame(txBuf, rxArp->sha, ETHERTYPE_ARP);
	txArp->htype = htons(1);
	txArp->ptype = htons(0x0800);
	txArp->hlen  = 6;
	txArp->plen  = 4;
	txArp->oper  = htons(2);
	memcpy(txArp->sha, heth.Init.MACAddr, 6);
	txArp->spa = htonl(myIP);
	memcpy(txArp->tha, rxArp->sha, 6);
	txArp->tpa = rxArp->spa;

	__SendETHFrame(txBuf, sizeof(ETH_Header) + sizeof(ARP_Packet));
}

static inline uint8_t* __GetNextTxBuffer() {
	uint8_t *buf = txPool[TxPoolBufferIdx];
	TxPoolBufferIdx = (TxPoolBufferIdx + 1) % TX_BUF_CNT;
	return buf;
}

static inline uint8_t* __PrepareETHFrame(uint8_t* buf, uint8_t dstMac[6], uint16_t ethertype) {
	ETH_Header *hdr = (ETH_Header*) buf;
	memcpy(hdr->dst, dstMac, 6);
	memcpy(hdr->src, heth.Init.MACAddr, 6);
	hdr->ethertype = htons(ethertype);
	return buf + sizeof(ETH_Header);
}

static inline uint8_t* __PrepareIPV4Packet(uint8_t* ipv4Buf, uint16_t dataLen, uint8_t protocol, uint32_t dstIp) {
	IPV4_Header *txIpv4 = (IPV4_Header*) ipv4Buf;
	txIpv4->version = 4;
	txIpv4->ihl = 5;
	txIpv4->dscp = 0;
	txIpv4->ecn = 0;
	txIpv4->len = htons(sizeof(IPV4_Header) + dataLen);
	txIpv4->id = 0;
	txIpv4->frag = htons(IPV4_DF_FLAG);
	txIpv4->ttl = 64;
	txIpv4->protocol = protocol;
	txIpv4->checksum = 0;
	txIpv4->src = htonl(myIP);
	txIpv4->dst = dstIp;
	return ipv4Buf + sizeof(IPV4_Header);
}

static inline HAL_StatusTypeDef __SendETHFrame(uint8_t *buf, uint16_t len) {
    Txbuffer.buffer = buf;
    Txbuffer.len = len;
    Txbuffer.next = NULL;

    memset(&TxConfig, 0, sizeof(TxConfig));
    TxConfig.Attributes   = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
    TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
    TxConfig.CRCPadCtrl   = ETH_CRC_PAD_INSERT;
    TxConfig.Length       = len;
    TxConfig.TxBuffer     = &Txbuffer;

    return HAL_ETH_Transmit_IT(&heth, &TxConfig);
}
