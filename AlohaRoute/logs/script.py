import pandas as pd
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from flask import Flask 
from flask import render_template 
from datetime import datetime

df = pd.read_csv('/home/pi/network.csv')
# df = pd.read_csv('/kaggle/input/network/network.csv')

df['Timestamp'] = pd.to_datetime(df['Timestamp'])
df_sorted = df.sort_values(by='Timestamp', ascending=False)

parents = df_sorted[df_sorted['Role'] == 'PARENT']
df_parent = parents.drop_duplicates(subset='Source')
print(df_parent)

G=nx.from_pandas_edgelist(df_parent, 'Source', 'Address', create_using=nx.DiGraph(), edge_attr='RSSI')
pos = nx.spring_layout(G, seed=25)
nx.draw(G, pos, with_labels=True, node_size=1500, alpha=0.75, arrows=True)
edge_labels = nx.get_edge_attributes(G, 'RSSI')
nx.draw_networkx_edge_labels(G, pos, edge_labels=edge_labels)
# plt.show()

dt = datetime.now()
timestamp = dt.strftime("%Y-%m-%d %H:%M:%S")
plt.title('Network Map')
plt.xlabel(f'{timestamp}')
plt.savefig(f'network_graph_{timestamp}.png')
plt.close()