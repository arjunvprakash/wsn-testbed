# Simple Mesh Routing Protocol (SMRP) & ProtoMon
A mesh-based routing protocol PoC in combination with an interface to visualize the network topology.

## Features
1. Gossiping based mesh
   > PoC version: Requires all nodes to within the communication range of each other
3. Neighbor discovery and routing table management
4. Routing table propogation to sink
5. Topology visualization on sink

## Usage
### Pre-requisites
1. Make sure python pip is installed on the sink
```bash
sudo apt update
sudo apt install python3-pip
# Required to fix the import error for numpy
sudo apt-get install libopenblas-dev
sudo apt-get install libopenjp2-7

# Additional steps - as required on Pi13 (use only in case of errors in GUI logs)
pip install -U pip setuptools
sudo apt-get install python3-venv

pip3 install numpy
python3 -m pip install pandas
# pip install pandas==2.2.3 # If above installation of pandas hangs/fails
# pip install Pillow==9.3.0 # In case of error "Building wheel for pillow (pyproject.toml) did not run successfully."

# For benchmark
pip install -r ../../benchmark/viz/requirements.txt
# For ProtoMon
pip install -r ../../ProtoMon/viz/requirements.txt
pip --version
``` 

### Configuration
1. Set the sink address to `ADDR_SINK` in [common.h](common.h#L26)

### Execution
1. Login as the pi user on all pis
1. Copy the project directory `SMRP_MACAW` to `/home/pi/sw_workspace/` 
2. Use the makefile to build the binary with the commands:
```bash
cd /home/pi/sw_workspace/SMRP_MACAW/
mkdir -p Debug
make -s && > Debug/output.txt
```
3. Run the generated binary using :
```bash
cd /home/pi/sw_workspace/SMRP_MACAW/Debug
bash -c './SMRP_MACAW <addr> # Address of the respective node
```

### View the network topology
2. On the client machine, open an SSH tunnel to the sink port 8000 using the command:
```bash
ssh -L 8000:localhost:8000 pi@<sink.address>
```
3. On the client. open http://localhost:8000/ on the browser.

## Results Samples

<img width="1903" height="1728" alt="SMRP_MACAW" src="https://github.com/user-attachments/assets/2a18833c-eb5a-47d6-99d1-9f920d0c93ab" />

### The configuration used for the above results:
| **Configuration** | **Value** | **Remarks** |
|:---|:---|:---|
| sleepDuration | *self \* 1000* | Application layer packet send frequency (ms) |
| vizIntervalS | 120 | ProtoMon_Config |
| loglevel | `INFO` | ProtoMon_Config |
| sendIntervalS | 60 | ProtoMon_Config |
| sendIntervalS | 20 | ProtoMon_Config |
| monitoredLevels | `PROTOMON_LEVEL_ROUTING` | ProtoMon_Config |
| initialSendWaitS | *self + 10* | ProtoMon_Config |
| beaconIntervalS | 33 | SMRP_Config |
| loglevel | INFO | SMRP_Config |
| nodeTimeoutS | 500 | SMRP_Config |
| senseDurationS | 15 | SMRP_Config |
| mac | MAC{ambient=0} | SMRP_Config |
| maxTries | 3 | SMRP_Config |
