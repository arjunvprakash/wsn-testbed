import pandas as pd
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from datetime import datetime

def set_positions_v1(node, x, y):
    pos[node] = (x, y)
    children = list(G.predecessors(node))
    print(node, children)
    for i, child in enumerate(children):
        set_positions_v1(child, x + (1 if i % 2 == 0 else -1), y - child)

def set_positions_v3(node, x, y):
    pos[node] = (x, y)
    print(node, x, y)
    children = list(G.predecessors(node))
    num_children = len(children)    
    if num_children > 0:
        spacing = 1
        for i, child in enumerate(children):
            edge_data = G.get_edge_data(child, node)            
            rssi_value = edge_data['RSSI']
            print(edge_data)
            x_factor = (i - (num_children - 1) / 2)
            child_x = x + x_factor * spacing
            set_positions_v3(child, child_x, y + rssi_value)
        
def set_positions_v2(node, x, y):
    #print(node, x, y)
    pos[node] = (x, y)
    children = list(G.predecessors(node))
    num_children = len(children)    
    if num_children > 0:
        spacing = 5
        for i, child in enumerate(children):
            x_factor = (i - (num_children - 1) / 2)
            child_x = x + x_factor * spacing            
            set_positions_v2(child, child_x, y - 1)            

df = pd.read_csv('/home/pi/sw_workspace/AlohaRoute/Debug/output/network.csv')
# df = pd.read_csv('/kaggle/input/network/network.csv')

df['Timestamp'] = pd.to_datetime(df['Timestamp'])
df_sorted = df.sort_values(by='Timestamp', ascending=False)

# parents = df_sorted[df_sorted['Role'] == 'PARENT']
df_parent = df_sorted.drop_duplicates(subset='Address')
df_parent = df_parent[df_parent.Address > 1]
df_parent.sort_values(by='Address')
print(df_parent)

G = nx.from_pandas_edgelist(df_parent, 'Address', 'Parent', create_using=nx.DiGraph(), edge_attr='RSSI')

pos = {}

root_node = 1
set_positions_v2(root_node, 0, 0)

fig, ax = plt.subplots(figsize=(12, 8))

nx.draw(G, pos, with_labels=True, node_size=1500, alpha=0.75, arrows=True, ax=ax)
edge_labels = nx.get_edge_attributes(G, 'RSSI')
nx.draw_networkx_edge_labels(G, pos, edge_labels=edge_labels, ax=ax)

dt = datetime.now()
timestamp = dt.strftime("%Y-%m-%d %H:%M:%S")
ax.set_title(f'Network Graph\n{timestamp}')
# plt.savefig(f'network_graph_{timestamp}.png')
plt.savefig('/home/pi/sw_workspace/AlohaRoute/Debug/output/network_graph.png')
# plt.show()
plt.close()