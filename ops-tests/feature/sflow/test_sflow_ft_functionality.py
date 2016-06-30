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
OpenSwitch Test for sFlow functionality.
"""

import time
import sflow_utils
from pytest import mark

TOPOLOGY = """
#                    +----------------+
#                    |                |
#                    |   Host 3       |
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
#         +-+----------+--+
#           |          |
#           |          |
#+----------+--+     +-+------------+
#|             |     |              |
#|             |     |              |
#|  Host 1     |     |  Host 2      |
#|             |     |              |
#+-------------+     +--------------+

# Nodes
[type=openswitch name="OpenSwitch"] ops1
[type=host name="Host 1"] hs1
[type=host name="Host 2"] hs2
[type=host name="Host 3" image="openswitch/sflowtool:latest"] hs3

# Links
hs1:1 -- ops1:1
hs2:1 -- ops1:2
hs3:1 -- ops1:3
"""


@mark.gate
@mark.platform_incompatible(['docker'])
def test_sflow_ft_functionality(topology, step):
    """
    Test sflow enable/disable.
    Test sampling of following types of packets:
        1. CPU destined traffic
        2. Routed packets (L3)
        3. Switched packets (L2)
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
    sampling_rate = 20
    vlan = '10'
    p3 = ops1.ports['3']
    v6host1 = '1000:0000:0000:0000:0000:0000:0000:0002'
    v6host2 = '1000:0000:0000:0000:0000:0000:0000:0001'
    v6host22 = '1001:0000:0000:0000:0000:0000:0000:0002'
    v6agent = '1002:0000:0000:0000:0000:0000:0000:0001'

    # Configure host interfaces
    step("### Configuring host interfaces ###")
    hs1.libs.ip.interface('1', addr='10.10.10.2/24', up=True)
    hs2.libs.ip.interface('1', addr='10.10.11.2/24', up=True)
    hs3.libs.ip.interface('1', addr='10.10.12.2/24', up=True)

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

    step("Configuring interface 3 of switch")
    with ops1.libs.vtysh.ConfigInterface('3') as ctx:
        ctx.ip_address('10.10.12.1/24')
        ctx.no_shutdown()

    # Configure sFlow
    step("### Configuring sFlow ###")
    with ops1.libs.vtysh.Configure() as ctx:
        ctx.sflow_enable()
        ctx.sflow_sampling(sampling_rate)
        ctx.sflow_agent_interface(p3, address_family='ipv4')
        ctx.sflow_collector('10.10.12.2')

    ops1('show sflow')

    collector = {}
    collector['ip'] = '10.10.12.2'
    collector['port'] = '6343'
    collector['vrf'] = 'vrf_default'

    sflow_config = ops1.libs.vtysh.show_sflow()
    assert sflow_config['sflow'] == 'enabled'
    assert int(sflow_config['sampling_rate']) == sampling_rate
    assert sflow_config['collector'][0] == collector
    assert str(sflow_config['agent_interface']) == p3

    time.sleep(20)

    # Start sflowtool
    hs3.libs.sflowtool.start(mode='line')

    # Generate CPU destined traffic
    step("### Checking CPU destined traffic ###")
    hs1.libs.ping.ping(ping_count, '10.10.10.1', ping_interval)

    # Stop sflowtool
    result = hs3.libs.sflowtool.stop()

    # Checking if packets are captured by collector
    assert len(result['packets']) > 0, "No packets seen at collector"

    # Check for ping request and response packets
    assert sflow_utils.check_ping_sample(sflow_output=result,
                                         host1='10.10.10.2',
                                         host2='10.10.10.1',
                                         agent_address='10.10.12.1',
                                         family='ipv4')

    # Generate L3 traffic
    step("### Checking Routed traffic (L3) ###")
    hs3.libs.sflowtool.start(mode='line')
    hs1.libs.ping.ping(ping_count, '10.10.11.2', ping_interval)
    result = hs3.libs.sflowtool.stop()

    assert len(result['packets']) > 0, "No packets seen at collector"

    # Check for ping request and response packets
    assert sflow_utils.check_ping_sample(sflow_output=result,
                                         host1='10.10.10.2',
                                         host2='10.10.11.2',
                                         agent_address='10.10.12.1',
                                         family='ipv4')

    step("### Disabling sFlow ###")
    with ops1.libs.vtysh.Configure() as ctx:
        ctx.no_sflow_enable()

    ops1('show sflow')
    sflow_config = ops1.libs.vtysh.show_sflow()
    assert sflow_config['sflow'] == 'disabled'

    time.sleep(20)

    # Check sampling when sFlow is disabled
    step("### Checking Routed traffic (L3) on disable ###")
    hs3.libs.sflowtool.start(mode='line')
    hs1.libs.ping.ping(ping_count, '10.10.11.2', ping_interval)
    result = hs3.libs.sflowtool.stop()

    assert result['flow_count'] == 0 and result['sample_count'] == 0

    # Enable sFlow again
    step("### Enabling sFlow again ###")
    with ops1.libs.vtysh.Configure() as ctx:
        ctx.sflow_enable()

    sflow_config = ops1.libs.vtysh.show_sflow()
    assert sflow_config['sflow'] == 'enabled'

    # Configure VLAN on switch
    step("### Configuring vlan ###")
    with ops1.libs.vtysh.ConfigVlan(vlan) as ctx:
        ctx.no_shutdown()

    # Configure switch interfaces connected to hosts to be part of VLAN
    step("### Configuring interfaces 1 & 2 to be "
         "part of vlan in access mode ###")
    with ops1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.no_routing()
        ctx.vlan_access(vlan)

    with ops1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.no_routing()
        ctx.vlan_access(vlan)

    # Change IP of Host 2 to same subnet as Host 1
    step("### Re-assigning IP to Host 2 to be on same subnet as Host 1 ###")
    hs2.libs.ip.remove_ip('1', addr='10.10.11.2/24')
    hs2.libs.ip.interface('1', addr='10.10.10.3/24', up=True)

    # Generate L2 traffic
    step("### Checking Switched traffic (L2) ###")
    hs3.libs.sflowtool.start(mode='line')
    hs1.libs.ping.ping(ping_count, '10.10.10.3', ping_interval)
    result = hs3.libs.sflowtool.stop()

    assert len(result['packets']) > 0, "No packets seen at collector"
    assert sflow_utils.check_ping_sample(sflow_output=result,
                                         host1='10.10.10.2',
                                         host2='10.10.10.3',
                                         agent_address='10.10.12.1',
                                         family='ipv4')

    # Configure host ipv6 interfaces
    step("### Configuring host ipv6 interfaces ###")
    hs1.libs.ip.interface('1', addr='1000::2/64', up=True)
    hs2.libs.ip.interface('1', addr='1001::2/64', up=True)
    hs3.libs.ip.interface('1', addr='1002::2/64', up=True)

    # Add ipv6 routes on hosts
    step("### Adding ipv6 routes on hosts ###")
    hs1.libs.ip.add_route('1001::/64', '1000::1')
    hs2.libs.ip.add_route('1000::/64', '1001::1')

    # Configure ipv6 interfaces on the switch
    step("Configuring ipv6 interface 1 of switch")
    with ops1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.routing()
        ctx.ipv6_address('1000::1/64')
        ctx.no_shutdown()

    step("Configuring ipv6 interface 2 of switch")
    with ops1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.routing()
        ctx.ipv6_address('1001::1/64')
        ctx.no_shutdown()

    step("Configuring ipv6 interface 3 of switch")
    with ops1.libs.vtysh.ConfigInterface('3') as ctx:
        ctx.no_ip_address('10.10.12.1/24')
        ctx.ipv6_address('1002::1/64')
        ctx.no_shutdown()

    # Configure sFlow
    step("### Configuring sFlow ###")
    with ops1.libs.vtysh.Configure() as ctx:
        ctx.sflow_agent_interface(p3, address_family='ipv6')
        ctx.no_sflow_enable()
        ctx.sflow_enable()
        ctx.sflow_collector('1002::2')

    ops1('show sflow')

    collector = {}
    collector['ip'] = '1002::2'
    collector['port'] = '6343'
    collector['vrf'] = 'vrf_default'

    sflow_config = ops1.libs.vtysh.show_sflow()
    assert sflow_config['sflow'] == 'enabled'
    assert int(sflow_config['sampling_rate']) == sampling_rate
    assert sflow_config['collector'][1] == collector
    assert str(sflow_config['agent_interface']) == p3

    time.sleep(20)

    # Start sflowtool
    hs3.libs.sflowtool.start(mode='line')

    # Generate CPU destined traffic
    step("### Checking CPU destined traffic ###")
    hs1.libs.ping.ping(ping_count, '1000::1', ping_interval)

    # Stop sflowtool
    result = hs3.libs.sflowtool.stop()

    # Checking if packets are captured by collector
    assert len(result['packets']) > 0, "No packets seen at collector"

    # Check for ping request and response packets
    assert sflow_utils.check_ping_sample(sflow_output=result,
                                         host1=v6host1,
                                         host2=v6host2,
                                         agent_address=v6agent,
                                         family='ipv6')

    # Generate L3 traffic
    step("### Checking Routed traffic (L3) ###")
    hs3.libs.sflowtool.start(mode='line')
    hs1.libs.ping.ping(ping_count, '1001::2', ping_interval)
    result = hs3.libs.sflowtool.stop()

    assert len(result['packets']) > 0, "No packets seen at collector"

    # Check for ping request and response packets
    assert sflow_utils.check_ping_sample(sflow_output=result,
                                         host1=v6host1,
                                         host2=v6host22,
                                         agent_address=v6agent,
                                         family='ipv6')
