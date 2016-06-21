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

##########################################################################
# Name:        test_ft_subinterface_switching_traffic.py
#
# Objective:   To verify that subinterface does not switch traffic. Ping
#              should fail to host on same vlan.
#
# Topology:    1 switch connected by 2 interface to 2 hosts
#
##########################################################################

"""
OpenSwitch Tests for subinterface route test using hosts
"""

from pytest import mark

TOPOLOGY = """
#               +-------+
# +-------+     |       |     +-------+
# |  hs1  <----->  sw1  <----->  hs2  |
# +-------+     |       |     +-------+
#               +-------+

# Nodes
[type=openswitch name="Switch 1"] sw1
[type=host name="Host 1"] hs1
[type=host name="Host 2"] hs2

# Links
hs1:1 -- sw1:1
sw1:2 -- hs2:1
"""


def configure_subinterface(sw, interface, ip_addr, vlan):
    with sw.libs.vtysh.ConfigSubinterface(interface, vlan) as ctx:
        ctx.no_shutdown()
        ctx.ip_address(ip_addr)
        ctx.encapsulation_dot1_q(vlan)


def configure_l2_interface(sw, interface, vlan):
    with sw.libs.vtysh.ConfigInterface(interface) as ctx:
        ctx.no_shutdown()
        ctx.no_routing()
        ctx.vlan_access(vlan)


@mark.platform_incompatible(['docker'])
def test_subinterface_switching(topology):
    """Test description.

    Topology:

        [h1] <-----> [s1] <-----> [h2]

    Objective:
        Test if subinterface participates in switching
        traffic to host part of same vlan.

    Cases:
        - Execute pings fails between hosts.
    """
    sw1 = topology.get('sw1')
    hs1 = topology.get('hs1')
    hs2 = topology.get('hs2')

    assert sw1 is not None
    assert hs1 is not None
    assert hs2 is not None

    subinterface_vlan = '10'
    sw1_subinterface_ip = '2.2.2.2'
    h1_ip_address = '2.2.2.1'
    h2_ip_address = '2.2.2.3'
    mask = '/24'

    with sw1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.no_shutdown()

    print("Create subinterface")
    configure_subinterface(sw1, '1',
                           sw1_subinterface_ip + mask,
                           subinterface_vlan)

    print("Configure IP and bring UP in host 1")
    hs1.libs.ip.interface('1', addr=h1_ip_address + mask, up=True)

    print("Configure IP and bring UP in host 2")
    hs2.libs.ip.interface('1', addr=h2_ip_address + mask, up=True)

    with sw1.libs.vtysh.ConfigVlan(subinterface_vlan) as ctx:
        ctx.no_shutdown()

    print("Configure l2 interface with vlan " + subinterface_vlan)
    configure_l2_interface(sw1, '2', subinterface_vlan)

    print("Ping h1 to host 2")
    ping = hs1.libs.ping.ping(10, h2_ip_address)
    assert ping['received'] == 0,\
        'Ping between ' + h1_ip_address + ' and ' + h2_ip_address + ' passed'
