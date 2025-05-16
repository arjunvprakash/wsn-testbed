import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime
import argparse
import os
import math
import seaborn as sns

def calculateRelativeTime(df, timestamp_col='Timestamp', relative_time_col='RelativeTime'):
    df[timestamp_col] = pd.to_datetime(df[timestamp_col], unit='ms')
    df['RecvTimestamp'] = pd.to_datetime(df['RecvTimestamp'], unit='ms')
    df['SentTimestamp'] = pd.to_datetime(df['SentTimestamp'], unit='ms')
    min_timestamp = df[timestamp_col].min()
        
    df[relative_time_col] = (df[timestamp_col] - min_timestamp).dt.total_seconds()
    
    df['RelativeRecvTime'] = (df['RecvTimestamp'] - min_timestamp).dt.total_seconds()
    df['RelativeSentTime'] = (df['SentTimestamp'] - min_timestamp).dt.total_seconds()   

    df['Delay'] = (df['RecvTimestamp'] - df['SentTimestamp']).dt.total_seconds()
    df["Node"] = df["Address"].astype(str)

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
df_lost = df[['Timestamp','Source', 'Address','Node', 'HopCount','TotalCount','RecvCount']]
df_lost = df_lost.sort_values(by='Timestamp', ascending=False).drop_duplicates(subset='Address', keep='first')
df_lost['LostCount'] = (df_lost['TotalCount'] - df_lost['RecvCount']).astype(int)
df_lost['LossRate'] = (df_lost['LostCount'] / df_lost['TotalCount']).astype(float) * 100
df_lost = df_lost.sort_values(by='Address', ascending=True)
df = df.dropna()

df['key'] = df.apply(lambda row: tuple([row['Source'], row['Address']]), axis=1)
df = df.sort_values(by='key')

plotList = [
            ['Node', 'AggDelay'],
            ['HopCount','BoxDelay'],
            ['Node', 'BoxDelay'],
            ['Node', 'BarDelay'],
            ['SeqId','DelayFreq'],  
            ['SeqId','NodeDelay'], 
            ['HopCount', 'LostCount'],
            ['Node', 'LostCount'],
            ['HopCount', 'LossRate'],
            ['Node', 'LossRate'], 
            # ['SeqId','Delay'],
            # ['SeqId','RelativeRecv'],
            # ['SeqId','RelativeSent']
           ]


num_metrics = len(plotList)
num_cols = min(3, num_metrics)
num_rows = math.ceil(num_metrics / num_cols)

fig, axes = plt.subplots(num_rows, num_cols, figsize=(5 * num_cols, 4 * num_rows))
axes = axes.flatten() if num_metrics > 1 else [axes]
fig.suptitle(f'{timestamp}\n{configStr}')
# plt.grid()
sns.set_style("whitegrid")

for i, (x,y) in enumerate(plotList):
    ax = axes[i]
    if('SeqId','DelayFreq') == (x,y):
        y = 'Delay'
        # ax.hist(df[y], bins=5, alpha=0.3, edgecolor='black', density=False)
        sns.histplot(data=df, x=y, stat="percent", ax=ax)
        ax.set_xlabel('Delay')
        ax.set_ylabel('Percent')
        ax.grid(True, zorder=0)
    elif ('Node', 'BarDelay') == (x,y):
        # https://seaborn.pydata.org/tutorial/error_bars.html
        sns.barplot(x="Node", y="Delay", data=df, errorbar='sd', ax=ax)
        ax.set_title("Average Delay per Node")
        ax.set_xlabel("Node")
        ax.tick_params(axis='x', labelrotation=90)
        ax.set_ylabel("Delay (s)")       
        ax.grid(True, zorder=0) 
    elif ('Node', 'BoxDelay') == (x,y):
        sns.boxplot(x = 'Node', y = 'Delay', data = df, ax=ax, linewidth=.75, fliersize=0.8) 
        ax.set_xlabel("Node")
        ax.tick_params(axis='x', labelrotation=90)
        ax.set_ylabel("Delay(s)")
        ax.set_title("Delay Variation per Node")
        ax.grid(True, zorder=0)
    elif ('HopCount', 'BoxDelay') == (x,y):
        sns.boxplot(x = 'HopCount', y = 'Delay', data = df, ax=ax, linewidth=.75, fliersize=0.8) 
        ax.set_xlabel("HopCount")
        ax.tick_params(axis='x', labelrotation=90)
        ax.set_ylabel("Delay(s)")
        ax.set_title("Delay Variation v/s HopCount")
        ax.grid(True, zorder=0)
    elif ('SeqId', 'NodeDelay') == (x,y):        
        ax.set_title("Nodewise Delay per SeqId")
        ax.set_xlabel("Node")
        ax.tick_params(axis='x', labelrotation=90)
        ax.set_ylabel("Cumulative Delay (s)")        
        pivot_df = df.pivot_table(index='Address', columns='SeqId', values='Delay', fill_value=0)
        pivot_df=pivot_df.sort_values(by='Address')
        pivot_df.plot(kind='bar', stacked=True, ax=ax)
        ax.legend(title='SeqId', bbox_to_anchor=(1.05, 1), loc='upper left', framealpha=0.5)
        ax.grid(True, zorder=0)
    elif ('HopCount', 'LostCount') == (x,y):            
        sns.boxplot(x=x, y=y, data = df_lost, ax=ax, linewidth=.75, fliersize=0.8)
        ax.set_title("Lost Packets v/s HopCount")
        ax.set_xlabel(x)
        ax.tick_params(axis='x', labelrotation=90)
        ax.set_ylabel("# of packets lost")
        ax.grid(True)
        totalLost = df_lost['LostCount'].sum()
        stats_text = f'Total = {totalLost}'
        ax.text(1.05, 0.5, stats_text, transform=ax.transAxes, fontsize=10,
                verticalalignment='center', bbox=dict(boxstyle='round,pad=0.3', edgecolor='black', facecolor='lightgrey'))
    elif ('Node', 'LostCount') == (x,y):       
        sns.barplot(x=x, y=y, data=df_lost, ax=ax)
        ax.set_title("Lost Packets v/s Node")
        ax.set_xlabel(x)
        ax.tick_params(axis='x', labelrotation=90)
        ax.set_ylabel("# of packets lost")
        ax.grid(True)
    elif ('Node', 'LossRate') == (x,y):       
        sns.barplot(x=x, y=y, data=df_lost, ax=ax)
        ax.set_title("Lost Rate v/s Node")
        ax.set_xlabel(x)
        ax.tick_params(axis='x', labelrotation=90)
        ax.set_ylabel("% of packets lost")
        ax.grid(True)
    elif ('HopCount', 'LossRate') == (x,y):            
        sns.boxplot(x=x, y=y, data = df_lost, ax=ax, linewidth=.75, fliersize=0.8)
        ax.set_title("Loss Rate v/s HopCount")
        ax.set_xlabel(x)
        ax.tick_params(axis='x', labelrotation=90)
        ax.set_ylabel("% of packets lost")
        ax.grid(True)
        totalLost = (df_lost['LostCount'].sum()/df_lost['TotalCount'].sum()).astype(float) * 100
        stats_text = f'Total = {totalLost:.2f} %'
        ax.text(1.05, 0.5, stats_text, transform=ax.transAxes, fontsize=10,
                verticalalignment='center', bbox=dict(boxstyle='round,pad=0.3', edgecolor='black', facecolor='lightgrey'))
    elif ('Node', 'AggDelay') == (x,y):
        # df['Delay'].plot(kind='box',ax=ax)
        sns.boxplot(y = 'Delay', data = df, ax=ax)
        ax.set_title("Variation in Aggregate Delay")
        ax.set_ylabel("Delay(s)")
        ax.set_xlabel("Aggregate")
        ax.grid(True, zorder=0)

        mean = df['Delay'].mean()
        median = df['Delay'].median()
        std_dev = df['Delay'].std()
        min_val = df['Delay'].min()
        max_val = df['Delay'].max()

        stats_text = (
            f'Mean: {mean:.2f} s\n'
            f'Median: {median:.2f} s\n'
            f'SD: {std_dev:.2f} s\n'
            f'Min: {min_val:.2f} s\n'
            f'Max: {max_val:.2f} s'
        )
        
        # Position the text on the plot
        ax.text(1.05, 0.5, stats_text, transform=ax.transAxes, fontsize=10,
                verticalalignment='center', bbox=dict(boxstyle='round,pad=0.3', edgecolor='black', facecolor='lightgrey'))
    # else:
    #     for (src, addr), grp in df.groupby(['Source', 'Address']):
    #         plt.title(f'{x} vs {y}')
    #         label = (f'{addr}→{src}')
    #         if ('SeqId','Delay') == (x,y):
    #             delay_avg = round(grp[y].mean(), 2)
    #             delay_median = round(grp[y].median(), 2)
    #             delay_min = round(grp[y].min(), 2)
    #             delay_max = round(grp[y].max(), 2)

    #             data_delay["Source"].append(src)
    #             data_delay["Address"].append(addr)
    #             data_delay["AvgDelay"].append(delay_avg)
    #             data_delay["MedianDelay"].append(delay_median)
    #             data_delay["MinDelay"].append(delay_min)
    #             data_delay["MaxDelay"].append(delay_max)
                
    #             label = (f'{addr}→{src}')
    #                     #f'Avg= {delay_avg}s\n'
    #                     #f'Med= {delay_median}s\n'
    #                     #f'Min= {delay_min}s\n'
    #                     #f'Max= {delay_max}s\n')
                
    #             # aggregates=(f'Aggregate Delay:\n'
    #             # f'Avg= {round(df[y].mean(), 2)} s\n'
    #             # f'Med= {round(df[y].median(), 2)} s\n'
    #             # f'Min= {round(df[y].min(), 2)} s\n'
    #             # f'Max= {round(df[y].max(), 2)} s\n')
                
    #         ax.plot(grp[x], grp[y], marker='.', label=label)
    #         ax.grid()
    #         # legend = ax.legend(title=f'{y}', bbox_to_anchor=(1, 0.5), loc='center left' )
    #         legend = ax.legend()
    #         legend.get_frame().set_alpha(0.0001)
    #         ax.set_xlabel(x)
    #         ax.set_ylabel(f'{y} (seconds)')
    #         ax.set_title(f'{x} vs {y} (seconds)')
    #         if ('SeqId','Delay') == (x,y):
    #             ax.text(0.01, 1.1, aggregates, wrap=True, transform=ax.transAxes)
            
       
for j in range(i + 1, len(axes)):
    fig.delaxes(axes[j])

plt.tight_layout()
fig.savefig(f'{saveDir}/benchmark.png')
df.to_csv(f'{saveDir}/results.csv')