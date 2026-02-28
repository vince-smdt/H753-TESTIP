/* Includes ------------------------------------------------------------------*/
#include "testip.h"
#include "string.h"
#include "stm32h7xx_hal_def.h"
#include "main.h"

/* Private macros ------------------------------------------------------------*/
#define htons(x) __builtin_bswap16(x) 	// Converts unsigned short integer from host byte order (little endian) to network byte order (big endian)
#define ntohs(x) __builtin_bswap16(x) 	// Converts unsigned short integer from network byte order (big endian) to host byte order (little endian)
#define htonl(x) __builtin_bswap32(x)	// Converts unsigned long integer from host byte order (little endian) to network byte order (big endian)
#define ntohl(x) __builtin_bswap32(x)	// Converts unsigned long integer from network byte order (big endian) to host byte order (little endian)

/* Private defines -----------------------------------------------------------*/
#define ETH_MAX_PAYLOAD_LEN		1500
#define ETHERTYPE_IPV4  		0x0800
#define ETHERTYPE_ARP   		0x0806

#define IPV4_MAX_DATA_LEN		1440
#define IPV4_PROTOCOL_ICMP		1
#define IPV4_PROTOCOL_UDP		17

#define ICMP_TYPE_ECHO_REPLY	0
#define ICMP_TYPE_ECHO			8
#define ICMP_CODE_ECHO			0
#define ICMP_ECHO_DATA_LEN		32

#define ARP_HTYPE_ETHERNET		1
#define ARP_PTYPE_IPV4			0x0800
#define ARP_HLEN_ETHERNET		6
#define ARP_PLEN_IPV4			4
#define ARP_OPER_REQUEST		1
#define ARP_OPER_REPLY			2

#define PING_TIMEOUT_MS			3000

/* Private variables ---------------------------------------------------------*/
static uint32_t myIP = MAKE_IPV4_ADDR(192, 168, 0, 100);
static uint16_t myPort = 55555;
static ETH_BufferTypeDef Txbuffer;
static uint8_t TxPoolBufferIdx = 0;
static PingControlBlock activePing;
static uint16_t icmpEchoSeq = 1000;
static char icmpEchoData[ICMP_ECHO_DATA_LEN] = "abcdefghijklmnopqrstuvwxyz data";

static ETH_BufferTypeDef TxEthHdrBuf;
static ETH_Header TxEthHdr __attribute__((section(".TxBuffSection")));

static ETH_BufferTypeDef TxIpv4HdrBuf;
static IPV4_Header TxIpv4Hdr __attribute__((section(".TxBuffSection")));

static ETH_BufferTypeDef TxIcmpEchoHdrBuf;
static ICMP_Echo_Header TxIcmpEchoHdr __attribute__((section(".TxBuffSection")));

static ETH_BufferTypeDef TxIcmpEchoDataBuf;
static uint8_t TxIcmpEchoData[ICMP_ECHO_DATA_LEN] __attribute__((section(".TxBuffSection")));

static ETH_BufferTypeDef TxUdpHdrBuf;
static UDP_Header TxUdpHdr __attribute__((section(".TxBuffSection")));

static ETH_BufferTypeDef TxUdpDataBuf;
static uint8_t TxUdpData[IPV4_MAX_DATA_LEN] __attribute__((section(".TxBuffSection")));

static ETH_BufferTypeDef TxArpBuf;
static ARP_Packet TxArp __attribute__((section(".TxBuffSection")));

/* External variables --------------------------------------------------------*/
extern ETH_TxPacketConfig TxConfig;
extern ETH_HandleTypeDef heth;
extern uint8_t txPool[TX_BUF_CNT][TX_BUF_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void __ProcessIPV4Packet(NetAddr *netAddr, uint8_t *ipv4Buf);
static void __ProcessICMPPacket(NetAddr *netAddr, uint8_t *icmpBuf, uint16_t icmpLen);
static void __ProcessICMPEchoReplyPacket(NetAddr *netAddr, uint8_t *icmpBuf, uint16_t icmpLen);
static void __ProcessICMPEchoPacket(NetAddr *netAddr, uint8_t *icmpBuf, uint16_t icmpLen);
static void __ProcessUDPPacket(NetAddr *netAddr, uint8_t *udpBuf);
static void __ProcessARPPacket(uint8_t *arpBuf);
static HAL_StatusTypeDef __SendARPPacket(uint8_t mac[6], uint32_t ip, uint16_t oper);
static uint8_t* __GetNextTxBuffer();
static uint8_t* __PrepareETHFrame(uint8_t* buf, uint8_t dstMac[6], uint16_t ethertype);
static void __PrepareETHHeaderStruct(uint8_t dst[6], uint16_t ethertype);
static void __PrepareIPV4HeaderStruct(uint16_t dataLen, uint8_t protocol, uint32_t dstIp);
static void __PrepareICMPEchoHeaderStruct();
static void __PrepareUDPHeaderStruct(uint16_t dataLen, uint16_t destPort);
static void __PrepareARPHeaderStruct(uint8_t mac[6], uint32_t ip, uint16_t oper);
static uint8_t* __PrepareIPV4Packet(uint8_t* ipv4Buf, uint16_t dataLen, uint8_t protocol, uint32_t dstIp);
static HAL_StatusTypeDef __SendETHFrame(uint8_t *buf, uint16_t len);

/* Public function definitions -----------------------------------------------*/
void TESTIP_Init() {
	HAL_ETH_Start_IT(&heth);

    memset(&TxConfig, 0, sizeof(TxConfig));
    TxConfig.Attributes   = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
    TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
    TxConfig.CRCPadCtrl   = ETH_CRC_PAD_INSERT;
    // Length set dynamically
    TxConfig.TxBuffer     = &TxEthHdrBuf;

	TxEthHdrBuf.buffer = (uint8_t*) &TxEthHdr;
	TxEthHdrBuf.len = sizeof(TxEthHdr);
	TxEthHdrBuf.next = NULL;
	// dst set dynamically
	memcpy(TxEthHdr.src, heth.Init.MACAddr, 6);
	// ethertype set dynamically

	TxIpv4HdrBuf.buffer = (uint8_t*) &TxIpv4Hdr;
	TxIpv4HdrBuf.len = sizeof(TxIpv4Hdr);
	TxIpv4HdrBuf.next = NULL;
	TxIpv4Hdr.version = 4;
	TxIpv4Hdr.ihl = 5;
	TxIpv4Hdr.dscp = 0;
	TxIpv4Hdr.ecn = 0;
	// len set dynamically
	TxIpv4Hdr.id = 0;
	TxIpv4Hdr.frag = htons(IPV4_DF_FLAG);
	TxIpv4Hdr.ttl = 64;
	// protocol set dynamically
	// checksum set dynamically ??????????????
	TxIpv4Hdr.src = htonl(myIP);
	// dst set dynamically

	TxIcmpEchoHdrBuf.buffer = (uint8_t*) &TxIcmpEchoHdr;
	TxIcmpEchoHdrBuf.len = sizeof(ICMP_Echo_Header);
	TxIcmpEchoHdrBuf.next = &TxIcmpEchoDataBuf;
	TxIcmpEchoHdr.type = ICMP_TYPE_ECHO;
	TxIcmpEchoHdr.code = ICMP_CODE_ECHO;
	// checksum set dynamically ??????????????
	TxIcmpEchoHdr.id = 0;
	// seq set dynamically

	TxIcmpEchoDataBuf.buffer = (uint8_t*) TxIcmpEchoData;
	TxIcmpEchoDataBuf.len = ICMP_ECHO_DATA_LEN;
	TxIcmpEchoDataBuf.next = NULL;
	memcpy(TxIcmpEchoData, icmpEchoData, ICMP_ECHO_DATA_LEN);

	TxUdpHdrBuf.buffer = (uint8_t*) &TxUdpHdr;
	TxUdpHdrBuf.len = sizeof(TxUdpHdr);
	TxUdpHdrBuf.next = &TxUdpDataBuf;
	TxUdpHdr.srcPort = htons(myPort);
	// dstPort set dynamically
	// len set dynamically
	// checksum set dynamically ??????????????

	TxUdpDataBuf.buffer = TxUdpData;
	// len set dynamically
	TxUdpDataBuf.next = NULL;

	TxArpBuf.buffer = (uint8_t*) &TxArp;
	TxArpBuf.len = sizeof(ARP_Packet);
	TxArpBuf.next = NULL;
	TxArp.htype = htons(ARP_HTYPE_ETHERNET);
	TxArp.ptype = htons(ARP_PTYPE_IPV4);
	TxArp.hlen  = ARP_HLEN_ETHERNET;
	TxArp.plen  = ARP_PLEN_IPV4;
	// oper set dynamically
	memcpy(TxArp.sha, heth.Init.MACAddr, 6);
	TxArp.spa = htonl(myIP);
	// tha set dynamically
	// tpa set dynamically


	activePing.state = PING_STATE_IDLE;
}

void TESTIP_Process() {
	if (activePing.state == PING_STATE_PENDING && (HAL_GetTick() - activePing.sentTick) > PING_TIMEOUT_MS) {
		activePing.state = PING_STATE_IDLE;
		TESTIP_PingCallback(activePing.targetIp, PING_RES_TIMEOUT, PING_TIMEOUT_MS);
	}
}

uint8_t* TESTIP_GetDataPtr() {
	return TxUdpData;
}

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

HAL_StatusTypeDef TESTIP_SendUDPPacket(NetAddr *netAddr, uint16_t len) {
	__PrepareETHHeaderStruct(netAddr->mac, ETHERTYPE_IPV4);
	__PrepareIPV4HeaderStruct(sizeof(UDP_Header) + len, IPV4_PROTOCOL_UDP, netAddr->ip);
	__PrepareUDPHeaderStruct(len, netAddr->port);

	TxUdpDataBuf.len = len;
	TxConfig.Length = sizeof(ETH_Header) + sizeof(IPV4_Header) + sizeof(UDP_Header) + len;

	return HAL_ETH_Transmit_IT(&heth, &TxConfig);
}

HAL_StatusTypeDef TESTIP_Ping(NetAddr *netAddr) {
	if (activePing.state == PING_STATE_PENDING) {
		return HAL_BUSY;
	}

	icmpEchoSeq++;

	__PrepareETHHeaderStruct(netAddr->mac, ETHERTYPE_IPV4);
	__PrepareIPV4HeaderStruct(sizeof(ICMP_Echo_Header) + ICMP_ECHO_DATA_LEN, IPV4_PROTOCOL_ICMP, netAddr->ip);
	__PrepareICMPEchoHeaderStruct();

	TxConfig.Length = sizeof(ETH_Header) + sizeof(IPV4_Header) + sizeof(ICMP_Echo_Header) + ICMP_ECHO_DATA_LEN;

	activePing.state = PING_STATE_PENDING;
	activePing.targetIp = netAddr->ip;
	activePing.id = 0;
	activePing.seq = icmpEchoSeq;
	activePing.sentTick = HAL_GetTick();

	return HAL_ETH_Transmit_IT(&heth, &TxConfig);
}

/* Callbacks -----------------------------------------------------------------*/
__weak void TESTIP_UDP_RxCpltCallback(NetAddr *netAddr, uint8_t *payload, uint16_t len) {
	/* Prevent unused argument(s) compilation warning */
	UNUSED(netAddr);
	UNUSED(payload);
	UNUSED(len);
	/* NOTE : This function Should not be modified, when the callback is needed,
	the TESTIP_UDP_RxCpltCallback could be implemented in the user file
	*/
}

__weak void TESTIP_PingCallback(uint32_t ip, PingStatus status, uint32_t rtt_ms) {
	/* Prevent unused argument(s) compilation warning */
	UNUSED(ip);
	UNUSED(status);
	UNUSED(rtt_ms);
	/* NOTE : This function Should not be modified, when the callback is needed,
	the TESTIP_PingCallback could be implemented in the user file
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

	netAddr->ip = ntohl(rxIpv4->src);

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
	case ICMP_TYPE_ECHO_REPLY:
		__ProcessICMPEchoReplyPacket(netAddr, icmpBuf, icmpLen);
		break;

	case ICMP_TYPE_ECHO:
		__ProcessICMPEchoPacket(netAddr, icmpBuf, icmpLen);
		break;

	default:
		break;
	}
}

static inline void __ProcessICMPEchoReplyPacket(NetAddr *netAddr, uint8_t *icmpBuf, uint16_t icmpLen) {
	if (activePing.state != PING_STATE_PENDING) {
		return; // Not expecting echo reply
	}
	if (activePing.targetIp != netAddr->ip) {
		return; // Not addressed to this host
	}

	uint32_t rtt_ms = HAL_GetTick() - activePing.sentTick;
	if (rtt_ms > PING_TIMEOUT_MS) {
		return; // Ping already timed out
	}

	ICMP_Echo_Header *rxIcmp = (ICMP_Echo_Header*) icmpBuf;
	if (activePing.id != rxIcmp->id) {
		return; // Wrong identifier
	}
	if (activePing.seq != rxIcmp->seq) {
		return; // Wrong sequence number
	}

	uint8_t *icmpDataBuf = icmpBuf + sizeof(ICMP_Echo_Header);
	if (memcmp(icmpEchoData, icmpDataBuf, ICMP_ECHO_DATA_LEN)) {
		return; // Wrong data
	}

	activePing.state = PING_STATE_IDLE;
	TESTIP_PingCallback(netAddr->ip, PING_RES_SUCCESS, rtt_ms);
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

	netAddr->port = ntohs(rxUdp->srcPort);

	TESTIP_UDP_RxCpltCallback(netAddr, data, len);
}

static inline void __ProcessARPPacket(uint8_t *arpBuf) {
	ARP_Packet *rxArp = (ARP_Packet*) arpBuf;
	uint32_t tpa = ntohl(rxArp->tpa);
	uint16_t oper = ntohs(rxArp->oper);

	if (tpa != myIP) {
		return; // Not addressed to this host
	}

	if (oper != ARP_OPER_REQUEST) {
		return; // Only process ARP requests
	}

	__SendARPPacket(rxArp->sha, rxArp->spa, ARP_OPER_REPLY);
}

static inline HAL_StatusTypeDef __SendARPPacket(uint8_t mac[6], uint32_t ip, uint16_t oper) {
	__PrepareETHHeaderStruct(mac, ETHERTYPE_ARP);
	__PrepareARPHeaderStruct(mac, ip, oper);

	TxConfig.Length = sizeof(ETH_Header) + sizeof(ARP_Packet);

	return HAL_ETH_Transmit_IT(&heth, &TxConfig);
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

static inline void __PrepareETHHeaderStruct(uint8_t dst[6], uint16_t ethertype) {
	memcpy(TxEthHdr.dst, dst, 6);
	TxEthHdr.ethertype = htons(ethertype);
}

static inline void __PrepareIPV4HeaderStruct(uint16_t dataLen, uint8_t protocol, uint32_t dstIp) {
	TxIpv4Hdr.len = htons(sizeof(IPV4_Header) + dataLen);
	TxIpv4Hdr.protocol = protocol;
	TxIpv4Hdr.checksum = 0;
	TxIpv4Hdr.dst = htonl(dstIp);

	TxEthHdrBuf.next = &TxIpv4HdrBuf;
}

static inline void __PrepareICMPEchoHeaderStruct() {
	TxIcmpEchoHdr.checksum = 0;
	TxIcmpEchoHdr.seq = icmpEchoSeq;

	TxIpv4HdrBuf.next = &TxIcmpEchoHdrBuf;
}

static inline void __PrepareUDPHeaderStruct(uint16_t dataLen, uint16_t destPort) {
	TxUdpHdr.dstPort = htons(destPort);
	TxUdpHdr.len = htons(sizeof(UDP_Header) + dataLen);
	TxUdpHdr.checksum = 0;

	TxUdpDataBuf.len = dataLen;
	TxIpv4HdrBuf.next = &TxUdpHdrBuf;
}

static inline void __PrepareARPHeaderStruct(uint8_t mac[6], uint32_t ip, uint16_t oper) {
	TxArp.oper = htons(oper);
	memcpy(TxArp.tha, mac, 6);
	TxArp.tpa = htonl(ip);

	TxEthHdrBuf.next = &TxArpBuf;
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
	txIpv4->dst = htonl(dstIp);
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
