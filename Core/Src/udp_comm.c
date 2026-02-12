#include "udp_comm.h"

/*
struct udp_pcb *upcb;
u16_t port;
ip_addr_t hostAddr;

void UDP_Comm_Init(void)
{
	upcb = udp_new();
	port = 7;
	IP_ADDR4(&hostAddr, 192, 168, 0, 111);

	err_t err = udp_bind(upcb, IP_ADDR_ANY, port);

	if(err == ERR_OK)
	{
		udp_recv(upcb, UDP_Comm_Recv_Packet, NULL);
	}
	else
	{
		udp_remove(upcb);
		upcb = NULL;
	}
}

void UDP_Comm_Send_Packet(char* payload, int len)
{
	if (upcb == NULL)
	{
		return;
	}

	struct pbuf *txBuf = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);

    if (txBuf == NULL)
    {
        return;
    }

	pbuf_take(txBuf, payload, len);

	udp_sendto(upcb, txBuf, &hostAddr, port);

	pbuf_free(txBuf);
}

void UDP_Comm_Recv_Packet(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
	char buf[11] = "moneysmart";

	UDP_Comm_Send_Packet(buf, 11);

	pbuf_free(p);
}
*/
