import pandas as pd
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from datetime import datetime
import argparse

def parseArgs():
    parser = argparse.ArgumentParser(description='Generate topology map for WSN from a CSV.')
    parser.add_argument('sink', metavar='sink', type=int, help='address of sink')
    return parser.parse_args()

def set_positions_v1(node, x, y):
    pos1[node] = (x, y)
    children = list(G1.predecessors(node))
    children.sort()
    print(node, children)
    for i, child in enumerate(children):
        set_positions_v1(child, x + (1 if i % 2 == 0 else -1), y - child)

def set_positions_v3(node, x, y):
    pos1[node] = (x, y)
    # print(node, x, y)
    children = list(G1.predecessors(node))
    children.sort()
    num_children = len(children)    
    if num_children > 0:
        spacing = 1
        for i, child in enumerate(children):
            edge_data = G1.get_edge_data(child, node)            
            rssi_value = edge_data['RSSI']
            print(edge_data)
            x_factor = (i - (num_children - 1) / 2)
            child_x = x + x_factor * spacing
            set_positions_v3(child, child_x, y + rssi_value)

def set_positions_v2(node, x, y):
    # print(f"Node {node}: Position {x}, {y}")
    pos1[node] = (x, y)
    children = list(G1.predecessors(node))
    children.sort()
    num_children = len(children)    
    if num_children > 0:
        spacing = 5
        for i, child in enumerate(children):
            x_factor = (i - (num_children - 1) / 2)
            child_x = x + x_factor * spacing            
            set_positions_v2(child, child_x, y - 1)   

def set_positions_v4(node, x, y):
    if node not in pos1:
        pos1[node] = (x, y)
    # print(f"Node {node}: Position {x}, {y}")
    children = list(G1.predecessors(node))
    children.sort()
    num_children = len(children)
    if num_children > 0:
        spacing = 5
        for i, child in enumerate(children):
            x_factor = (i - (num_children - 1) / 2)
            child_x = x + x_factor * spacing
            set_positions_v2(child, child_x, y - 1)

      
args = parseArgs()
root_node = int(args.sink)

df = pd.read_csv('/home/pi/sw_workspace/AlohaRoute/Debug/results/network.csv')

### ------------ Data Preparation ------------ ###
df['Timestamp'] = pd.to_datetime(df['Timestamp'], unit='s')
df.sort_values(by='Timestamp', ascending=False, inplace=True)

node_state = df[['Timestamp', 'Address', 'State']].copy()
node_state.sort_values(by='Timestamp', ascending=False, inplace=True)
node_state.drop_duplicates(subset=['Address',], keep='first', inplace=True, ignore_index=True)
node_state_dict = dict(zip(node_state['Address'], node_state['State']))
inactive_nodes = {node for node, state in node_state_dict.items() if state != 'ACTIVE'}
print(node_state)

### Data extraction for Network Tree
###     Direct parent info
df_p1 = df[(df['State'] != "UNKNOWN") & (df['Role'] == 'PARENT')][['Timestamp', 'State', 'Source', 'Address', 'RSSI']]
###     Direct child info
df_p2 = df[(df['State'] != "UNKNOWN") & (df['Role'] == 'CHILD')][['Timestamp', 'State', 'Source', 'Address', 'RSSI']]
df_p2.columns = ['Timestamp', 'State', 'Address', 'Source', 'RSSI']
###     Indirect parent info
df_p3 = df[df['State'] != "UNKNOWN"][['Timestamp', 'State', 'Address', 'Parent', 'ParentRSSI']]
df_p3.columns = ['Timestamp', 'State', 'Source', 'Address', 'RSSI']
###     Combine
df_parent = pd.concat([df_p1, df_p2, df_p3], ignore_index=True)
df_parent.drop_duplicates(subset=['Source',], keep='first', inplace=True)
df_parent.sort_values(by='Source', ascending=True, inplace=True)


### Data extraction for Network Tree
###     Direct adjacency
df1 = df[df['State'] != "UNKNOWN"][['Timestamp', 'State', 'Source', 'Address', 'RSSI']]
###     Indirect parent info
df2 = df[df['State'] != "UNKNOWN"][['Timestamp', 'State', 'Address', 'Parent', 'ParentRSSI']]
df2.columns = ['Timestamp', 'State', 'Source', 'Address', 'RSSI']
###     Combine
df_neighbour = pd.concat([df1, df2], ignore_index=True)
df_neighbour['key'] = df_neighbour.apply(lambda row: tuple(sorted([row['Source'], row['Address']])), axis=1)
df_neighbour = df_neighbour.sort_values(by='Timestamp', ascending=False)
df_neighbour = df_neighbour.drop_duplicates(subset='key', keep='first')
df_neighbour.sort_values(by='key', ascending=True, inplace=True)
df_neighbour = df_neighbour.drop('key', axis=1)

### Draw Network Tree
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 8))
if not df_parent.empty:
    G1 = nx.from_pandas_edgelist(df_parent, 'Source', 'Address', create_using=nx.DiGraph(), edge_attr='RSSI')
    G1.add_node(root_node)     
    pos1 = {}
    set_positions_v4(root_node, 0, 0)    
    
    if 0 in G1:
        G1.remove_node(0)    

    edges_to_remove = [(u, v) for u, v in G1.edges if u in inactive_nodes or v in inactive_nodes]
    active_edges = [(u, v) for u, v in G1.edges if u not in inactive_nodes and v not in inactive_nodes]
    inactive_edges = [(u, v) for u, v in G1.edges if u in inactive_nodes or v in inactive_nodes]
    G1.remove_edges_from(edges_to_remove)   

    nx.draw_networkx(
        G1, pos=pos1, 
        node_size=1500, ax=ax1,        
        with_labels=True, arrows=True, 
       style='solid'
    )
    
    nx.draw_networkx_edges(
        G1, pos=pos1, edgelist=inactive_edges, alpha=0.2, 
        style='dotted', ax=ax1, arrows=False
    )

    
    edge_labels = nx.get_edge_attributes(G1, 'RSSI')
    nx.draw_networkx_edge_labels(G1, pos1, edge_labels=edge_labels, ax=ax1)
    ax1.set_title('Network Tree')
else:
    G1 = nx.Graph()
    G1.add_node(root_node)
    pos1 = {root_node: (0, 0)}
    nx.draw(G1, pos1, with_labels=True, node_size=1500, alpha=0.9, ax=ax1)
    ax1.set_title('Network Tree')


### Draw Adjacency Graph
if not df_neighbour.empty:
    G2 = nx.from_pandas_edgelist(df_neighbour, 'Source', 'Address', create_using=nx.DiGraph(), edge_attr='RSSI')    
    if 0 in G2:
        G2.remove_node(0)

    pos2 = nx.circular_layout(G2)

    active_edges = [(u, v) for u, v in G2.edges if u not in inactive_nodes and v not in inactive_nodes]
    inactive_edges = [(u, v) for u, v in G2.edges if u in inactive_nodes or v in inactive_nodes]
    
    solid_edges = []  
    dotted_edges = [] 
    inactive_edges = []  

    duplicate_edges = []
    for u, v in G2.edges:
        if (u, v) in G1.edges:
            solid_edges.append((u, v))
            duplicate_edges.append((v,u))
        else:
            if u in inactive_nodes or v in inactive_nodes:
                inactive_edges.append((u, v))  # Inactive edge (dotted, non-labelled)
            else:
                dotted_edges.append((u, v))  # Active edge (dotted, non-directed)


    G2.remove_edges_from(duplicate_edges)
    nx.draw_networkx_nodes(G2, pos2, node_size=1500, ax=ax2)
         
    nx.draw_networkx_edges(
        G2, pos2,
        edgelist=solid_edges,
        ax=ax2,
        style='solid'
    )
    
    nx.draw_networkx_edges(
        G2, pos2,
        edgelist=dotted_edges,
        ax=ax2,
        style='dotted'
    )
    
    nx.draw_networkx_edges(
        G2, pos2,
        edgelist=inactive_edges,
        alpha=0.2,
        ax=ax2,
        style='dotted'
    )    
    
    nx.draw_networkx_labels(G2, pos2, ax=ax2)
    
    edge_labels2 = {edge: G2[edge[0]][edge[1]]['RSSI'] for edge in solid_edges + dotted_edges}
    nx.draw_networkx_edge_labels(G2, pos2, edge_labels=edge_labels2, ax=ax2, label_pos=0.4)
    ax2.set_title('Adjacency Graph')
else:
    G2 = nx.Graph()
    G2.add_node(root_node)
    pos2 = nx.circular_layout(G2)
    nx.draw(G2, pos2, with_labels=True, node_size=1500, alpha=0.96, ax=ax2)
    ax2.set_title('Adjacency Graph')

dt = datetime.now()
timestamp = dt.strftime("%Y-%m-%d %H:%M:%S")
fig.suptitle(f'Topology Map\n{timestamp}')
plt.savefig(f'/home/pi/sw_workspace/AlohaRoute/Debug/results/network_graph_{timestamp}.png')
plt.savefig('/home/pi/sw_workspace/AlohaRoute/Debug/results/network_graph.png')
plt.close()