# -*- coding: utf-8 -*-
#
# Copyright (C) 2016 Hewlett Packard Enterprise Development LP
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
OpenSwitch Test for sFlow agent interface configuration changes.
"""

import time


TOPOLOGY = """
#                    +----------------+
#                    |                |
#                    |   Host 2       |
#                    |  (sflowtool)   |
#                    |                |
#                    +-+--------------+
#                      |
#                      |
#         +------------+--+
#         |               |
#         |               |
#         |    Open       |
#         |    Switch     |
#         |               |
#         |               |
#         +-+-------------+
#           |
#           |
#+----------+--+
#|             |
#|             |
#|  Host 1     |
#|             |
#+-------------+

# Nodes
[type=openswitch name="OpenSwitch"] ops1
[type=host name="Host 1"] hs1
[type=host name="Host 2" image="openswitch/sflowtool:latest"] hs2

# Links
hs1:1 -- ops1:1
hs2:1 -- ops1:2
"""


def test_sflow_ft_agent_interface(topology, step):
    """
    Tests agent interface configuration.
    """
    ops1 = topology.get('ops1')
    hs1 = topology.get('hs1')
    hs2 = topology.get('hs2')

    assert ops1 is not None
    assert hs1 is not None
    assert hs2 is not None

    p1 = ops1.ports['1']
    p2 = ops1.ports['2']
    ping_count = 200
    ping_interval = 0.1
    sampling_rate = 20

    # Configure host interfaces
    step("### Configuring host interfaces ###")
    hs1.libs.ip.interface('1', addr='10.10.10.2/24', up=True)
    hs2.libs.ip.interface('1', addr='10.10.11.2/24', up=True)

    # Configure interfaces on the switch
    step("Configuring interface 1 of switch")
    with ops1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.ip_address('10.10.10.1/24')
        ctx.no_shutdown()

    step("Configuring interface 2 of switch")
    with ops1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.ip_address('10.10.11.1/24')
        ctx.no_shutdown()

    step("Configuring interface 3 of switch")
    with ops1.libs.vtysh.ConfigInterface('3') as ctx:
        ctx.ip_address('10.10.12.1/24')
        ctx.no_shutdown()

    # Configure sFlow
    step("### Configuring sFlow ###")
    with ops1.libs.vtysh.Configure() as ctx:
        ctx.sflow_enable()
        ctx.sflow_sampling(sampling_rate)
        ctx.sflow_agent_interface(p1)
        ctx.sflow_collector('10.10.11.2')

    ops1('show sflow')

    collector = {}
    collector['ip'] = '10.10.11.2'
    collector['port'] = '6343'
    collector['vrf'] = 'vrf_default'

    sflow_config = ops1.libs.vtysh.show_sflow()
    assert sflow_config['sflow'] == 'enabled'
    assert int(sflow_config['sampling_rate']) == sampling_rate
    assert sflow_config['collector'][0] == collector
    assert str(sflow_config['agent_interface']) == p1

    time.sleep(20)

    # Start sflowtool
    hs2.libs.sflowtool.start(mode='line')

    # Generate CPU destined traffic
    hs1.libs.ping.ping(ping_count, '10.10.10.1', ping_interval)

    # Stop sflowtool
    result = hs2.libs.sflowtool.stop()

    # Checking if packets are captured by collector
    assert len(result['packets']) > 0

    # Checking agent interface ip for interface 1 in FLOW
    for packet in result['packets']:
        if packet['packet_type'] == 'FLOW':
            assert packet['agent_address'] == '10.10.10.1'

    # Configure sFlow agent interface
    step("### Configuring sFlow ###")
    with ops1.libs.vtysh.Configure() as ctx:
        ctx.sflow_agent_interface(p2)

    ops1('show sflow')
    sflow_config = ops1.libs.vtysh.show_sflow()
    assert str(sflow_config['agent_interface']) == p2

    time.sleep(20)

    # Start sflowtool
    hs2.libs.sflowtool.start(mode='line')
    hs1.libs.ping.ping(ping_count, '10.10.10.1', ping_interval)
    result = hs2.libs.sflowtool.stop()

    # Checking if packets are captured by collector
    assert len(result['packets']) > 0

    # Checking agent interface change
    for packet in result['packets']:
        if packet['packet_type'] == 'FLOW':
            assert packet['agent_address'] == '10.10.11.1'

    step("### Remove sFlow agent interface configuration ###")
    with ops1.libs.vtysh.Configure() as ctx:
        ctx.no_sflow_agent_interface()

    time.sleep(20)

    ops1('show sflow')
    sflow_config = ops1.libs.vtysh.show_sflow()
    assert str(sflow_config['agent_interface']) == 'Not set'

    # Start sflowtool
    hs2.libs.sflowtool.start(mode='line')
    hs1.libs.ping.ping(ping_count, '10.10.10.1', ping_interval)
    result = hs2.libs.sflowtool.stop()

    # Checking if packets are captured by collector
    assert len(result['packets']) > 0

    # Checking if agent address belongs to one of the IP addresses configured
    # on the switch
    possible_agent_addresses = ['10.10.10.1', '10.10.11.1', '10.10.12.1']
    for packet in result['packets']:
        if packet['packet_type'] == 'FLOW':
            assert packet['agent_address'] in possible_agent_addresses
