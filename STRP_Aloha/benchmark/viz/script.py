import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime
import argparse
import os
import math

def calculateRelativeTime(df, timestamp_col='Timestamp', relative_time_col='RelativeTime'):
    df[timestamp_col] = pd.to_datetime(df[timestamp_col], unit='ms')
    df['RecvTimestamp'] = pd.to_datetime(df['RecvTimestamp'], unit='ms')
    df['SentTimestamp'] = pd.to_datetime(df['SentTimestamp'], unit='ms')
    min_timestamp = df[timestamp_col].min()
        
    df[relative_time_col] = (df[timestamp_col] - min_timestamp).dt.total_seconds()
    
    df['Delay'] = (df['RecvTimestamp'] - df['SentTimestamp']).dt.total_seconds()   
    df['RelativeRecvTime'] = (df['RecvTimestamp'] - min_timestamp).dt.total_seconds()
    df['RelativeSentTime'] = (df['SentTimestamp'] - min_timestamp).dt.total_seconds()   

    return df.sort_values(by=relative_time_col)

def parseArgs():
    parser = argparse.ArgumentParser(description='Generate benchmark visualization')
    parser.add_argument('-c', '--config', type=str, metavar='config', help='Experiment configuration values as string')
    return parser.parse_args()

# main

# data_csv = StringIO(data)

args = parseArgs()
configStr = args.config

saveDir = "benchmark"
recv_csv = f'{saveDir}/recv.csv'
dt = datetime.now() 
timestamp = dt.strftime("%Y-%m-%d %H:%M:%S")

df = pd.read_csv(recv_csv)
df = calculateRelativeTime(df)


df['key'] = df.apply(lambda row: tuple([row['Source'], row['Address']]), axis=1)
df = df.sort_values(by='key')


plotList = [['SeqId','Delay'], 
            ['Histogram','Delay'],
            ['SeqId','RelativeRecvTime'],
            ['SeqId','RelativeSentTime']]

num_metrics = len(plotList) + 1
num_cols = min(2, num_metrics)  # Limit to 2 columns per row
num_rows = math.ceil(num_metrics / num_cols)

fig, axes = plt.subplots(num_rows, num_cols, figsize=(5 * num_cols, 4 * num_rows))
axes = axes.flatten() if num_metrics > 1 else [axes]
fig.suptitle(f'{timestamp}\n{configStr}')

for i, (x,y) in enumerate(plotList):
    ax = axes[i]
    if 'Histogram' == x:        
        ax.hist(df[y], bins=5, alpha=0.3, edgecolor='black') #, density=True)
        ax.set_xlabel('Delay (seconds)')
        # ax.set_ylabel('Percentage')
        ax.set_ylabel('Frequency')
        # ax.set_yticks([0, 0.2, 0.4, 0.6, 0.8, 1.0]) 
        # ax.set_yticklabels([0, 20, 40, 60, 80, 100])
    else:
        for (src, addr), grp in df.groupby(['Source', 'Address']):
            plt.title(f'{x} vs {y}')
            label = (f'{addr}→{src}')
            if ('SeqId','Delay') == (x,y):
                delay_avg = round(grp[y].mean(), 2)
                delay_median = round(grp[y].median(), 2)
                delay_min = round(grp[y].min(), 2)
                delay_max = round(grp[y].max(), 2)
                label = (f'{addr}→{src}\n'
                        f'Avg= {delay_avg}s\n'
                        f'Med= {delay_median}s\n'
                        f'Min= {delay_min}s\n'
                        f'Max= {delay_max}s\n')
                
                aggregates=(f'Aggregate Delay:\n'
                f'Avg= {round(df[y].mean(), 2)} s\n'
                f'Med= {round(df[y].median(), 2)} s\n'
                f'Min= {round(df[y].min(), 2)} s\n'
                f'Max= {round(df[y].max(), 2)} s\n')
                
            ax.plot(grp[x], grp[y], marker='.', label=label)
            ax.grid()
            legend = ax.legend(title=f'{y}', bbox_to_anchor=(1, 0.5), loc='center left' )
            #legend = ax.legend()
            legend.get_frame().set_alpha(0.0001)
            ax.set_xlabel(x)
            ax.set_ylabel(f'{y} (seconds)')
            ax.set_title(f'{x} vs {y} (seconds)')
            if ('SeqId','Delay') == (x,y):
                ax.text(0.01, 1.1, aggregates, wrap=True, transform=ax.transAxes)

       
for j in range(i + 1, len(axes)):
    fig.delaxes(axes[j])

plt.tight_layout()
fig.savefig(f'{saveDir}/benchmark.png')