import pandas as pd
import numpy as np
import networkx as nx
import matplotlib.pyplot as plt
from datetime import datetime
import argparse
import os
import math
from bokeh.plotting import figure, output_file, save
from bokeh.layouts import gridplot
from bokeh.models import HoverTool, ColumnDataSource, WheelZoomTool
from bokeh.palettes import Category20, Category20b, Category20c, Dark2, Set3
from bokeh.layouts import column
from bokeh.models import Div

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

def calculateRelativeTime(df, timestamp_col='Timestamp', relative_time_col='RelativeTime'):
        df[timestamp_col] = pd.to_datetime(df[timestamp_col], unit='s')

        epoch = pd.Timestamp("1970-01-01")
        df.loc[df[timestamp_col] == epoch, timestamp_col] = pd.NaT

        df[timestamp_col] = df[timestamp_col].ffill()
        df[relative_time_col] = (df[timestamp_col] - df[timestamp_col].min()).dt.total_seconds()
        return df

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
                               ,color=color_map[key]
                               )
                        p.scatter('RelativeTime'
                                ,metric
                               ,source=source
                               ,legend_label=f'Node {src} → {addr}'
                               ,color=color_map[key]
                               ,size=2
                               )                               
                        valid_plots += 1

        else:
            for (src, addr), src_addr_df in data.groupby(['Source', 'Address']):
                if src_addr_df[metric].max() > 0:
                    src_addr_df = src_addr_df[['RelativeTime','Source','Address',metric]].copy()
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
                               ,color=color_map[key]
                               )
                        p.scatter('RelativeTime'
                                ,metric
                               ,source=source
                               ,legend_label=labelStr
                               ,color=color_map[key]
                               ,size=2
                               )
                        valid_plots += 1
        
        if valid_plots > 0:
            p.legend.background_fill_alpha = 0.2 
            # p.add_layout(p.legend[0], place="right")  # Move to right if space available
            # p.legend.label_text_font_size = "8pt"
            # p.legend.location = "top_left" if src % 2 == 0 else "top_right"
            p.legend.click_policy = "hide" 

            if metric.lower().endswith('recv') or metric.lower().endswith('latency'):
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

# Main Script

## Parse arguments
args = parseArgs()
root_node = int(args.sink)

dt = datetime.now() 
timestamp = dt.strftime("%Y-%m-%d %H:%M:%S")
saveDir = "plots"

# Value mapping for node states
node_states = {
    -1: 'Unknown',
     0: 'Inactive',
     1: 'Active'
}

# Value mapping for node roles
node_roles = {
    0: 'Node',
    1: 'Child',
    2: 'Parent'
}

## Standard columns for MAC and Routing data
mac_metaColumns = ['RelativeTime','Timestamp','Source','Address']
routing_metaColumns = ['RelativeTime','Timestamp','Source','Address']

## Network graph
if os.path.exists('network.csv'):    
    
    df = pd.read_csv('network.csv')    
    ### ------------ Data Preparation ------------ ###
    calculateRelativeTime(df)    
    #df['Timestamp'] = pd.to_datetime(df['Timestamp'], unit='s')
    df.sort_values(by='Timestamp', ascending=False, inplace=True)

    node_state = df[['Timestamp', 'Address', 'State']].copy()
    node_state.sort_values(by='Timestamp', ascending=False, inplace=True)
    node_state.drop_duplicates(subset=['Address',], keep='first', inplace=True, ignore_index=True)
    node_state_dict = dict(zip(node_state['Address'], node_state['State']))
    inactive_nodes = {node for node, state in node_state_dict.items() if state == 0}
    # print("node_state:", node_state_dict)
    # print("inactive_nodes:", inactive_nodes)

    ### Data extraction for Network Tree
    ###     Direct parent info
    df_p1 = df[(df['State'] >= 0) & (df['Role'] == 2)][['Timestamp','Source', 'Address', 'RSSI']]
    ###     Direct child info
    df_p2 = df[(df['State'] >= 0) & (df['Role'] == 1)][['Timestamp','Source', 'Address', 'RSSI']].rename(columns={'Source': 'Address', 'Address': 'Source'})
    ###     Indirect parent info
    df_p3 = df[df['State'] >= 0][['Timestamp','Address', 'Parent', 'ParentRSSI']].rename(columns={'Address': 'Source', 'Parent': 'Address', 'ParentRSSI': 'RSSI'})
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
    df1 = df[df['State'] >= 0][['Timestamp','Source', 'Address', 'RSSI']]
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
  
    fig.suptitle(f'Topology Map\n{timestamp}')

    # Save plot to file
    os.makedirs(saveDir, exist_ok=True)  # Ensure the directory exists
    filePath = os.path.join(saveDir, f'network_graph_{timestamp}.png')
    plt.savefig(filePath)    
    filePath = os.path.join(saveDir, 'network_graph.png')
    plt.savefig(filePath)
    plt.close()

## Routing metrics plot
if os.path.exists('routing.csv'):
    routing_df = pd.read_csv('routing.csv')   

    routing_df = calculateRelativeTime(routing_df)

    routing_dataCols = [col for col in routing_df.columns if col not in routing_metaColumns]
    plot_metricsV7(routing_df,routing_dataCols,'routing')

## MAC metrics plot
if os.path.exists('mac.csv'):
    mac_df = pd.read_csv('mac.csv')

    mac_df = calculateRelativeTime(mac_df)

    mac_dataCols = [col for col in mac_df.columns if col not in mac_metaColumns]
    plot_metricsV7(mac_df, mac_dataCols, 'mac')