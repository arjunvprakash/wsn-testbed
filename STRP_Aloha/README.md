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
1. Copy the project directory `STRP_ALOHA` to `/home/pi/sw_workspace/` 
2. Use the makefile to build the binary with the commands:
```bash
cd /home/pi/sw_workspace/AlohaRoute/
mkdir -p Debug
make -s && > Debug/output.txt
```
3. Run the generated binary using :
```bash
cd /home/pi/sw_workspace/STRP_ALOHA/Debug
bash -c './STRP_ALOHA <addr> # Address of the respective node
```

### View the network topology
2. On the client machine, open an SSH tunnel to the sink port 8000 using the command:
```bash
ssh -L 8000:localhost:8000 pi@<sink.address>
```
3. On the client. open http://localhost:8000/ on the browser.

## Results Samples

<img width="1903" height="2751" alt="STRP_ALOHA" src="https://github.com/user-attachments/assets/89a43896-e07f-44fc-9d9e-150097921941" />
