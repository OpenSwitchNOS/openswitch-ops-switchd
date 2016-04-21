sFlow Feature Test Cases
========================

## Contents

- [Global sFlow functionality](#global-sflow-functionality)
- [Multiple sFlow collectors](#multiple-sflow-collectors)
- [Changing agent interface](#changing-agent-interface)
- [Changing global sampling rate](#changing-global-sampling-rate)
- [Changing polling interval](#changing-polling-interval)
- [Changing per interface sampling](#changing-per-interface-sampling)

## Global sFlow functionality
### Objective
This test verifies the global sFlow functionality on the switch.
### Requirements
- Physical switch/workstations test setup
- **FT File**: `ops-switchd/ops-tests/feature/test_sflow_ft_functionality.py`
### Setup
#### Topology diagram
```ditaa
                    +----------------+
                    |                |
                    |                |
                    |   sflowtool    |
                    |                |
                    +-+--------------+
                      |
                      |
         +------------+--+
         |               |
         |               |
         |    Switch     |
         |               |
         |               |
         |               |
         +-+----------+--+
           |          |
           |          |
+----------+--+     +-+------------+
|             |     |              |
|             |     |              |
|  Host-1     |     |  Host-2      |
|             |     |              |
+-------------+     +--------------+
```
### Description
This test is used to ensure that the global sFlow feature works on OpenSwitch.

1. Connect Host-1 to the switch. Configure IP address and route.
    ```
    ip addr add 10.10.10.2/24 dev eth1
    ip route add 10.10.11.0/24 via 10.10.10.1
    ```

2. Connect Host-2 to the switch. Configure IP address and route.
    ```
    ip addr add 10.10.11.2/24 dev eth1
    ip route add 10.10.10.0/24 via 10.10.11.1
    ```

3. Connect another host which has sflowtool (the sFlow collector) to the switch. Configure an IP address on it.
    ```
    ip addr add 10.10.12.2/24 dev eth1
    ```

4. Configure the interfaces on the switch that are connected to these hosts.
    ```
    switch# configure terminal
    switch(config)# interface 1
    switch(config-if)# ip address 10.10.10.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 2
    switch(config-if)# ip address 10.10.11.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 3
    switch(config-if)# ip address 10.10.12.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)#
    ```

5. Enable the global sFlow feature on the switch.
    ```
    switch(config)# sflow enable
    ```

6. Configure the collector IP address (attached to interface 2 of the switch).
   Set the agent-interface (interface 3) and the sampling rate at 20 (One in 20 packets are sampled)
    ```
    switch(config)# sflow collector 10.10.12.2
    switch(config)# sflow agent-interface 3
    switch(config)# sflow sampling 20
    ```

7. Ping between Host-1 and the switch (CPU destined traffic). The sflowtool is able to see FLOW packets from the switch.
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

8. Ping between Host-1 and Host-2 (Layer-3 traffic). The sflowtool is able to see FLOW packets from the switch.
    ```
    ping 10.10.11.2 -c 200 -i 0.1
    ```

9. Disable the global sFlow feature on the switch.
    ```
    switch(config)# no sflow enable
    ```

10. Ping again and check if the sflowtool does not receive the sFlow packets from the switch.

11. Enable sFlow again to test Layer-2 traffic.
    ```
    switch(config)# sflow enable
    ```

12. Add a new Layer-2 VLAN to the switch. Configure the interfaces connected to the hosts to become a part of this VLAN in access mode.
    ```
    switch(config)# vlan 10
    switch(config-vlan)# no shut
    switch(config-vlan)# exit
    switch(config)# interface 1
    switch(config-if)# no routing
    switch(config-if)# vlan access 10
    switch(config-if)# exit
    switch(config)# interface 2
    switch(config-if)# no routing
    switch(config-if)# vlan access 10
    switch(config-if)# exit
    ```

13. Reconfigure IP address on Host-2 to be on the same subnet as Host-1.
    ```
    ip addr del 10.10.11.2/24 dev eth1
    ip addr add 10.10.10.3/24 dev eth1
    ```

13. Ping between the hosts (Layer-2 traffic). The sflowtool is able to see FLOW packets from the switch.
    ```
    ping 10.10.11.2 -c 200 -i 0.1
    ```

### Test result criteria
#### Test pass criteria
- When sFlow is enabled, the sflowtool is able to see the sFlow packets that are sent from the switch.
- When sFlow is disabled, the sflowtool does not see any sFlow packets from the switch.

#### Test fail criteria
- When enabled, the sflowtool does not receive any sFlow packets from the switch.
- When disabled, the sflowtool is still able to receive the sFlow packets from the switch.

## Multiple sFlow collectors
### Objective
This test verifies that the multiple sFlow collector configuration works on the switch.
### Requirements
- Physical switch/workstations test setup
- **FT File**: `ops-switchd/ops-tests/feature/test_sflow_ft_multiple_collectors.py`
### Setup
#### Topology diagram
```ditaa
+----------------+      +----------------+
|                |      |                |
|                |      |                |
|  sflowtool-3   |      |  sflowtool-2   |
|                |      |                |
+--------------+-+      +-+--------------+
               |          |
               |          |
             +-+----------+--+
             |               |
             |               |
             |    Switch     |
             |               |
             |               |
             |               |
             +-+----------+--+
               |          |
               |          |
       +-------+--+     +-+--------------+
       |          |     |                |
       |          |     |                |
       | Host     |     |  sflowtool-1   |
       |          |     |                |
       +----------+     +----------------+
```
### Description
This test is used to ensure that the sFlow feature on OpenSwitch works with multiple collectors.

1. Connect Host to the switch interface 1 and configure an IP address.
    ```
    ip addr add 10.10.10.2/24 dev eth1
    ```

2. Connect the sFlow collectors sflowtool-1, 2, and 3 to the switch interfaces 2, 3, and 4 respectively. Configure IP addresses on them.
    ```
    ip addr add 10.10.11.2/24 dev eth1
    ip addr add 10.10.12.2/24 dev eth1
    ip addr add 10.10.13.2/24 dev eth1
    ```

3. Configure the interfaces on the switch that are connected to the host and the sFlow collectors.
    ```
    switch# configure terminal
    switch(config)# interface 1
    switch(config-if)# ip address 10.10.10.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 2
    switch(config-if)# ip address 10.10.11.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 3
    switch(config-if)# ip address 10.10.12.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 4
    switch(config-if)# ip address 10.10.13.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)#
    ```

4. Enable the global sFlow feature on the switch.
    ```
    switch(config)# sflow enable
    ```

5. Configure the IP address of all three sFlow collectors.
   Set the agent-interface (interface 4) and the sampling rate at 20 (One in 20 packets are sampled)
    ```
    switch(config)# sflow collector 10.10.11.2
    switch(config)# sflow collector 10.10.12.2
    switch(config)# sflow collector 10.10.13.2
    switch(config)# sflow agent-interface 4
    switch(config)# sflow sampling 20
    ```

6. Ping between the Host and the switch. All the sFlow collectors should be able to see the FLOW packets from the switch.
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

### Test result criteria
#### Test pass criteria
- When sflow is enabled, all the sFlow collectors receive sFlow packets from the switch.

#### Test fail criteria
- When sFlow is enabled, none of the sFlow collectors receive sFlow packets from the switch.

## Changing agent interface
### Objective
This test verifies that the configured agent interface is used by the sFlow packets.
### Requirements
- Physical switch/workstations test setup
- **FT File**: `ops-switchd/ops-tests/feature/test_sflow_ft_agent_interface.py`
### Setup
#### Topology diagram
```ditaa
                    +----------------+
                    |                |
                    |                |
                    |   sflowtool    |
                    |                |
                    +-+--------------+
                      |
                      |
         +------------+--+
         |               |
         |               |
         |    Switch     |
         |               |
         |               |
         |               |
         +-+-------------+
           |
           |
+----------+--+
|             |
|             |
|  Host-1     |
|             |
+-------------+
```
### Description
This test is used to ensure that the agent interface configured is used by the OpenSwitch.

1. Connect Host-1 to the switch. Configure IP address and route.
    ```
    ip addr add 10.10.10.2/24 dev eth1
    ip route add 10.10.11.0/24 via 10.10.10.1
    ```

2. Connect another host which has sflowtool (the sFlow collector) to the switch. Configure an IP address on it.
    ```
    ip addr add 10.10.11.2/24 dev eth1
    ```

3. Configure the interfaces on the switch that are connected to these hosts.
    ```
    switch# configure terminal
    switch(config)# interface 1
    switch(config-if)# ip address 10.10.10.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 2
    switch(config-if)# ip address 10.10.11.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 3
    switch(config-if)# ip address 10.10.12.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    ```

4. Enable the global sFlow feature on the switch.
    ```
    switch(config)# sflow enable
    ```

5. Configure the collector IP address (attached to interface 2 of the switch).
   Set the agent-interface (interface 1) and the sampling rate at 20 (One in 20 packets are sampled)
    ```
    switch(config)# sflow collector 10.10.11.2
    switch(config)# sflow agent-interface 1
    switch(config)# sflow sampling 20
    ```

6. Ping between Host-1 and the switch. The sflowtool is able to see FLOW packets from the switch with the agent address 10.10.10.1 (IP address of interface 1).
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

7. Change the agent-interface to use interface 3.
    ```
    switch(config)# sflow agent-interface 3
    ```

8. Ping between Host-1 and the switch. The sflowtool is able to see FLOW packets from the switch with the new agent address as 10.10.12.1 (IP address of interface 3).
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

9. Remove the agent interface configuration.
   ```
   switch(config)# no sflow agent-interface
   ```

10. Ping between Host-1 and the switch. The sFlow datagram will use one of the IP addresses configured on the switch. The sflowtool is able to see FLOW packets with an agent IP address that is available on the switch.
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

### Test result criteria
#### Test pass criteria
- When sFlow is enabled, the agent IP address in the FLOW packets match the configured agent interface's IP address.
- When agent interface configuration is removed, the agent IP address in the FLOW packets must be one of the IP addresses on the switch.

#### Test fail criteria
- When sFlow is enabled, the agent IP address in the FLOW packets does not match the configured agent interface's IP address.
- When agent interface configuration is removed, the agent IP address in the FLOW packets does not match any of the IP addresses on the switch.

## Changing global sampling rate
### Objective
This test verifies that the configured global sampling rate is used by the sFlow packets.
### Requirements
- Physical switch/workstations test setup
- **FT File**: `ops-switchd/ops-tests/feature/test_sflow_ft_sampling_rate.py`
### Setup
#### Topology diagram
```ditaa
                    +----------------+
                    |                |
                    |                |
                    |   sflowtool    |
                    |                |
                    +-+--------------+
                      |
                      |
         +------------+--+
         |               |
         |               |
         |    Switch     |
         |               |
         |               |
         |               |
         +-+-------------+
           |
           |
+----------+--+
|             |
|             |
|  Host-1     |
|             |
+-------------+
```
### Description
This test is used to ensure that the sampling rate configured is used by the OpenSwitch.

1. Connect Host-1 to the switch. Configure IP address and route.
    ```
    ip addr add 10.10.10.2/24 dev eth1
    ip route add 10.10.11.0/24 via 10.10.10.1
    ```

2. Connect another host which has sflowtool (the sFlow collector) to the switch. Configure an IP address on it.
    ```
    ip addr add 10.10.11.2/24 dev eth1
    ```

3. Configure the interfaces on the switch that are connected to these hosts.
    ```
    switch# configure terminal
    switch(config)# interface 1
    switch(config-if)# ip address 10.10.10.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 2
    switch(config-if)# ip address 10.10.11.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    ```

4. Enable the global sFlow feature on the switch.
    ```
    switch(config)# sflow enable
    ```

5. Configure the collector IP address (attached to interface 2 of the switch).
   Set the agent-interface (interface 1) and the sampling rate at 10 (One in 10 packets are sampled)
    ```
    switch(config)# sflow collector 10.10.11.2
    switch(config)# sflow agent-interface 1
    switch(config)# sflow sampling 10
    ```

6. Ping between Host-1 and the switch. The sflowtool is able to see FLOW packets from the switch.
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

7. Change the sampling-rate to 30.
    ```
    switch(config)# sflow sampling 30
    ```

8. Ping between Host-1 and the switch. The sflowtool is able to see FLOW packets from the switch.
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

### Test result criteria
#### Test pass criteria
- When sFlow is enabled, the sampling rate in the show sFlow CLI output must match the configured sampling rate.
- We must see atleast 50% flow packets at the collector.
- When sampling rate is changed, the sampling rate in the show sFlow CLI output must match the new sampling rate.
- We must see atleast 50% flow packets at the collector.

#### Test fail criteria
- When sFlow is enabled, the sampling rate in the show sFlow CLI output does not match the configured sampling rate.
- We do not see 50% flow packets at the collector.

## Changing polling interval
### Objective
This test verifies that the configured polling interval is used by the sFlow packets.
### Requirements
- Physical switch/workstations test setup
- **FT File**: `ops-switchd/ops-tests/feature/test_sflow_ft_polling_interval.py`
### Setup
#### Topology diagram
```ditaa
                    +----------------+
                    |                |
                    |                |
                    |   sflowtool    |
                    |                |
                    +-+--------------+
                      |
                      |
         +------------+--+
         |               |
         |               |
         |    Switch     |
         |               |
         |               |
         |               |
         +-+-------------+
           |
           |
+----------+--+
|             |
|             |
|  Host-1     |
|             |
+-------------+
```
### Description
This test is used to ensure that the polling interval configured is used by the OpenSwitch.

1. Connect Host-1 to the switch. Configure IP address and route.
    ```
    ip addr add 10.10.10.2/24 dev eth1
    ```

2. Connect another host which has sflowtool (the sFlow collector) to the switch. Configure an IP address on it.
    ```
    ip addr add 10.10.11.2/24 dev eth1
    ```

3. Configure the interfaces on the switch that are connected to these hosts.
    ```
    switch# configure terminal
    switch(config)# interface 1
    switch(config-if)# ip address 10.10.10.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 2
    switch(config-if)# ip address 10.10.11.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    ```

4. Enable the global sFlow feature on the switch.
    ```
    switch(config)# sflow enable
    ```

5. Configure the collector IP address (attached to interface 2 of the switch).
   Set the agent-interface (interface 1) and the sampling rate at 10 and polling interval to 10.
    ```
    switch(config)# sflow collector 10.10.11.2
    switch(config)# sflow agent-interface 1
    switch(config)# sflow sampling 10
    switch(config)# sflow polling 10
    ```

6. Ping between Host-1 and the switch.
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

7. Change the polling interval to default(30).
    ```
    switch(config)# no sflow polling
    ```

8. Ping between Host-1 and the switch.
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

### Test result criteria
#### Test pass criteria
- When sFlow is enabled, the polling interval in the show sflow command must match the configured polling interval.
- When the polling interval is increased(30) ,the new polling interval muct match the one in the show sflow command.
- The CNTR packet count with lower polling rate(10) must be greater than the one with higher polling rate(30).
- The number of interfaces in CNTR packets must be greater than or equal to 2 and the agent address must be same as the one configured.

#### Test fail criteria
- When sFlow is enabled, the polling interval in the show sflow command does not match the configured polling interval.
- The CNTR packet count with lower polling rate(10) is less than the one with higher polling rate(30).
- The number of interfaces in CNTR packets is less than 2 or agent interface does not match the configured agent interface.

## Changing per interface sampling
### Objective
This test verifies that the configured per interface sampling works on sFlow packets.
### Requirements
- Physical switch/workstations test setup
- **FT File**: `ops-switchd/ops-tests/feature/test_sflow_ft_per_interface_sampling.py`
### Setup
#### Topology diagram
```ditaa
                    +----------------+
                    |                |
                    |                |
                    |   sflowtool    |
                    |                |
                    +-+--------------+
                      |
                      |
         +------------+--+
         |               |
         |               |
         |    Switch     |
         |               |
         |               |
         |               |
         +-+-----------+-+
           |           |
           |           |
+----------+--+     +--+----------+
|             |     |             |
|             |     |             |
|  Host-1     |     |  Host-3     |
|             |     |             |
+-------------+     +-------------+
```
### Description
This test is used to ensure that the polling interval configured is used by the OpenSwitch.

1. Connect Host-1 to the switch. Configure IP address and route.
    ```
    ip addr add 10.10.10.2/24 dev eth1
    ip route add 10.10.12.0/24 via 10.10.10.1
    ```

2. Connect another host which has sflowtool (the sFlow collector) to the switch. Configure an IP address on it.
    ```
    ip addr add 10.10.11.2/24 dev eth1
    ```

3. Connect Host-3 to the switch. Configure IP address and route.
    ```
    ip addr add 10.10.12.2/24 dev eth1
    ip route add 10.10.10.0/24 via 10.10.10.1
    ```

4. Configure the interfaces on the switch that are connected to these hosts.
    ```
    switch# configure terminal
    switch(config)# interface 1
    switch(config-if)# ip address 10.10.10.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 2
    switch(config-if)# ip address 10.10.11.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    switch(config)# interface 3
    switch(config-if)# ip address 10.10.12.1/24
    switch(config-if)# no shut
    switch(config-if)# exit
    ```

5. Enable the global sFlow feature on the switch.
    ```
    switch(config)# sflow enable
    ```

6. Configure the collector IP address (attached to interface 2 of the switch).
   Set the agent-interface (interface 1) and the sampling rate at 10 and polling interval to 10.
    ```
    switch(config)# sflow collector 10.10.11.2
    switch(config)# sflow agent-interface 1
    switch(config)# sflow sampling 10
    switch(config)# sflow polling 10
    ```

7. Disable sflow on interface 1,2 and 3.
   ```
   switch(config)# interface 1
   switch(config-if)# no sflow enable
   switch(config)# interface 2
   switch(config-if)# no sflow enable
   switch(config)# interface 3
   switch(config-if)# no sflow enable
   ```

8. Ping between Host-1 and the switch. We do not see any FLOW packets.
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ```

7. Enable sflow on interface 1.
    ```
    switch(config)# interface 1
    switch(config-if)# sflow enable
    ```

8. Ping between Host-1 and the switch and Host-2 and the switch. We only see FLOW packets form interface 1.
    ```
    ping 10.10.10.1 -c 200 -i 0.1
    ping 10.10.12.1 -c 200 -i 0.1
    ```

### Test result criteria
#### Test pass criteria
- When per interface sFlow is disabled, the show sflow interface command must show disabled.
- When the per interface sFlow is disabled on all interfaces no FLOW packets must be seen.
- When sFlow is enabled on a single interface, the show sflow interface command must show enabled.
- When sFlow is enabled on a single interface we must see flow packets only from that interface.

#### Test fail criteria
- When per interface sFlow is disbled, the show sflow interface command still shows enabled.
- When the per interface sFlow is disabled on all interfaces FLOW packets are still present.
- When sFlow is enabled on a single interface, the show sflow interface command still shows disabled.
- When sFlow is enabled on a single interface we must see flow packets only from other interfaces too.
