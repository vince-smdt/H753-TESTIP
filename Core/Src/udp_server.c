#include "udp_server.h"

/*
void UDP_Send_Ze_Data(void)
{
    struct udp_pcb *pcb;
    struct pbuf *p;
    ip_addr_t dest_ip;

    const char msg[] = "Moneysmart101";

    pcb = udp_new();
    if (pcb == NULL) return;

    IP4_ADDR(&dest_ip, 192, 168, 0, 111);

    p = pbuf_alloc(PBUF_TRANSPORT, sizeof(msg), PBUF_RAM);
    if (p == NULL) {
        udp_remove(pcb);
        return;
    }

    memcpy(p->payload, msg, sizeof(msg));

    udp_sendto(pcb, p, &dest_ip, 7);

    pbuf_free(p);
    udp_remove(pcb);
}


void UDP_Server_Init(void)
{
	// UDP Control Block structure
	struct udp_pcb *upcb;
	err_t err;

	// 1. Create a new UDP control block
	upcb = udp_new();

	// 2. Bind the upcb to the local port
	ip_addr_t myIPADDR;
	IP_ADDR4(&myIPADDR, 192, 168, 0, 123);

	err = udp_bind(upcb, &myIPADDR, 7);  // 7 is the server UDP port


	// 3. Set a receive callback for the upcb
	if(err == ERR_OK)
	{
		udp_recv(upcb, udp_receive_callback, NULL);
	}
	else
	{
		udp_remove(upcb);
	}
}

void udp_receive_callback(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
	struct pbuf *txBuf;

	char buf[100];

	int len = sprintf (buf,"Hello %s From UDP SERVER\n", (char*)p->payload);

	// allocate pbuf from RAM
	txBuf = pbuf_alloc(PBUF_TRANSPORT,len, PBUF_RAM);

	// copy the data into the buffer
	pbuf_take(txBuf, buf, len);

	// Connect to the remote client
	udp_connect(upcb, addr, port);

	// Send a Reply to the Client
	udp_send(upcb, txBuf);

	// free the UDP connection, so we can accept new clients
	udp_disconnect(upcb);

	// Free the p_tx buffer
	pbuf_free(txBuf);

	// Free the p buffer
	pbuf_free(p);
}
*/
