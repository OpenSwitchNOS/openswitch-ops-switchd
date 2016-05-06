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
OpenSwitch Test for sFlow per interface sampling.
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
#         +-+-----------+-+
#           |           |
#           |           |
#+----------+--+     +--+----------+
#|             |     |             |
#|             |     |             |
#|  Host 1     |     |  Host 3     |
#|             |     |             |
#+-------------+     +-------------+

# Nodes
[type=openswitch name="OpenSwitch"] ops1
[type=host name="Host 1"] hs1
[type=host name="Host 2" image="openswitch/sflowtool:latest"] hs2
[type=host name="Host 3"] hs3

# Links
hs1:1 -- ops1:1
hs2:1 -- ops1:2
hs3:1 -- ops1:3
"""


def test_sflow_ft_per_interface_sampling(topology, step):
    """
    Tests per interface sampling.
    """
    ops1 = topology.get('ops1')
    hs1 = topology.get('hs1')
    hs2 = topology.get('hs2')
    hs3 = topology.get('hs3')

    assert ops1 is not None
    assert hs1 is not None
    assert hs2 is not None
    assert hs3 is not None

    ping_count = 200
    ping_interval = 0.1
    sampling_rate = 10
    polling_interval = 10
    p1 = ops1.ports['1']

    # Configure host interfaces
    step("### Configuring host interfaces ###")
    hs1.libs.ip.interface('1', addr='10.10.10.2/24', up=True)
    hs2.libs.ip.interface('1', addr='10.10.11.2/24', up=True)
    hs3.libs.ip.interface('1', addr='10.10.12.2/24', up=True)

    # Add routes on hosts
    step("### Adding routes on hosts ###")
    hs1.libs.ip.add_route('10.10.12.0/24', '10.10.10.1')
    hs3.libs.ip.add_route('10.10.10.0/24', '10.10.12.1')

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
        ctx.sflow_polling(polling_interval)

    collector = {}
    collector['ip'] = '10.10.11.2'
    collector['port'] = '6343'
    collector['vrf'] = 'vrf_default'

    sflow_config = ops1.libs.vtysh.show_sflow()
    assert sflow_config['sflow'] == 'enabled'
    assert int(sflow_config['sampling_rate']) == sampling_rate
    assert sflow_config['collector'][0] == collector
    assert str(sflow_config['agent_interface']) == p1
    assert int(sflow_config['polling_interval']) == polling_interval

    # No sFlow enable on both interfaces
    with ops1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.no_sflow_enable()

    with ops1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.no_sflow_enable()

    with ops1.libs.vtysh.ConfigInterface('3') as ctx:
        ctx.no_sflow_enable()

    # Checking the show sflow interface <interface> command
    sflow_config = ops1.libs.vtysh.show_sflow_interface('1')
    assert sflow_config['sflow'] == 'disabled'
    assert int(sflow_config['sampling_rate']) == sampling_rate

    sflow_config = ops1.libs.vtysh.show_sflow_interface('2')
    assert sflow_config['sflow'] == 'disabled'
    assert int(sflow_config['sampling_rate']) == sampling_rate
    time.sleep(20)

    sflow_config = ops1.libs.vtysh.show_sflow_interface('3')
    assert sflow_config['sflow'] == 'disabled'
    assert int(sflow_config['sampling_rate']) == sampling_rate
    time.sleep(20)

    # Start sflowtool
    hs2.libs.sflowtool.start(mode='line')

    # Generate CPU destined traffic
    hs1.libs.ping.ping(ping_count, '10.10.10.1', ping_interval)

    time.sleep(20)
    # Stop sflowtool
    result = hs2.libs.sflowtool.stop()

    # Checking if FLOW packets still post no sflow enable on interfaces
    for packet in result['packets']:
        assert str(packet['packet_type']) != 'FLOW'

    # sFlow enable on interfaces 1
    with ops1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.sflow_enable()

    # Checking the show sflow interface <interface> command
    sflow_config = ops1.libs.vtysh.show_sflow_interface('1')
    assert sflow_config['sflow'] == 'enabled'
    assert int(sflow_config['sampling_rate']) == sampling_rate

    time.sleep(20)

    # Start sflowtool
    hs2.libs.sflowtool.start(mode='line')

    # Generate CPU destined traffic on sflow enabled interface
    hs1.libs.ping.ping(ping_count, '10.10.10.1', ping_interval)

    # Generate CPU destined traffic on sflow disabled interface
    hs3.libs.ping.ping(ping_count, '10.10.12.1', ping_interval)

    time.sleep(20)
    # Stop sflowtool
    result = hs2.libs.sflowtool.stop()

    # Checking if FLOW packets are not sent from interface
    # where sFlow is disabled
    assert result['flow_count'] > 0
    src_addresses = ['10.10.12.2', '10.10.12.1', '10.10.12.2', '10.10.12.1']
    for packet in result['packets']:
        if str(packet['packet_type']) == 'FLOW':
            assert str(packet['src_ip']) not in src_addresses
