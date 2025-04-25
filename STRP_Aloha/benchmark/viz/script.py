import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime
from io import StringIO
import argparse
import os

def calculateRelativeTime(df, timestamp_col='Timestamp', relative_time_col='RelativeTime'):
    df[timestamp_col] = pd.to_datetime(df[timestamp_col], unit='ms')
    df['RecvTimestamp'] = pd.to_datetime(df['RecvTimestamp'], unit='ms')
    df['SentTimestamp'] = pd.to_datetime(df['SentTimestamp'], unit='ms')
    min_timestamp = df[timestamp_col].min()
    
    # min_timestamp = pd.to_datetime(startTimeMs, unit='ms')
    #print(min_timestamp)
    # min_timestamp = min_timestamp - pd.Timedelta(milliseconds=1000)
    
    df[relative_time_col] = (df['RecvTimestamp'] - min_timestamp).dt.total_seconds()
    return df.sort_values(by=relative_time_col)

def parseArgs():
    parser = argparse.ArgumentParser(description='Generate benchmark visualization')
    parser.add_argument('startTimeMs', metavar='startTimeMs', type=int, help='Start time in milliseconds')
    return parser.parse_args()

# main

# data_csv = StringIO(data)

# args = parseArgs()
# startTimeMs = args.startTimeMs

saveDir = "benchmark"
recv_csv = f'{saveDir}/recv.csv'
dt = datetime.now() 
timestamp = dt.strftime("%Y-%m-%d %H:%M:%S")

df = pd.read_csv(recv_csv)
df = calculateRelativeTime(df)

df['Delay'] = (df['RecvTimestamp'] - df['SentTimestamp']).dt.total_seconds()
df['RelativeRecv'] = (df['RecvTimestamp'] - df['SentTimestamp'].min())/1000
df['RelativeSent'] = (df['SentTimestamp'] - df['SentTimestamp'].min())/1000

df['key'] = df.apply(lambda row: tuple([row['Source'], row['Address']]), axis=1)

#print(df)
#for key, grp in df.groupby(['key']):
#    plt.plot(grp['SeqId'], grp['Delay'] / 1000, marker='.', label=key)

(x,y) = ['SeqId','Delay']
plt.figure(figsize=(10, 6))

for (src, addr), grp in df.groupby(['Source', 'Address']):
    #grp[y] = grp[y]/1000
    delay_avg = round(grp[y].mean(), 2)
    delay_median = round(grp[y].median(), 2)
    delay_min = round(grp[y].min(), 2)
    delay_max = round(grp[y].max(), 2)
    label = (f'{addr}→{src}\n'
             f'Avg: {delay_avg} s\n'
             f'Median: {delay_median} s\n'
             f'Min: {delay_min} s\n'
             f'Max: {delay_max} s\n')
    plt.plot(grp[x], grp[y], marker='.', label=label)
plt.grid()
plt.legend(title=f'{y}', bbox_to_anchor=(1, 0.5), loc='center left' )
plt.xlabel(x)
plt.ylabel(f'{y} (seconds)')
plt.title(f'{x} vs {y} (seconds)')
aggregates=(f'Aggregate Delay:\n'
           f'Avg: {round(df[y].mean(), 2)} s\n'
           f'Median: {round(df[y].median(), 2)} s\n'
           f'Min: {round(df[y].min(), 2)} s\n'
           f'Max: {round(df[y].max(), 2)} s\n'
           )
plt.text(1.01, 1.03, aggregates, transform=plt.gca().transAxes, fontsize=10,ha='left', va='center', wrap=True)
plt.tight_layout()
# plt.show()
plt.savefig(f'{saveDir}/msg_delay.png')
plt.savefig(f'{saveDir}/msg_delay_{timestamp}.png')

(x,y) = ['SeqId','RelativeTime']
plt.figure(figsize=(10, 6))
for (src, addr), grp in df.groupby(['Source', 'Address']):
    label = (f'{addr}→{src}')
    plt.plot(grp[x], grp[y], marker='.', label=label)
plt.grid()
plt.legend(title=f'{y}', bbox_to_anchor=(1, 0.5), loc='center left' )
plt.xlabel(x)
plt.ylabel(f'{y}')
plt.title(f'{x} vs {y} (seconds)')
plt.tight_layout()
# plt.show()
plt.savefig(f'{saveDir}/msg_time.png')
plt.savefig(f'{saveDir}/msg_time{timestamp}.png')

# df['Height'] = df['RelativeRecv'] - df['RelativeSent']
# (x,y) = ['SeqId', 'RelativeSent']
# plt.figure(figsize=(10, 6))
# for (src, addr,seqId), grp in df.groupby(['Source', 'Address', 'SeqId']):
    
#     label = (f'{addr}→{src}: {seqId}')
#     y = grp['SeqId'].astype(str)  # Convert SeqId to string for plotting
#     left = grp['RelativeSent'] # Use RelativeSent for the bottom of the candle
#     x = grp['Height']  # Height of the candle

#     # Create bars
#     plt.barh(y, x, left=left, label=f'Source: {src}, Address: {addr}', alpha=0.5)
# plt.grid()
# plt.legend(title=f'{y}', bbox_to_anchor=(1.05, 1), )
# plt.xlabel(x)
# plt.ylabel(f'{y}')
# plt.title(f'{x}(seconds)')
#plt.tight_layout()
# plt.show()