# High-level design of ops-switchd
The ops-switchd daemon is responsible for driving the configuration from the database into the ASIC, in addition to reading the statuses and statistics from the ASIC and writing them to the database.
The ops-switchd daemon has three primary layers: the SDK independent code, the SDK plugin, and the ASIC SDK. SDK independent code reads the configuration from the database and pushes this configuration through the SDK plugin layer, which translates the configuration into ASIC SDK APIs. The SDK plugin layer is an extension of the "ofproto provider" and the "netdev provider" interfaces. The SDK plugin is compiled as a dynamically linked library, which is loaded at run time by ops-switchd.

## Reponsibilities
* Manages VRF configuration and L3 ports created as part of the VRF.
* Updates the ASIC with the route and nexthop configuration from the database.
* Updates the ASIC with the neighbor information from the database.
* Manages the VLAN creation/deletion in the ASIC.
* Manages the LAG creation/deletion in the ASIC.
* Manages the interface configuration in the ASIC.
* Gathers the interface stats from the ASIC and update the database.

##  Design choices
The ops-switchd daemon is a modified version of ovs-vswitchd from Open vSwitch. ovs-vswitchd was extended to support full configurability of the interfaces, introduce the concept of VRFs, route and nexthop management.

## Relationships to external OpenSwitch entities

```ditaa
+-----------------------+
|                       |
|         OVSDB         |
|                       |
+-----------^-----------+
            |
            |
            |
+-----------v-----------+
| SDK independent layer |
|                       |
+-----------------------+
|   SDK plugin layer    |
|                       | ops-switchd
+-----------------------+
|       ASIC SDK        |
|                       |
+-----------^-----------+
            |
            |
+-----------v-----------+
|         ASIC          |
|                       |
+-----------------------+
```
The ops-switchd daemon reads configuration from database and updates ASIC and reads statuses and statistics from ASIC and updates database. In OpenSwitch, the ops-switchd daemon is the only daemon that can talk to the ASIC.

## OVSDB-Schema
The following columns are read by the ops-switchd daemon:
```
  System:cur_cfg
  System:bridges
  System:vrfs
  System:system_mac
  System:ecmp_config
  System:other_config
  Subsystem:interfaces
  Bridge:name
  Bridge:ports
  Port:name
  Port:interfaces
  Port:tag
  Port:vlan_option
  Port:bond_option
  Port:hw_config
  Interface:name
  Interface:hw_intf_info
  VLAN:name
  VLAN:vid
  VLAN:enable
  VRF:name
  VRF:ports
  Neighbor:ip_address
  Neighbor:mac
  Neighbor:port
  Neighbor:vrf
  Nexthop:selected
  Nexthop:ip_address
  Nexthop:ports
  Route:selected
  Route:from
  Route:prefix
  Route:address_family
  Route:nexthops
```

The following columns are written by the `ops-switchd` daemon:
```
  Port:status
  Port:statistics
  Interface:statistics
  Interface:admin_state
  Interface:link_speed
  Interface:duplex
  Interface:mtu
  Interface:mac_in_use
  Interface:pause
  Neighbor:status
  Nexthop:status
```

## Code Design
* Initialization: Load the ASIC SDK plugins found in the plugins directory. Update the database with the values that are written to by the ops-switchd daemon.
* Main loop: The run functions of the various sub-modules are called from the main loop, including the sub-modules bridge, subsystem, bufmon, plugins, and netdev. The VRF and bridge handling are integrated and processed inside bridge code. The bridge looks at the VLAN table in the database to update the ASIC plugin with the updated VLAN information through the ofproto layer. LAG user configuration is read from the database and compared with the LAG status updated by lacpd and this information is sent to the ASIC through the bundle configuration APIs in bridge ofproto. Interface configuration and interface statistics collection are handled inside a subystem run through the netdev layer. The VRF is handled by creating a new ofproto class of type "vrf". This new ofproto class has APIs defined for L3 management, including L3 interface creation/deletion, neighbor management, route and nexthop management. The VRF code reads the route and nexthop information (with also support for ECMP) from the database and updates the ASIC with this configuration. The VRF reads the neighbor table from the database which in turn is driven by the Linux ARP table and updates ASIC with this information.

## References
* [openvswitch](http://www.openvswitch.org)
