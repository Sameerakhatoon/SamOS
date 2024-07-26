#include "port.h"
#include "graph.h"

// Forward declarations.
Graph*     create_new_graph(char* topology_name);
Node*      create_graph_node(Graph* graph, char* node_name);
Interface* node_attach_netdev(Node* node, struct netdev* dev);
void       dump_graph(Graph* graph);
void       dump_node(Node* node);
void       dump_interface(Interface* intf);

Graph* create_new_graph(char* topology_name){
    Graph* graph = calloc(1, sizeof(Graph));
    if(!graph){
        goto out;
    }
    strncpy(graph->topology_name, topology_name, TOPOLOGY_NAME_SIZE);
    graph->topology_name[TOPOLOGY_NAME_SIZE - 1] = '\0';
    init_glthread(&graph->node_list);
out:
    return graph;
}

Node* create_graph_node(Graph* graph, char* node_name){
    Node* node = calloc(1, sizeof(Node));
    if(!node){
        goto out;
    }
    strncpy(node->node_name, node_name, NODE_NAME_SIZE);
    node->node_name[NODE_NAME_SIZE - 1] = '\0';
    init_glthread(&node->graph_glue);
    glthread_add_next(&graph->node_list, &node->graph_glue);
    // ch2: zero the L3 properties (loopback off, no device-type flags).
    init_node_nw_prop(&node->node_nw_prop);
out:
    return node;
}

// One Interface per NIC instead of a Link between two
// Nodes; the MAC is the NIC's, not interface_assign_mac_address's hash.
Interface* node_attach_netdev(Node* node, struct netdev* dev){
    int slot = get_node_intf_available_slot(node);
    Interface* intf = slot < 0 ? 0 : calloc(1, sizeof(Interface));
    if(!intf){
        return 0;
    }
    strncpy(intf->if_name, dev->name, IF_NAME_SIZE);
    intf->if_name[IF_NAME_SIZE - 1] = '\0';
    intf->att_node = node;
    intf->dev      = dev;
    init_intf_nw_prop(&intf->intf_nw_props);
    memcpy(IF_MAC(intf), dev->mac, sizeof(MacAddr));
    IF_MTU(intf) = dev->mtu;          // ch43: the fragment budget
    node->intf[slot] = intf;
    dev->intf = intf;
    return intf;
}

// No neighbour Node or cost; the NIC's interrupt path instead.
void dump_interface(Interface* intf){
    printf("    Local Node : %s, Interface Name : %s, NIC interrupts %s\n",
           intf->att_node->node_name, intf->if_name, intf->dev->irq);
}

void dump_node(Node* node){
    printf("Node Name = %s :\n", node->node_name);
    for(int i = 0; i < MAX_INTF_PER_NODE; i++){
        if(!node->intf[i]){
            break;
        }
        dump_interface(node->intf[i]);
    }
}

void dump_graph(Graph* graph){
    printf("Topology Name = %s\n", graph->topology_name);
    GlThread* cur = 0;
    ITERATE_GLTHREAD_BEGIN(&graph->node_list, cur){
        Node* node = graph_glue_to_node(cur);
        dump_node(node);
    } ITERATE_GLTHREAD_END(&graph->node_list, cur);
}
