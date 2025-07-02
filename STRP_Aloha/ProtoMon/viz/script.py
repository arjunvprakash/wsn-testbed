import random
import pandas as pd
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
import matplotlib.image as mpimg
from datetime import datetime
import argparse
import os
import math
import json
from enum import Enum
from bokeh.plotting import figure, output_file, save
from bokeh.layouts import gridplot
from bokeh.models import HoverTool, ColumnDataSource, WheelZoomTool
from bokeh.palettes import Category20, Category20b, Category20c, Dark2, Set3
from bokeh.layouts import column
from bokeh.models import Div
import matplotlib.patches as patches

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

def draw_edges_from_path(df_path, ax, df_rssi, pos=None, directed=True, config=None):
    if config is not None:
        nodeColor = config['node_color']
        sinkColor = config['sink_color']
        nodeSize = config['node_size']
        fontSize = config['font_size']
        edgeColor = config['active_edge_color']
    else:
        nodeColor = default_map_config['node_color']
        sinkColor = default_map_config['sink_color']
        nodeSize = default_map_config['node_size']
        fontSize = default_map_config['font_size']
        edgeColor = default_map_config['active_edge_color']
    G1 = nx.DiGraph()
    for _, row in df_path.iterrows():
        path = row['Path']
        nodes = path.split('-')  # Split the path by the '-' separator
        nodes = [int(node) for node in nodes] 
        for j in range(len(nodes) - 1):
            source, address = nodes[j], nodes[j + 1]
            key = tuple(sorted((source, address)))
            rssi_value = df_rssi.loc[df_rssi['key'] == key, 'RSSI'] if not df_rssi is None else ""
            edgeLabel = rssi_value.iloc[0] if not df_rssi is None and not rssi_value.empty else ''
            if config is not None:
                edgeLabel = ''
            edgeAlpha = 1.0  # Set edge transparency (can be modified based on conditions)
            if not G1.has_edge(int(source), int(address)):
                G1.add_edge(source, address, label=edgeLabel, edge_color=edgeColor)
    pos1 = nx.circular_layout(G1) if pos is None else pos
    nodeColors = [sinkColor if node == root_node else nodeColor for node in G1.nodes()]
    nx.draw(G1, pos1, with_labels=True, node_size=nodeSize, ax=ax, node_color=nodeColors, font_size=fontSize, edge_color=edgeColor)
    edge_labels = nx.get_edge_attributes(G1, 'label')
    nx.draw_networkx_edge_labels(G1, pos1, edge_labels=edge_labels, ax=ax, font_size=fontSize)
    # Add a border using a rectangle patch
    border = patches.Rectangle(
        (0, 0),  # Bottom left corner of the rectangle
        1, 1,    # Width and height of the rectangle
        transform=ax.transAxes,  # Use axes coordinates
        color='black',            # Border color
        linewidth=3,              # Border width
        fill=False                # No fill
    )
    ax.add_patch(border)
    return G1

def draw_edges_from_dataframe(df, G, pos, ax, directed=False, config=None):
    if config is not None:
        nodeColor = config['node_color']
        sinkColor = config['sink_color']
        nodeSize = config['node_size']
        fontSize = config['font_size']
        edgeColor = config['active_edge_color']
    else:
        nodeColor = default_map_config['node_color']
        sinkColor = default_map_config['sink_color']
        nodeSize = default_map_config['node_size']
        fontSize = default_map_config['font_size']
        edgeColor = default_map_config['active_edge_color']
    for _, row in df.iterrows():
        source, address = row['Source'], row['Address']
        edgeStyle = row['edge_style']
        edgeLabel = row['edge_label']
        if config is not None:
            edgeLabel = ''
        edgeAlpha = row['edge_alpha']

        nodeColors = [sinkColor if node == root_node else nodeColor for node in G.nodes()]        

        # if not G.has_edge(source, address):
        #         G.add_edge(source, address, label=edgeLabel, edge_color=edgeColor, alpha=edgeAlpha, style=edgeStyle, arrows=False if edgeStyle == 'dotted' else directed)

    # nx.draw(G, pos, with_labels=True, node_size=nodeSize, ax=ax, node_color=nodeColors, font_size=fontSize, edge_color=edgeColor)
    # edge_labels = nx.get_edge_attributes(G, 'label')
    # nx.draw_networkx_edge_labels(G, pos, edge_labels=edge_labels, ax=ax, label_pos=0.4, font_size=fontSize)

        nx.draw_networkx_nodes(G, pos, ax=ax, node_size=nodeSize, node_color=nodeColors)
        nx.draw_networkx_labels(G, pos, ax=ax, font_size=fontSize)
    
        nx.draw_networkx_edges(
            G, pos=pos, edgelist=[(source, address)], style=edgeStyle, edge_color=edgeColor,
            ax=ax, alpha=edgeAlpha, arrows=directed
        )
    
        if edgeLabel is not None:
            nx.draw_networkx_edge_labels(
                G, pos=pos, edge_labels={(source, address): edgeLabel},
                ax=ax, label_pos=0.4, font_size=fontSize
            )
    # Add a border using a rectangle patch
    border = patches.Rectangle(
        (0, 0),  # Bottom left corner of the rectangle
        1, 1,    # Width and height of the rectangle
        transform=ax.transAxes,  # Use axes coordinates
        color='black',            # Border color
        linewidth=3,              # Border width
        fill=False                # No fill
    )
    ax.add_patch(border)

def get_edge_style(row, directed=False, config=None):
    if config is not None:
        active_edge_style = config['active_edge_style']
        inactive_edge_style = config['inactive_edge_style']
        active_edge_alpha = config['active_edge_alpha']
        inactive_edge_alpha = config['inactive_edge_alpha']
    else:
        active_edge_style = default_map_config['active_edge_style']
        inactive_edge_style = default_map_config['inactive_edge_style']
        active_edge_alpha = default_map_config['active_edge_alpha']
        inactive_edge_alpha = default_map_config['inactive_edge_alpha']        

    source, address = row['Source'], row['Address']    
    is_inactive = source in inactive_nodes or address in inactive_nodes
    row['edge_label'] = '' if is_inactive else str(row['RSSI'])
    row['edge_alpha'] = inactive_edge_alpha if is_inactive else active_edge_alpha    
    if directed:
        row['edge_style'] = inactive_edge_style if is_inactive else active_edge_style         
    else:
        row['edge_style'] = inactive_edge_style
    return row

def calculateRelativeTime(df, timestamp_col='Timestamp', relative_time_col='RelativeTime'):
        df[timestamp_col] = pd.to_datetime(df[timestamp_col], unit='s')

        epoch = pd.Timestamp("1970-01-01")
        df.loc[df[timestamp_col] == epoch, timestamp_col] = pd.NaT

        df[timestamp_col] = df[timestamp_col].ffill()
        df[relative_time_col] = (df[timestamp_col] - df[timestamp_col].min()).dt.total_seconds()
        return df.sort_values(by=relative_time_col)

def plot_metricsV3(data, metrics, layer, saveDir='plots'):
    
    os.makedirs(saveDir, exist_ok=True)  # Ensure the directory exists

    valid_metrics = [metric for metric in metrics if data[metric].max() > 0]
    
    if not valid_metrics:
        print(f"No valid metrics found for {layer}. Skipping plot.")
        return
    
    num_metrics = len(valid_metrics)
    num_cols = min(3, num_metrics)  # Limit to 3 columns per row for better readability
    num_rows = math.ceil(num_metrics / num_cols)

    fig, axes = plt.subplots(num_rows, num_cols, figsize=(5 * num_cols, 4 * num_rows))
    if num_metrics > 1:
        axes = axes.flatten()  # Flatten axes for easy indexing
    else:
        axes = [axes]
    fig.suptitle(f'{layer.capitalize()} Parameters', fontweight='bold')

    for i, metric in enumerate(valid_metrics):
        ax = axes[i]
        for src in data['Source'].unique():
            src_df = data[data['Source'] == src].copy()
            if src_df[metric].max() > 0:
                for addr in src_df['Address'].unique():
                    src_addr_df = src_df[src_df['Address'] == addr]
                    if src_addr_df[metric].max() > 0:
                        if metric.startswith("Total"):
                            src_addr_df = src_addr_df.sort_values(by='RelativeTime')
                            src_addr_df[metric] = src_addr_df[metric].cumsum()     
                        ax.plot(src_addr_df['RelativeTime'], src_addr_df[metric], label=f'Src {src}, Addr {addr}', marker='o')
        
        ax.set_title(metric)
        ax.set_xlabel('Time (s)')
        ax.set_ylabel(metric)
        ax.legend()
        ax.grid()

    # Hide empty subplots if metrics < num_rows * num_cols
    for j in range(i + 1, len(axes)):
        fig.delaxes(axes[j])

    #plt.tight_layout(rect=[0, 0, 1, 0.95])  
    plt.tight_layout()  

    # Save the figure as a PNG
    filePath = os.path.join(saveDir, f"{layer}.png")
    plt.savefig(filePath)   
    filePath = os.path.join(saveDir, f"{layer}_{timestamp}.png")
    plt.savefig(filePath)   
    plt.close(fig) 

def plot_metricsV7(df, cols, layer, saveDir='plots'):
    os.makedirs(saveDir, exist_ok=True)  # Ensure the directory exists

    data = df.copy()
    # data.sort_values(by='RelativeTime', inplace=True)
    metrics = cols

    valid_metrics = [metric for metric in metrics if data[metric].max() > 0]

    # Compute Lost Packets = TotalSent - TotalRecv
    sent_df = data[['RelativeTime', 'Source', 'Address', 'TotalSent']].copy()
    recv_df = data[['RelativeTime', 'Source', 'Address', 'TotalRecv']].copy()
    recv_df = recv_df.rename(columns={'Source': 'Address', 'Address': 'Source'})  # Swap Source & Address
    lost_packets_df = pd.concat([sent_df.assign(TotalRecv=-1), recv_df.assign(TotalSent=0)], ignore_index=True).sort_values(by="RelativeTime")

    valid_metrics.append("TotalLost")  # Add LostPackets
    num_metrics = len(valid_metrics)
    num_cols = min(4, num_metrics)  # Limit to 3 columns per row
    num_rows = math.ceil(num_metrics / num_cols)

    plots = []  
    valid_plots = 0  
    for metric in valid_metrics:
        p = figure(title=f"{metric}", x_axis_label="Time (s)", y_axis_label=metric, width=400, height=300, tools="pan,wheel_zoom,box_zoom,reset,save")    

        color_index = 0

        combined_palette = Category20[20] + Category20b[20] + Category20c[20] + Dark2[8] + Set3[12]
        unique_pairs = list(lost_packets_df.groupby(['Source', 'Address']).groups.keys())  
        num_pairs = len(unique_pairs)
        palette = combined_palette[:num_pairs] if num_pairs <= len(combined_palette) else combined_palette * (num_pairs // len(combined_palette) + 1)

        color_map = {pair: palette[i % len(palette)] for i, pair in enumerate(unique_pairs)}

        def generate_random_rgb():
            r = random.randint(0, 255)
            g = random.randint(0, 255)
            b = random.randint(0, 255)
            return (r, g, b)

        # Fix for key error in agg case: ToDO refactor
        def get_color(source, address):
            key = (source, address)
            return color_map.get(key, generate_random_rgb())
        
        if metric == "TotalLost":
            for (src, addr), src_addr_df in lost_packets_df.groupby(['Source', 'Address']):
                src_addr_df = src_addr_df[['RelativeTime', 'Source','Address', 'TotalSent', 'TotalRecv']].copy()
                
                if src_addr_df["TotalSent"].max() > 0:
                    src_addr_df['TotalSent'] = src_addr_df['TotalSent'].ffill().cumsum()
                    src_addr_df = src_addr_df[src_addr_df['TotalRecv'] > -1]
                    src_addr_df['TotalRecv'] = src_addr_df['TotalRecv'].cumsum()
                    src_addr_df[metric] = src_addr_df['TotalSent'] - src_addr_df['TotalRecv']
                    src_addr_df = src_addr_df[(src_addr_df['TotalSent'] > 0) & (src_addr_df[metric] > -1)]   

                    if src_addr_df.shape[0] > 0:
                        # Create Bokeh ColumnDataSource
                        source = ColumnDataSource(src_addr_df)

                        key = (src, addr)                       
                        p.line('RelativeTime' 
                               ,metric
                               ,source=source
                               ,legend_label=f'Node {src} → {addr}'
                               ,line_width=2
                               ,color=get_color(src,addr)
                               )
                        p.scatter('RelativeTime'
                                ,metric
                               ,source=source
                               ,legend_label=f'Node {src} → {addr}'
                               ,color=get_color(src,addr)
                               ,size=2
                               )                               
                        valid_plots += 1

        else:
            if metric.lower().startswith("agg"):
                for (src), src_addr_df in data.groupby(['Source']):
                    src = src[0]
                    if src_addr_df[metric].max() > 0:
                        src_addr_df = src_addr_df[['RelativeTime','Source',metric]].copy()  
                        if metric.lower().split("agg")[1].startswith("total"):
                            src_addr_df[metric] = src_addr_df[metric].cumsum()
                        # addr = src_addr_df['Address'].unique()[0]
                    if src_addr_df.shape[0] > 0:
                        # Create Bokeh ColumnDataSource
                        source = ColumnDataSource(src_addr_df)

                        # key = (src,addr) 
                        #print(key)
                    
                        labelStr = f'Node {src}'
                        p.line('RelativeTime', 
                               metric
                               ,source=source
                               ,legend_label=labelStr
                               ,line_width=2
                               ,color=get_color(src,0)
                               )
                        p.scatter('RelativeTime'
                                ,metric
                               ,source=source
                               ,legend_label=labelStr
                               ,color=get_color(src,0)
                               ,size=2
                               )
                        valid_plots += 1
            else:
                for (src, addr), src_addr_df in data.groupby(['Source', 'Address']):
                    if src_addr_df[metric].max() > 0:
                        src_addr_df = src_addr_df[['RelativeTime','Source','Address',metric]].copy()
                        # src_addr_df = src_addr_df.sort_values(by='RelativeTime')
                        if metric.lower().startswith("total"):
                            src_addr_df[metric] = src_addr_df[metric].cumsum()

                        if src_addr_df.shape[0] > 0:
                            # Create Bokeh ColumnDataSource
                            source = ColumnDataSource(src_addr_df)

                            key = (src, addr)                        
                        
                            labelStr = f'Node {addr} → {src}' if metric.lower().endswith('recv') or metric.lower().endswith('latency') else f'Node {src} → {addr}'
                            p.line('RelativeTime', 
                                metric
                                ,source=source
                                ,legend_label=labelStr
                                ,line_width=2
                                ,color=get_color(src,addr)
                                )
                            p.scatter('RelativeTime'
                                    ,metric
                                ,source=source
                                ,legend_label=labelStr
                                ,color=get_color(src,addr)
                                ,size=2
                                )
                            valid_plots += 1
        
        if valid_plots > 0:
            p.legend.background_fill_alpha = 0.2 
            # p.add_layout(p.legend[0], place="right")  # Move to right if space available
            # p.legend.label_text_font_size = "8pt"
            # p.legend.location = "top_left" if src % 2 == 0 else "top_right"
            p.legend.click_policy = "hide" 

            if metric.lower().startswith("agg"):
                hover = HoverTool(tooltips=[
                    ("Time", "@RelativeTime s"),
                    (metric, f"@{metric}"),
                    ("Source", f"@Source")
                ])       
            elif metric.lower().endswith('recv') or metric.lower().endswith('latency'):
                hover = HoverTool(tooltips=[
                    ("Time", "@RelativeTime s"),
                    (metric, f"@{metric}"),
                    ("Source", "@Address"),
                    ("Address", "@Source")
                ])                            
            else :
                hover = HoverTool(tooltips=[
                    ("Time", "@RelativeTime s"),
                    (metric, f"@{metric}"),
                    ("Source", f"@Source"),
                    ("Address", "@Address")
                ])                            
            p.add_tools(hover) 
            # p.add_tools(WheelZoomTool())
            # p.toolbar.active_inspect = hover
            plots.append(p)

    grid = gridplot([plots[i:i+num_cols] for i in range(0, len(plots), num_cols)])

    # Create a global title
    global_title = Div(
        text=f"<h2 style='text-align:center;'>{layer.capitalize()} Parameters : {timestamp}</h2>",
        width=800, 
        height=40
    )

    # Combine title and grid into a single layout
    grid_layout = column(global_title, grid)

    # Save HTML
    output_file_path = os.path.join(saveDir, f"{layer}.html")
    output_file(output_file_path)
    save(grid_layout)

def plot_metricsV4(df, cols, layer, saveDir='plots'):

    data = df.copy()
    metrics = cols

    # print(data.tail())
    
    os.makedirs(saveDir, exist_ok=True)  # Ensure the directory exists

    valid_metrics = [metric for metric in metrics if data[metric].max() > 0]
    
    # Compute Lost Packets = TotalSent - TotalRecv
    sent_df = data[['RelativeTime', 'Source', 'Address', 'TotalSent']].copy()
    recv_df = data[['RelativeTime', 'Source', 'Address', 'TotalRecv']].copy()

    # Prepare the received data by swapping Source & Address
    recv_df = recv_df.rename(columns={'Source': 'Address', 'Address': 'Source'})

    # Merge sent & received data
    lost_packets_df = pd.concat([sent_df.assign(TotalRecv=-1), recv_df.assign(TotalSent=0)], ignore_index=True)
    lost_packets_df = lost_packets_df.sort_values(by="RelativeTime")

    valid_metrics.append("TotalLost")  # Add LostPackets as a new plot
    num_metrics = len(valid_metrics)
    num_cols = min(3, num_metrics)  # Limit to 3 columns per row
    num_rows = math.ceil(num_metrics / num_cols)

    fig, axes = plt.subplots(num_rows, num_cols, figsize=(5 * num_cols, 4 * num_rows))
    axes = axes.flatten() if num_metrics > 1 else [axes]
    fig.suptitle(f'{layer.capitalize()} Parameters : {timestamp}', fontweight='bold')

    valid_plots = 0
    
    for i, metric in enumerate(valid_metrics):
        ax = axes[i]
              
        if metric == "TotalLost":
            for (src, addr), src_addr_df in lost_packets_df.groupby(['Source', 'Address']):
                src_addr_df = src_addr_df.copy()   
                
                if src_addr_df["TotalSent"].max() > 0:                   
                    src_addr_df['TotalSent'] = src_addr_df['TotalSent'].ffill().cumsum()
                    src_addr_df = src_addr_df[src_addr_df['TotalRecv'] > -1]
                    src_addr_df['TotalRecv'] = src_addr_df['TotalRecv'].cumsum()
                    src_addr_df = src_addr_df[src_addr_df['TotalSent'] > 0]
                    src_addr_df['TotalLost'] = src_addr_df['TotalSent'] - src_addr_df['TotalRecv']
                    src_addr_df = src_addr_df[(src_addr_df['TotalSent'] > 0) & (src_addr_df['TotalLost'] > -1)]                 
                    
                    if src_addr_df.shape[0] > 0:
                        # print(f'{addr} -> {src} {src_addr_df.head()}')
                        ax.plot(src_addr_df['RelativeTime'], src_addr_df[metric], label=f'Node {src} → {addr}', marker=".")
                        valid_plots+=1

        else:                
            for (src, addr), src_addr_df in data.groupby(['Source', 'Address']):
                if src_addr_df[metric].max() > 0:
                    src_addr_df = src_addr_df.copy()                   
                    
                    if metric.lower().startswith("total"):
                        src_addr_df[metric] = src_addr_df[metric].cumsum()

                    if src_addr_df.shape[0] > 0:
                        labelStr = f'Node {addr} → {src}' if metric.lower().endswith('recv') or metric.lower().endswith('latency') else f'Node {src} → {addr}'                   
                        ax.plot(src_addr_df['RelativeTime'], src_addr_df[metric], label=labelStr, marker=".")
                        valid_plots+=1
        
        if valid_plots > 0:
            ax.set_title(metric)
            ax.set_xlabel('Time (s)')
            ax.set_ylabel(metric)
            ax.legend()
            ax.grid()

    # Hide empty subplots
    for j in range(i + 1, len(axes)):
        fig.delaxes(axes[j])
    
    plt.tight_layout()  

    # Save the figure as a PNG
    filePath = os.path.join(saveDir, f"{layer}.png")
    plt.savefig(filePath)   
    # filePath = os.path.join(saveDir, f"{layer}_{timestamp}.png")
    # plt.savefig(filePath)   
    plt.close(fig) 


# Dependency: routing.h
class LinkType(Enum):
    IDLE = 0
    INBOUND = 1
    OUTBOUND = 2
    INOUTBOUND = 3

# Dependency: routing.h
class NodeState(Enum):
    UNKNOWN = -1
    INACTIVE = 0
    ACTIVE = 1

# Main Script

## Parse arguments
args = parseArgs()
root_node = int(args.sink)

dt = datetime.now() 
timestamp = dt.strftime("%Y-%m-%d %H:%M:%S")
saveDir = "plots"

# Global variables for topology maps
config_json = '../../ProtoMon/viz/map_config.json'
node_pos_csv = '../../ProtoMon/viz/node_pos.csv'
map_png = '../../ProtoMon/viz/map.png'

# Global variables for graphs
routing_csv = 'routing.csv'
mac_csv = 'mac.csv'
network_csv = 'network.csv'
numMaps = 3 # max possible network graphs

default_map_config = {
    "active_edge_color": "k",
    "node_color": "#1f78b4",
    "sink_color": "green",
    "node_size": 1500,
    "font_size": 10,
    "inactive_edge_color": "gray",
    "inactive_edge_style": "dotted",
    "active_edge_style": "solid",
    "active_edge_alpha": 0.9,
    "inactive_edge_alpha": 0.2
}

map_config = default_map_config.copy()

## Standard columns for MAC and Routing data
mac_metaColumns = ['RelativeTime','Timestamp','Source','Address']
routing_metaColumns = ['RelativeTime','Timestamp','Source','Address', 'Path']

if os.path.exists(routing_csv):
    routing_df = pd.read_csv(routing_csv)
    routing_df = calculateRelativeTime(routing_df)
    df_path = routing_df[['RelativeTime', 'Path', 'Source', 'Address']].copy()
    df_path = df_path.dropna(subset=['Path'])
    df_path = df_path.sort_values(by='RelativeTime', ascending=False)
    df_path = df_path.drop_duplicates(subset=['Source', 'Address'], keep='first')

fig, ax = plt.subplots(1, numMaps, figsize=(16, 8))
i = 0

# Read configuration from file
config_file_path = config_json
if os.path.exists(config_file_path):
    try:
        with open(config_file_path, 'r') as config_file:
            file_config = json.load(config_file)
            # Update config with values from the file, using defaults if not present
            map_config.update({key: file_config.get(key, default_map_config[key]) for key in default_map_config.keys()})
    except (json.JSONDecodeError, KeyError) as e:
        print(f"Error reading config file: {e}. Using default configuration.")

if not df_path.empty:
    #print(df_path)
    G1 = draw_edges_from_path(df_path, ax[i], None)
    
    # Show the plot
    ax[i].set_title("Routing Path")
    i += 1

    ### Plot Node positions
    if os.path.exists(node_pos_csv) and os.path.exists(map_png):  
        
        node_pos = pd.read_csv(node_pos_csv)
        Gx = G1 #if not df_path.empty else G2 
        df_geo = df_path #if not df_path.empty else df_neighbour
        filtered_node_loc = node_pos[node_pos['Node'].isin(Gx.nodes)]
        missing_nodes = set(Gx.nodes) - set(filtered_node_loc['Node'])
        img = mpimg.imread(map_png)
        if len(missing_nodes) == 0 and img is not None:           
            ax[i].imshow(img)
            # G3 = nx.from_pandas_edgelist(df_geo, 'Source', 'Address', create_using=nx.Graph(), edge_attr='RSSI')                
            pos3 = filtered_node_loc.set_index('Node')[['Lat', 'Long']].apply(tuple, axis=1).to_dict()
            # draw_edges_from_dataframe(df_geo, G3, pos3, ax[i], directed=False,config=map_config)
            G2 = draw_edges_from_path(df_path, ax[i], None, pos=pos3,config=map_config)
            ax[i].set_title('Node Positions')
            i += 1
        else:
            print(f"Positions of {missing_nodes} missing in " + node_pos_csv)

## Network graph
if os.path.exists(network_csv):    
    
    ### ------------ Data Preparation ------------ ###
    df = pd.read_csv(network_csv)    
    calculateRelativeTime(df)    
    #df['Timestamp'] = pd.to_datetime(df['Timestamp'], unit='s')
    df.sort_values(by='Timestamp', ascending=False, inplace=True)

    node_state = df[['Timestamp', 'Address', 'State']].copy()
    node_state.sort_values(by='Timestamp', ascending=False, inplace=True)
    node_state.drop_duplicates(subset=['Address',], keep='first', inplace=True, ignore_index=True)
    node_state_dict = dict(zip(node_state['Address'], node_state['State']))
    inactive_nodes = {node for node, state in node_state_dict.items() if state == 0}

    ### Data extraction for Network Tree
    # Check if LinkType column contains any INBOUND entries

    df_p1 = pd.DataFrame()   
    df_p2 = pd.DataFrame()
    df_p3 = pd.DataFrame()
    df_p4 = pd.DataFrame()

    ###     Direct parent info
    df_p1 = df[(df['State'] != NodeState.UNKNOWN.value) & (df['LinkType'] == LinkType.OUTBOUND.value)][['Timestamp','Source', 'Address', 'RSSI']]
    # print(f"Direct parent links : {df_p1.shape[0]}")

    ###     Direct child info
    df_p2 = df[(df['State'] != NodeState.UNKNOWN.value) & (df['LinkType'] == LinkType.INBOUND.value)][['Timestamp','Source', 'Address', 'RSSI']].rename(columns={'Source': 'Address', 'Address': 'Source'})
    # print(f"Direct child links : {df_p2.shape[0]}")

    # Support for tree protocols: STRP
    # Check if the columns 'Parent' and 'ParentRSSI' exist in df
    parentcols_exist = all(col in df.columns for col in ['Parent', 'ParentRSSI']) 
    ###    Extract indirect parent info
    if parentcols_exist:
        df_p3 = df[df['State'] != NodeState.UNKNOWN.value][['Timestamp','Address', 'Parent', 'ParentRSSI']].rename(columns={'Address': 'Source', 'Parent': 'Address', 'ParentRSSI': 'RSSI'})
        # print(f"Indirect parent links : {df_p3.shape[0]}")

    #  Bidirectional links     
    df_p4 = df[(df['State'] != NodeState.UNKNOWN.value) & (df['LinkType'] == LinkType.INOUTBOUND.value)][['Timestamp','Source', 'Address', 'RSSI']]
    if not df_p4.empty:        
        # Duplicate the rows and swap Source and Address
        df_p4_swapped = df_p4.rename(columns={'Source': 'Address', 'Address': 'Source'})
        df_p4 = pd.concat([df_p4, df_p4_swapped], ignore_index=True)
        # print(f"Bidirectional links : {df_p4.shape[0]}")

    ###     Combine
    df_parent = pd.concat([df_p1, df_p2, df_p3, df_p4], ignore_index=True)
    df_parent = df_parent[df_parent['Address'] > 0]
    df_parent = df_parent.sort_values('Timestamp', ascending=False)

    max_timestamp_per_source = df_parent.groupby('Source')['Timestamp'].max().reset_index()
    df_parent = df_parent.merge(max_timestamp_per_source, on=['Source', 'Timestamp'], how='inner')

    df_parent['key'] = df_parent.apply(lambda row: tuple(sorted([row['Source'], row['Address']])), axis=1)
    df_parent = df_parent.sort_values(by='Timestamp', ascending=False)
    df_parent = df_parent.drop_duplicates(subset='key', keep='first').sort_values(by='key')

    # df_parent.sort_values(by='key', inplace=True)
    df_parent = df_parent.apply(get_edge_style, axis=1, directed=True)

    # Check if exactly one nexthop per Source
    nextop_count = df_parent.groupby('Source').size()
    single_nexthop = nextop_count.max() == 1

    ### Data preparation for adjacency graph
    ###     Direct adjacency
    df1 = df[df['State'] != NodeState.UNKNOWN.value][['Timestamp','Source', 'Address', 'RSSI']]
    df1['key'] = df1.apply(lambda row: tuple(sorted([row['Source'], row['Address']])), axis=1)
    df1 = df1.apply(get_edge_style, axis=1, directed=False)
    ###     Combine with parent info
    df_neighbour = pd.concat([df_parent, df1], ignore_index=False).drop_duplicates(subset='key', keep='first').sort_values(by='key')

    parent_rssi_map = df_parent.set_index('key')['RSSI'].to_dict()
    df_neighbour['RSSI'] = df_neighbour.apply(
        lambda row: parent_rssi_map.get(row['key'], row['RSSI']), axis=1
    )
    

    ### Draw Network Tree

    # if not df_parent.empty:
    #     G3 = nx.from_pandas_edgelist(df_parent, 'Source', 'Address', create_using=nx.DiGraph(), edge_attr='RSSI')
    #     G3.add_node(root_node)     
    #     pos1 = {}
    #     # if single_nexthop:
    #     #     # Plot as tree
    #     #     set_positions_v2(root_node, 0, 0)   
    #     # else:
    #        # Plot as nodes graph   
    #     pos1 = nx.circular_layout(G3)

    #     draw_edges_from_dataframe(df_parent, G3, pos1, ax[i], directed=True,config=None)
    #     ax[i].set_title('Network Tree')
    #     i += 1
    # else:
    #     nx.draw(nx.Graph(), {root_node: (0, 0)}, with_labels=True, node_size=default_map_config['node_size'], ax=ax[i])
    #     ax[i].set_title('Network Tree')


    ### Draw Adjacency Graph
    if not df_neighbour.empty:
        G4 = nx.from_pandas_edgelist(df_neighbour, 'Source', 'Address', create_using=nx.Graph(), edge_attr='RSSI')
        pos2 = nx.circular_layout(G4)
        draw_edges_from_dataframe(df_neighbour, G4, pos2, ax[i], directed=False,config=None)
        ax[i].set_title('Adjacency Graph')
        i += 1
    else:
        nx.draw(nx.Graph(), {root_node: (0, 0)}, with_labels=True, node_size=default_map_config['node_size'], ax=ax[i])
        ax[i].set_title('Adjacency Graph')
        i += 1

# for j in range(i, len(fig.axes)):
#     if not fig.axes[j].has_data():  # Check if the axis is empty
#         fig.delaxes(fig.axes[j])

for j in range(len(fig.axes) - 1, i - 1, -1):
    if not fig.axes[j].has_data():  # Check if the axis is empty
        fig.delaxes(fig.axes[j])

fig.suptitle(f'Network Topology : {timestamp}', fontweight='bold')
plt.tight_layout() 

# Save plot to file
os.makedirs(saveDir, exist_ok=True)  # Ensure the directory exists
filePath = os.path.join(saveDir, f'network_graph_{timestamp}.png')
plt.savefig(filePath)    
filePath = os.path.join(saveDir, 'network_graph.png')
plt.savefig(filePath)
plt.close()

## Routing metrics plot
if os.path.exists(routing_csv):
    routing_df = pd.read_csv(routing_csv)   

    routing_df = calculateRelativeTime(routing_df)

    routing_dataCols = [col for col in routing_df.columns if col not in routing_metaColumns]
    plot_metricsV7(routing_df,routing_dataCols,'routing')

## MAC metrics plot
if os.path.exists(mac_csv):
    mac_df = pd.read_csv(mac_csv)

    mac_df = calculateRelativeTime(mac_df)

    mac_dataCols = [col for col in mac_df.columns if col not in mac_metaColumns]
    plot_metricsV7(mac_df, mac_dataCols, 'mac')