# wsn-testbed

A framework to monitor and visualize parameters and topology in a WSN testbed.

### Features SUmmary
- Collects generic parameters such as packets sent/received, average latency
- Select protocols to be monitored (routing, MAC or both)
- Underlying protocols can define their own set of custom paramters
- Protocol parameters can be integrated into ProtoMon by implementing the required APIs 
- Visualization of collected metrics as timeseries line charts at the sink
- Customize the type of line chart using metric name prefixes : `Total`, `Agg` or `AggTotal`
- Plot the geographical distribution (node positions) by inputting : (i) a map image of the field area and (ii) the node position as coordinates of the image
- Customize node positions viz using custom configuration 

## Setup and Usage

To import the existing projects from the github repository () into the
locally installed testbed GUI, one can do a `git clone` into the
`sw_workspace` of the GUI. Make sure that the project directories are
directly in the `sw_workspace` and not nested within the `wsn-testbed`
or any other subdirectory.

For a protocol stack to setup and use the ProtoMon framework, there are
minimal code changes required in the project for each layer, as
described below.

#### Application layer:

The core activity to be done at the application layer is to enable the
monitoring of the underlying protocols by initializing ProtoMon.
Following steps are to be taken.

1.  Include the header files: `common.h` and `ProtoMon/ProtoMon.h`

2.  Set the monitoring levels and other configuration in a
    `ProtoMon_config` variable

3.  Invoke `ProtoMon_init` passing the `ProtoMon_config` variable to
    initialize ProtoMon. It is ideal to be done before initializating
    the routing and MAC protocols.

#### Routing and MAC layers:

The changes to be made are related to implementing the API functions of
ProtoMon. Following steps are to be taken.

1.  Include the header files: `common.h` and as well as either
    `ProtoMon/routing.h` or `ProtoMon/mac.h` for the routing or mac
    layer respectively.

2.  Implement the control, metrics and topology APIs. If there are no
    protocol-specific parameters being monitored, provide empty
    implementations corresponding to each of the respective API
    functions.

3.  Adapt and assign the send/receive functions of the protocol to the
    respective function pointers of ProtoMon send/receive API. For
    example in STRP :

    ```
    int (*Routing_sendMsg)(t_addr dest, uint8_t *data, unsigned int len) = STRP_sendMsg;
    int (*Routing_recvMsg)(Routing_Header *h, uint8_t *data) = STRP_recvMsg;
    int (*Routing_timedRecvMsg)(Routing_Header *h, uint8_t *data, unsigned int timeout) = STRP_timedRecvMsg;
    ```

After these few changes, ProtoMon would be successfully integrated into
the project to monitor the routing and MAC protocols. Simply commenting
out the ProtoMon initialize statement would deactivate the monitoring
and no other change is required for it.

## Architecture

<img width="836" height="633" alt="ProtoMon Architecture" src="https://github.com/user-attachments/assets/e8db923d-600d-432a-a1b0-cf59686e8a43" />

## ProtoMon Interfaces

<img width="841" height="615" alt="ProtoMon Interfaces" src="https://github.com/user-attachments/assets/9ed7db2c-5552-486f-98d6-41077521d7fc" />

## Implementation Overview
<img width="839" height="1681" alt="Overview" src="https://github.com/user-attachments/assets/9e84939a-0e96-42c4-ba8e-9134cf5a1cc1" />


