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
OpenSwitch Test for sFlow sampling rate configuration changes.
"""

import pytest
import time
from pytest import mark
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


@pytest.mark.timeout(1000)
@mark.gate
def test_sflow_ft_sampling_rate(topology, step):
    """
    Tests sampling rate configuration.
    """
    ops1 = topology.get('ops1')
    hs1 = topology.get('hs1')
    hs2 = topology.get('hs2')

    assert ops1 is not None
    assert hs1 is not None
    assert hs2 is not None

    ping_count = 10000.0
    ping_interval = 0.001
    sampling_rate = 2048
    p1 = ops1.ports['1']
    expected_percent = 70.0
    ingress_egress = 4.0
    count = 10
    quiet = True

    # We expect at least 70% flow packets at the collector
    expected_samples = float(ingress_egress * ping_count/sampling_rate *
                             (expected_percent/100.0))

    # Configure host interfaces
    step("### Configuring host interfaces ###")
    hs1.libs.ip.interface('1', addr='10.10.10.2/24', up=True)
    hs2.libs.ip.interface('1', addr='10.10.11.2/24', up=True)

    # Add routes on hosts
    step("### Adding routes on hosts ###")
    hs1.libs.ip.add_route('10.10.11.0/24', '10.10.10.1')
    hs2.libs.ip.add_route('10.10.10.0/24', '10.10.11.1')

    # Configure interfaces on the switch
    step("Configuring interface 1 of switch")
    with ops1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.ip_address('10.10.10.1/24')
        ctx.no_shutdown()

    step("Configuring interface 2 of switch")
    with ops1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.ip_address('10.10.11.1/24')
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
    for x in range(0, count):
        hs1.libs.ping.ping(ping_count, '10.10.11.2', ping_interval, quiet)

    time.sleep(15)
    # Stop sflowtool
    result = hs2.libs.sflowtool.stop()

    # Checking if packets are present
    assert len(result['packets']) > 0

    # Checking num of flow packets matches expected samples
    assert result['flow_count'] >= count*expected_samples

    for packet in result['packets']:
        if str(packet['packet_type']) == 'FLOW' and \
                packet['eth_type'] == '0x0800':
            assert int(packet['sampling_rate']) == sampling_rate

    # Configure new sampling rate
    step("### Configuring sFlow ###")
    sampling_rate = 4096

    # We expect at least 70% flow packets at the collector
    expected_samples = float(ingress_egress * ping_count/sampling_rate *
                             (expected_percent/100.0))

    with ops1.libs.vtysh.Configure() as ctx:
        ctx.sflow_sampling(sampling_rate)

    ops1('show sflow')

    sflow_config = ops1.libs.vtysh.show_sflow()
    assert int(sflow_config['sampling_rate']) == sampling_rate

    time.sleep(20)

    # Start sflowtool
    hs2.libs.sflowtool.start(mode='line')
    for x in range(0, count):
        hs1.libs.ping.ping(ping_count, '10.10.11.2', ping_interval, quiet)
    time.sleep(15)
    result = hs2.libs.sflowtool.stop()

    # Checking if packets are present
    assert len(result['packets']) > 0

    # Checking num of flow packets matches expected samples
    assert result['flow_count'] >= count*expected_samples

    for packet in result['packets']:
        if str(packet['packet_type']) == 'FLOW' and \
                packet['eth_type'] == '0x0800':
            assert int(packet['sampling_rate']) == sampling_rate
