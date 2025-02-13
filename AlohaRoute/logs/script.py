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


def draw_edges_from_dataframe(df, G, pos, ax, directed=False):
    for _, row in df.iterrows():
        source, address = row['Source'], row['Address']
        edge_style = row['edge_style']
        edge_label = row['edge_label']
        alpha = row['edge_alpha']

        nx.draw_networkx_nodes(G, pos, ax=ax, node_size=1500)
        nx.draw_networkx_labels(G1, pos, ax=ax)
    
        nx.draw_networkx_edges(
            G, pos=pos, edgelist=[(source, address)], style=edge_style,
            ax=ax, alpha=alpha, arrows=directed
        )
    
        if edge_label is not None:
            nx.draw_networkx_edge_labels(
                G, pos=pos, edge_labels={(source, address): edge_label},
                ax=ax, label_pos=0.4
            )

def get_edge_style(row, directed=False):
    source, address = row['Source'], row['Address']    
    is_inactive = source in inactive_nodes or address in inactive_nodes
    row['edge_label'] = '' if is_inactive else str(row['RSSI'])
    row['edge_alpha'] = 0.2 if is_inactive else 1.0     
    if directed:
        row['edge_style'] = 'dotted' if is_inactive else 'solid'           
    else:
        row['edge_style'] = 'dotted'
    return row
 
      
args = parseArgs()
root_node = int(args.sink)

df = pd.read_csv('network.csv')

### ------------ Data Preparation ------------ ###
df['Timestamp'] = pd.to_datetime(df['Timestamp'], unit='s')
df.sort_values(by='Timestamp', ascending=False, inplace=True)

node_state = df[['Timestamp', 'Address', 'State']].copy()
node_state.sort_values(by='Timestamp', ascending=False, inplace=True)
node_state.drop_duplicates(subset=['Address',], keep='first', inplace=True, ignore_index=True)
node_state_dict = dict(zip(node_state['Address'], node_state['State']))
inactive_nodes = {node for node, state in node_state_dict.items() if state != 'ACTIVE'}
# print("node_state:", node_state_dict)
# print("inactive_nodes:", inactive_nodes)

### Data extraction for Network Tree
###     Direct parent info
df_p1 = df[(df['State'] != "UNKNOWN") & (df['Role'] == 'PARENT')][['Timestamp','Source', 'Address', 'RSSI']]
###     Direct child info
df_p2 = df[(df['State'] != "UNKNOWN") & (df['Role'] == 'CHILD')][['Timestamp','Source', 'Address', 'RSSI']].rename(columns={'Source': 'Address', 'Address': 'Source'})
###     Indirect parent info
df_p3 = df[df['State'] != "UNKNOWN"][['Timestamp','Address', 'Parent', 'ParentRSSI']].rename(columns={'Address': 'Source', 'Parent': 'Address', 'ParentRSSI': 'RSSI'})
###     Combine
df_parent = pd.concat([df_p1, df_p2, df_p3], ignore_index=True)
df_parent = df_parent[df_parent['Address'] > 0]
df_parent = df_parent.sort_values('Timestamp', ascending=False).drop_duplicates('Source', keep='first')
df_parent['key'] = df_parent.apply(lambda row: tuple(sorted([row['Source'], row['Address']])), axis=1)
df_parent = df_parent.sort_values(by='Timestamp', ascending=False)
df_parent = df_parent.drop_duplicates(subset='key', keep='first')
df_parent.sort_values(by='key', ascending=True, inplace=True)
df_parent = df_parent.apply(get_edge_style, axis=1, directed=True)


### Data preparation for adjacency graph
###     Direct adjacency
df1 = df[df['State'] != "UNKNOWN"][['Timestamp','Source', 'Address', 'RSSI']]
df1['key'] = df1.apply(lambda row: tuple(sorted([row['Source'], row['Address']])), axis=1)
df1 = df1.apply(get_edge_style, axis=1, directed=False)
###     Combine with parent info
df_neighbour = pd.concat([df_parent, df1], ignore_index=False).drop_duplicates(subset='key', keep='first').sort_values(by='key')

parent_rssi_map = df_parent.set_index('key')['RSSI'].to_dict()
df_neighbour['RSSI'] = df_neighbour.apply(
    lambda row: parent_rssi_map.get(row['key'], row['RSSI']), axis=1
)

### Draw Network Tree
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 8))
if not df_parent.empty:
    G1 = nx.from_pandas_edgelist(df_parent, 'Source', 'Address', create_using=nx.DiGraph(), edge_attr='RSSI')
    G1.add_node(root_node)     
    pos1 = {}
    set_positions_v2(root_node, 0, 0)   
    draw_edges_from_dataframe(df_parent, G1, pos1, ax1, directed=True)
    ax1.set_title('Network Tree')
else:
    nx.draw(nx.Graph(), {root_node: (0, 0)}, with_labels=True, node_size=1500, ax=ax1)
    ax1.set_title('Network Tree')

### Draw Adjacency Graph
if not df_neighbour.empty:
    G2 = nx.from_pandas_edgelist(df_neighbour, 'Source', 'Address', create_using=nx.Graph(), edge_attr='RSSI')
    pos2 = nx.circular_layout(G2)
    draw_edges_from_dataframe(df_neighbour, G2, pos2, ax2, directed=False)
    ax2.set_title('Adjacency Graph')
else:
    nx.draw(nx.Graph(), {root_node: (0, 0)}, with_labels=True, node_size=1500, ax=ax2)
    ax2.set_title('Adjacency Graph')

dt = datetime.now()
timestamp = dt.strftime("%Y-%m-%d %H:%M:%S")
fig.suptitle(f'Topology Map\n{timestamp}')
plt.savefig(f'network_graph_{timestamp}.png')
plt.savefig('network_graph.png')
plt.close()