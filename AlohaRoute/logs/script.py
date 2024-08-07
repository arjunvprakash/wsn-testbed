import pandas as pd
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from flask import Flask 
from flask import render_template 

import pandas as pd
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt


df = pd.read_csv('/home/pi/network.csv')
# df = pd.read_csv('/kaggle/input/network/network.csv')

df['Timestamp'] = pd.to_datetime(df['Timestamp'])
df_sorted = df.sort_values(by='Timestamp', ascending=False)

parents = df_sorted[df_sorted['Role'] == 'PARENT']
df_parent = parents.drop_duplicates(subset='Source')
print(df_parent)

G=nx.from_pandas_edgelist(df_parent, 'Source', 'Address', create_using=nx.DiGraph(), edge_attr='RSSI')
pos = nx.spring_layout(G)
nx.draw(G, pos, with_labels=True, node_size=1500, alpha=0.75, arrows=True)
edge_labels = nx.get_edge_attributes(G, 'RSSI')
nx.draw_networkx_edge_labels(G, pos, edge_labels=edge_labels)
plt.show()

plt.savefig('network_graph.png')
plt.close()

app = Flask(__name__)

@app.route("/") 
def hello(): 
    message = "Network Graph"
    return render_template('index.html', message=message) 
  
if __name__ == "__main__": 
    app.run(debug=True, ssl_context=('/home/pi/cert.pem', '/home/pi/key.pem'))