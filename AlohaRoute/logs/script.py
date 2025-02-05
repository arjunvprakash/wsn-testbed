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
    print(node, children)
    for i, child in enumerate(children):
        set_positions_v1(child, x + (1 if i % 2 == 0 else -1), y - child)

def set_positions_v3(node, x, y):
    pos1[node] = (x, y)
    print(node, x, y)
    children = list(G1.predecessors(node))
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
    num_children = len(children)    
    if num_children > 0:
        spacing = 5
        for i, child in enumerate(children):
            x_factor = (i - (num_children - 1) / 2)
            child_x = x + x_factor * spacing            
            set_positions_v2(child, child_x, y - 1)   

def set_positions_v4(node, x, y):
    if node not in pos1:
        pos1[node] = (x, y)  # Assign position if not already assigned
    print(f"Node {node}: Position {x}, {y}")
    children = list(G1.predecessors(node))
    num_children = len(children)
    if num_children > 0:
        spacing = 5  # Adjust spacing as needed
        for i, child in enumerate(children):
            x_factor = (i - (num_children - 1) / 2)  # Center children
            child_x = x + x_factor * spacing
            set_positions_v2(child, child_x, y - 1)

      
args = parseArgs()
root_node = int(args.sink)

df = pd.read_csv('/home/pi/sw_workspace/AlohaRoute/Debug/results/network.csv')

### ------------ Data Preparation ------------ ###
df['Timestamp'] = pd.to_datetime(df['Timestamp'], unit='s')
df.sort_values(by='Timestamp', ascending=False, inplace=True)

all_nodes = pd.unique(df[['Source', 'Address', 'Parent']].values.ravel('K'))
all_nodes.sort()
print("all_nodes: ", all_nodes)

### Data extraction for Network Tree
###     Direct parent info
df_p1 = df[(df['State'] != "UNKNOWN") & (df['Role'] == 'PARENT')][['Timestamp', 'State', 'Source', 'Address', 'RSSI']]
###     Direct child info
df_p2 = df[(df['State'] != "UNKNOWN") & (df['Role'] == 'CHILD')][['Timestamp', 'State', 'Source', 'Address', 'RSSI']]
df_p2.columns = ['Timestamp', 'State', 'Address', 'Source', 'RSSI']
### Indirect parent info
df_p3 = df[df['State'] != "UNKNOWN"][['Timestamp', 'State', 'Address', 'Parent', 'ParentRSSI']]
df_p3.columns = ['Timestamp', 'State', 'Source', 'Address', 'RSSI']
###     Combine
df_parent = pd.concat([df_p1, df_p2, df_p3], ignore_index=True)
df_parent.drop_duplicates(subset=['Source',], keep='first', inplace=True)
df_parent.sort_values(by='Source', ascending=True, inplace=True)
###     Edges only for ACTIVE nodes
tree_edges = df_parent[df_parent['State'] == 'ACTIVE']
print("df_parent : \n", df_parent)


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
graph_edges = df_neighbour[df_neighbour['State'] == 'ACTIVE']
df_neighbour = df_neighbour.drop('key', axis=1)
print("df_neighbour: \n", df_neighbour)

### Draw Network Tree
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 8))
if not df_parent.empty:
    #G1 = nx.from_pandas_edgelist(df_parent, 'Source', 'Address', create_using=nx.DiGraph(), edge_attr='RSSI')
    G1 = nx.DiGraph()
    G1.add_nodes_from(all_nodes)
    G1.add_node(root_node) 
    for _, row in tree_edges.iterrows():
        G1.add_edge(row['Source'], row['Address'], RSSI=row['RSSI'])
    pos1 = {}
    set_positions_v2(root_node, 0, 0)
    
    if 0 in G1:
        G1.remove_node(0)
        
    nx.draw(G1, pos1, with_labels=True, node_size=1500, alpha=0.9, arrows=True, ax=ax1)
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
    #G2 = nx.from_pandas_edgelist(df_neighbour, 'Source', 'Address', create_using=nx.Graph(), edge_attr='RSSI')
    G2 = nx.Graph()
    G2.add_nodes_from(all_nodes)
    for _, row in graph_edges.iterrows():
        G2.add_edge(row['Source'], row['Address'], RSSI=row['RSSI'])
    if 0 in G2:
        G2.remove_node(0)

    pos2 = nx.circular_layout(G2)
    nx.draw(G2, pos2, with_labels=True, node_size=1500, alpha=0.96, arrows=False, ax=ax2, edge_color='grey', style=':')
    edge_labels2 = nx.get_edge_attributes(G2, 'RSSI')
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
#plt.show()
plt.close()