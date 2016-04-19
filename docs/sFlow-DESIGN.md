# sFlow Design

- [Overview](#overview)
- [High level design](#high-level-design)
    - [Opennsl plugin](#opennsl-plugin)
    - [Docker container plugin](#docker-container-plugin)
- [DB schema](#db-schema)
- [Code design](#code-design)
- [References](#references)

## Overview

sFlow is a real-time packet sampling technology that can forward sampled packet
data from all the ports in a network device to an sFlow data collector.
OpenSwitch implements the sFlow version 5 (sFlow v5) agent software that
forwards the data to the sFlow collector. Along with the sampled packets, the
sFlow agent software also periodically forwards interface statistics to the
collector.


## High-level design

The sFlow agent in OpenSwitch is responsible for sending the sampled data
packets and the interface statistics to the collector. The ASIC SDK plugins
configure sFlow sampling in the hardware and converts the sampled packets
coming from the hardware into sFlow datagrams which are then sent to the
collector. The ASIC SDK plugins also periodically polls the interface
statistics from the hardware and sends these statistics to the collector as
sFlow datagrams.

```ditaa


                                          +----------------------+
                                          |       database       |
                                     +---->                      |
         ops-switchd                 |    +----------------------+
                                     |
         +-----------------------+   |
         |                       |   | sFlow sample counters
         |                       |   |
         |     SDK independent   +---+
         |     layer             |
         |                       |
         |                       |
         +-----------------------+
         |                       |
         |                       +------------------------------------>
         |     SDK plugin        +---------+      sFlow interface
         |     layer             |         |      statistics to the collector
         |                       |         |
         |                       |         +--------------------------->
         +-----------^-----------+            sFlow packet samples to the
                     |                        collector
                     |
                     |  sFlow statistics
                     |  and packet samples
                     |
         +-----------------------+
         |                       |
         |      hardware         |
         |                       |
         +-----------------------+
```

### OpenNSL plugin

The `ops-switchd` daemon configures the Open Network Switch Library (OpenNSL)
compliant hardware with the sFlow configuration through the OpenNSL plugin
layer. OpenNSL APIs are used to configure the same sampling rate for both
ingress and egress packets on a per-interface basis. sFlow packets reach an rx
callback function in the OpenNSL plugin which uses the OVS sflow library to
send the packets to the collector. Interface counter statistics can be polled
with the OpenNSL APIs for statistics.

### Docker container plugin
The `ops-switchd` daemon configures sFlow on the container software using the
container plugin layer. The container plugin configures sFlow on the L2
interfaces by translating the sFlow configuration from the OpenSwitch database
into the sFlow configuration on the "ASIC" OVS database. This configuration is
then applied on all the OVS bridges which in-turn applies it on all the
interfaces(L2) under each bridge. For sampling on L3 interfaces, `Host sFlow`
agent is run on OpenSwitch and the configuration from database is converted to
`Host sFlow` configuration by writing to the `/etc/hsflowd.conf` file and
iptable NFLOG rules. `Host sFlow` agent also periodically sends Linux interface
statistics to the configured collector.

```ditaa


               +-------------------------+
               |  Container plugin       |
               |                         |
               +-------------------------+
                   |              |
                   |              |configure host sFlow
                   |              |agent and iptables
                   |              |with sFlow configuration.
   configure       |         +----v------+
   openvswitch-sim |         |           |
   database with   |         |Host sFlow +-----------> sampled L3 packets
   sFlow configs   |         | agent     |             to collector.
                   |         |           |
                   |         +-----------+
                   |            |. . .| L3 interfaces
                +--v--------+   +     +
                |           |
                |openvswitch|------------------------> sampled L2 packets
                |  -sim     |                          to collector.
                |           |
                +-----------+
                  |     |
                  |. . .|L2 interfaces
                  +     +

```

## DB schema
The following columns are read by sFlow:
```
Subsystem: sFlow reference
sFlow: All the columns in the table.
```
The following columns are written by sFlow:
```
Interface: statistics
```

## Code design

The bridge (`bridge.c`) in `ops-switchd` sends the global sFlow configuration
from the subsystem table to the hardware plugins through the
`ofproto->set_sflow()` API. This API is called for each bridge and VRF in the
system. The plugins receives sampled packets from the hardware in an rx
callback function. These sampled packets are sent to the collector by using the
`sflow` library in `ops-switchd`. This library must be configured with the
collector details, and the library then takes care of the packaging and sending
the sampled packets to the collector as sFlow datagrams. The `run()` function
of the plugin would periodically poll the hardware for the interface statistics
which are then sent to collector using the `sflow` library in `ops-switchd`.
The container plugin on OpenSwitch uses "ASIC" OVS and Host sFlow agent to
sample the packets.  `ops-switchd` periodic timer would poll the plugins for
the statistics about the number of samples sent to the collector which are then
published into the database.

## References

- [sFlow v5](http://www.sflow.org/sflow_version_5.txt)
- [Open vSwitch](http://openvswitch.org/)
- [Host sFlow agent](http://www.sflow.net/)
- [iptables nflog](http://ipset.netfilter.org/iptables-extensions.man.html)
- [ethtool](http://linux.die.net/man/8/ethtool)
