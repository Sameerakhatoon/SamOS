#ifndef SAMTCPIP_COMM_H
#define SAMTCPIP_COMM_H

// Packet exchange. Upstream: the simulator's wire, one UDP socket per
// Node on 127.0.0.1 and a select() receiver thread (ch4), then a Linux
// TAP device (ch14-16).
//
// The wire is the NIC. send_pkt_out hands the frame to
// the Interface's netdev (net/netdev.h); pkt_receive is what the
// netdev layer calls for each received frame, from net_poll, under
// the net lock that replaces ch17's recursive ingress mutex. No
// sockets, no threads, no aux header, no pcap (ch28).

#include "port.h"
#include "graph.h"

#define MAX_PACKET_BUFFER_SIZE 2048

// Public APIs used by the rest of the stack.
int send_pkt_out(char* pkt, size_t pkt_size, Interface* oif);
// ch6: flood the packet out of every interface of `node` EXCEPT
// `exempted_intf` (typically the interface the frame arrived on,
// so we don't echo it back). Pass NULL to skip nothing.
int send_pkt_flood(Node* node, Interface* exempted_intf, char* pkt, size_t pkt_size);

// Entry point for a received frame on `iif`. Layer 2 takes it from here.
int pkt_receive(Node* node, Interface* iif, char* pkt, size_t pkt_size);

#endif
