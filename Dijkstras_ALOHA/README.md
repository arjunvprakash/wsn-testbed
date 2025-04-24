# Simple Tree Routing Protocol (STRP) & ProtoMon
A simple, but fully autonomous tree-based routing protocol in combination with an interface to visualize the network topology.

## Features
1. Complete autonomity
2. Neighbor discovery and routing table management
3. Routing table propogation to sink
4. Topology visualization on sink

## Usage
### Pre-requisites
1. Make sure python pip is installed on the sink
```bash
sudo apt update
sudo apt install python3-pip
# Required to fix the import error for numpy
sudo apt-get install libopenblas-dev
sudo apt-get install libopenjp2-7
pip --version
``` 

### Configuration
1. Set the sink address to `ADDR_SINK` in [common.h](common.h#L13)

### Execution
1. Login as the pi user on the nodes as well as the sink
1. Copy the project directory `AlohaRoute` to `/home/pi/sw_workspace/` 
2. Use the makefile to build the binary with the commands:
```bash
cd /home/pi/sw_workspace/AlohaRoute/
mkdir -p Debug
make -s && > Debug/output.txt
```
3. Run the generated binary using :
```bash
cd /home/pi/sw_workspace/AlohaRoute/Debug
bash -c './AlohaRoute <addr> # Address of the respective node/sink
```

### View the network topology
2. On the client machine, open an SSH tunnel to the sink port 8000 using the command:
```bash
ssh -L 8000:localhost:8000 pi@sink
```
3. On the client. open http://localhost:8000/ on the browser.

## Results Samples

![network_graph_2024-08-21 18%3A29%3A21](https://github.com/user-attachments/assets/c3171cee-83f8-456c-8e2f-d6f553f0e296)
![network_graph_2024-08-21 18%3A27%3A59](https://github.com/user-attachments/assets/9f58df68-840c-4bc7-974b-743b55e98aa7)
![network_graph_2nodes](https://github.com/user-attachments/assets/cf218bf2-39a8-45b8-9f5f-6d62913d972c)
![network_graph](https://github.com/user-attachments/assets/cbe31ae0-7998-4695-b664-abddb38d3f26)



## Functional Overview

![Node: State Diagram](https://github.com/user-attachments/assets/498ceb98-5ee0-46b6-a2e8-7865197b7c7a)

![Sink: State Diagram](https://github.com/user-attachments/assets/61f38c28-a1f6-45cd-a0e0-7a96af3caffd)

## Architecture

![Architecture](https://github.com/user-attachments/assets/2a767383-09fb-4f22-9d32-f471772b138a)
