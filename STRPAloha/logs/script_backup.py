import pandas as pd
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from datetime import datetime
import argparse

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
    print(node, x, y)
    pos1[node] = (x, y)
    children = list(G1.predecessors(node))
    num_children = len(children)    
    if num_children > 0:
        spacing = 5
        for i, child in enumerate(children):
            x_factor = (i - (num_children - 1) / 2)
            child_x = x + x_factor * spacing            
            set_positions_v2(child, child_x, y - 1)            

parser = argparse.ArgumentParser(description='Generate topology map for WSN from a CSV.')
parser.add_argument('sink', metavar='sink', type=int, help='address of sink')
args = parser.parse_args()

df = pd.read_csv('/home/pi/sw_workspace/AlohaRoute/Debug/results/network.csv')
# df = pd.read_csv('/kaggle/input/network/network.csv')

df['Timestamp'] = pd.to_datetime(df['Timestamp'])
df_sorted = df.sort_values(by='Timestamp', ascending=False)

# parents = df_sorted[df_sorted['Role'] == 'PARENT']
df_parent = df_sorted.drop_duplicates(subset='Address')
df_parent = df_parent[df_parent.Address > 1]
df_parent.sort_values(by='Address')
print("df_parent:")
print(df_parent)

df_neighbour = df_sorted.drop_duplicates(subset=['Source', 'Address']).sort_values(by='Source')
# print(df_neighbour)
df_neighbour = df_neighbour.sort_values('Timestamp', ascending=False)
df_neighbour['key'] = df_neighbour[['Source', 'Address']].apply(lambda x: tuple(sorted(x)), axis=1)
df_neighbour = df_neighbour.drop_duplicates('key', keep='first')
df_neighbour = df_neighbour.drop('key', axis=1)
df_neighbour.sort_values('Source')
print("df_neighbour:")
print(df_neighbour)

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 8))

G1 = nx.from_pandas_edgelist(df_parent, 'Address', 'Parent', create_using=nx.DiGraph(), edge_attr='ParentRSSI')

if 0 in G1:
    G1.remove_node(0)

pos1 = {}
root_node = int(args.sink)
set_positions_v2(root_node, 0, 0)
nx.draw(G1, pos1, with_labels=True, node_size=1500, alpha=0.75, arrows=True, ax=ax1)
edge_labels = nx.get_edge_attributes(G1, 'ParentRSSI')
nx.draw_networkx_edge_labels(G1, pos1, edge_labels=edge_labels, ax=ax1)
ax1.set_title('Network Tree')

df_parent_edges = df_parent[['Timestamp','Address', 'Parent', 'ParentRSSI']].rename(columns={'Address': 'Source', 'Parent': 'Address', 'ParentRSSI': 'RSSI'}).sort_values('Source')
# df_combined = pd.concat([df_neighbour[['Timestamp', 'Source', 'Address', 'RSSI']], 
#                          df_parent_edges[['Timestamp', 'Source', 'Address', 'RSSI']]])
# df_combined.sort_values(by='Timestamp', ascending=False)
# df_combined = df_combined.drop_duplicates(subset=['Source', 'Address'], keep='first')
G2 = nx.from_pandas_edgelist(df_neighbour, 'Source', 'Address', create_using=nx.Graph(), edge_attr='RSSI')

if 0 in G2:
    G2.remove_node(0)

G2.add_nodes_from(df_parent[['Address', 'Parent']].values.flatten())

for _, row in df_parent_edges.iterrows():
    source = row['Source']
    address = row['Address']
    rssi = row['RSSI']
    G2.add_edge(source, address, RSSI=rssi)



pos2 = nx.circular_layout(G2)
nx.draw(G2, pos2, with_labels=True, node_size=1500, alpha=0.75, arrows=False, ax=ax2, edge_color='grey', style=':')
edge_labels2 = nx.get_edge_attributes(G2, 'RSSI')
nx.draw_networkx_edge_labels(G2, pos2, edge_labels=edge_labels2, ax=ax2, label_pos=0.4)
ax2.set_title('Adjacency Graph')

dt = datetime.now()
timestamp = dt.strftime("%Y-%m-%d %H:%M:%S")
fig.suptitle(f'Topology Map\n{timestamp}')
plt.savefig(f'/home/pi/sw_workspace/AlohaRoute/Debug/results/network_graph_{timestamp}.png')
plt.savefig('/home/pi/sw_workspace/AlohaRoute/Debug/results/network_graph.png')
# plt.show()
plt.close()