# -*- coding: utf-8 -*-
#
# Copyright (C) 2015 Hewlett Packard Enterprise Development LP
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

"""
OpenSwitch Test for sFlow feature with multiple collectors.
"""

import time
import sflow_utils
from pytest import mark


TOPOLOGY = """
#+----------------+      +----------------+
#|                |      |                |
#|    Host-4      |      |    Host-3      |
#|  (sflowtool-3) |      |  (sflowtool-2) |
#|                |      |                |
#+--------------+-+      +-+--------------+
#               |          |
#               |          |
#             +-+----------+--+
#             |               |
#             |               |
#             |    Open       |
#             |    Switch     |
#             |               |
#             |               |
#             +-+----------+--+
#               |          |
#               |          |
#       +-------+--+     +-+--------------+
#       |          |     |                |
#       |          |     |    Host-2      |
#       | Host-1   |     |  (sflowtool-1) |
#       |          |     |                |
#       +----------+     +----------------+

# Nodes
[type=openswitch name="OpenSwitch"] ops1
[type=host name="Host 1"] hs1
[type=host name="Host 2" image="openswitch/sflowtool:latest"] hs2
[type=host name="Host 3" image="openswitch/sflowtool:latest"] hs3
[type=host name="Host 4" image="openswitch/sflowtool:latest"] hs4

# Links
hs1:1 -- ops1:1
hs2:1 -- ops1:2
hs3:1 -- ops1:3
hs4:1 -- ops1:4
"""


@mark.gate
def test_sflow_ft_multiple_collectors(topology, step):
    """
    Test sflow is able to send packets to multiple collectors
    """
    ops1 = topology.get('ops1')
    hs1 = topology.get('hs1')
    hs2 = topology.get('hs2')
    hs3 = topology.get('hs3')
    hs4 = topology.get('hs4')

    assert ops1 is not None
    assert hs1 is not None
    assert hs2 is not None
    assert hs3 is not None
    assert hs4 is not None

    ping_count = 200
    ping_interval = 0.1
    sampling_rate = 20
    p4 = ops1.ports['4']

    # Configure host interfaces
    step("### Configuring host interfaces ###")
    hs1.libs.ip.interface('1', addr='10.10.10.2/24', up=True)
    hs2.libs.ip.interface('1', addr='10.10.11.2/24', up=True)
    hs3.libs.ip.interface('1', addr='10.10.12.2/24', up=True)
    hs4.libs.ip.interface('1', addr='10.10.13.2/24', up=True)

    # Configure interfaces on the switch
    step("Configuring interfaces of the switch")
    with ops1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.ip_address('10.10.10.1/24')
        ctx.no_shutdown()

    with ops1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.ip_address('10.10.11.1/24')
        ctx.no_shutdown()

    with ops1.libs.vtysh.ConfigInterface('3') as ctx:
        ctx.ip_address('10.10.12.1/24')
        ctx.no_shutdown()

    with ops1.libs.vtysh.ConfigInterface('4') as ctx:
        ctx.ip_address('10.10.13.1/24')
        ctx.no_shutdown()

    # Configure sFlow
    step("### Configuring sFlow ###")
    with ops1.libs.vtysh.Configure() as ctx:
        ctx.sflow_enable()
        ctx.sflow_sampling(sampling_rate)
        ctx.sflow_agent_interface(p4)
        ctx.sflow_collector('10.10.11.2')
        ctx.sflow_collector('10.10.12.2')
        ctx.sflow_collector('10.10.13.2')

    ops1('show sflow')

    collector_default_port = '6343'
    collector_default_vrf = 'vrf_default'
    collectors = []
    collector_1 = {}
    collector_1['ip'] = '10.10.11.2'
    collector_1['port'] = collector_default_port
    collector_1['vrf'] = collector_default_vrf
    collectors.append(collector_1)

    collector_2 = {}
    collector_2['ip'] = '10.10.12.2'
    collector_2['port'] = collector_default_port
    collector_2['vrf'] = collector_default_vrf
    collectors.append(collector_2)

    collector_3 = {}
    collector_3['ip'] = '10.10.13.2'
    collector_3['port'] = collector_default_port
    collector_3['vrf'] = collector_default_vrf
    collectors.append(collector_3)

    sflow_config = ops1.libs.vtysh.show_sflow()
    assert sflow_config['sflow'] == 'enabled'
    assert int(sflow_config['sampling_rate']) == sampling_rate
    assert str(sflow_config['agent_interface']) == p4
    for collector in sflow_config['collector']:
        assert collector in collectors

    time.sleep(20)

    # Start sflowtool
    hs2.libs.sflowtool.start(mode='line')
    hs3.libs.sflowtool.start(mode='line')
    hs4.libs.sflowtool.start(mode='line')

    # Generate CPU destined traffic
    step("### Checking CPU destined traffic ###")
    hs1.libs.ping.ping(ping_count, '10.10.10.1', ping_interval)

    # Stop sflowtool on Hosts
    result_hs2 = hs2.libs.sflowtool.stop()
    result_hs3 = hs3.libs.sflowtool.stop()
    result_hs4 = hs4.libs.sflowtool.stop()

    step("Flow count hs2: " + str(result_hs2['flow_count']))
    step("Flow count hs3: " + str(result_hs3['flow_count']))
    step("Flow count hs4: " + str(result_hs4['flow_count']))

    # Checking if packets are captured by collector
    assert len(result_hs2['packets']) > 0
    assert len(result_hs3['packets']) > 0
    assert len(result_hs4['packets']) > 0

    # Check for ping samples in all the collectors
    assert sflow_utils.check_ping_sample(sflow_output=result_hs2,
                                         host1='10.10.10.2',
                                         host2='10.10.10.1',
                                         agent_address='10.10.13.1',
                                         family='ipv4')

    assert sflow_utils.check_ping_sample(sflow_output=result_hs3,
                                         host1='10.10.10.2',
                                         host2='10.10.10.1',
                                         agent_address='10.10.13.1',
                                         family='ipv4')

    assert sflow_utils.check_ping_sample(sflow_output=result_hs4,
                                         host1='10.10.10.2',
                                         host2='10.10.10.1',
                                         agent_address='10.10.13.1',
                                         family='ipv4')
