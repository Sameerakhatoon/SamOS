#include "port.h"
#include "comm.h"
#include "graph.h"

// comm.c's UDP-loopback wire and receiver thread, and
// tap.c's TAP device, are the netdev layer now. What is left is the
// three entry points the stack calls. Since ch43 the stack builds and
// reads real wire frames itself (network byte order, 14-byte header,
// no FCS), so nothing is converted here.

// Hand-off to the Layer 2 frame receiver - defined in layer2.c.
extern void layer2_frame_recv(Node* node, Interface* iif, char* pkt, size_t pkt_size);

// Send `pkt` out of `oif`: its NIC. The frame is copied before the
// call returns, so callers may free it (they all do).
int send_pkt_out(char* pkt, size_t pkt_size, Interface* oif){
    if(pkt_size > MAX_PACKET_BUFFER_SIZE || !oif->dev){
        return -1;
    }
    return netdev_xmit(oif->dev, pkt, (uint32_t)pkt_size) < 0 ? -1 : 0;
}

int send_pkt_flood(Node* node, Interface* exempted_intf, char* pkt, size_t pkt_size){
    int rc = 0;
    for(int i = 0; i < MAX_INTF_PER_NODE; i++){
        Interface* intf = node->intf[i];
        if(!intf){
            break;
        }
        if(intf == exempted_intf){
            continue;
        }
        int one = send_pkt_out(pkt, pkt_size, intf);
        if(one < 0){
            rc = one;
        }
    }
    return rc;
}

// ch17's recursive ingress mutex is the net lock, which every caller
// holds (net_poll).
int pkt_receive(Node* node, Interface* iif, char* pkt, size_t pkt_size){
    layer2_frame_recv(node, iif, pkt, pkt_size);
    return 0;
}
