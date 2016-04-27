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
# Name:        test_layer3_ft_subinterface_route.py
#
# Objective:   To verify that subinterface route are installed in asic. Ping
#              should pass only if route is installed.
#
# Topology:    2 switches connected by 1 interface and 2 hosts connected
#              by 1 interface
#
##########################################################################

"""
OpenSwitch Tests for subinterface route test using hosts
"""

TOPOLOGY = """
# +-------+                                 +-------+
# |       |     +-------+     +-------+     |       |
# |  hs1  <----->  sw1  <----->  sw2  <----->  hs2  |
# |       |     +-------+     +-------+     |       |
# +-------+                                 +-------+

# Nodes
[type=openswitch name="Switch 1"] sw1
[type=openswitch name="Switch 2"] sw2
[type=host name="Host 1"] hs1
[type=host name="Host 2"] hs2

# Links
hs1:1 -- sw1:1
sw1:2 -- sw2:2
sw2:4 -- hs2:1
"""


def configure_subinterface(sw, interface, ip_addr, vlan):
    with sw.libs.vtysh.ConfigSubinterface(interface, vlan) as ctx:
        ctx.no_shutdown()
        ctx.ip_address(ip_addr)
        ctx.encapsulation_dot1_q(vlan)


def turn_on_interface(sw, interface):
    with sw.libs.vtysh.ConfigInterface(interface) as ctx:
        ctx.no_shutdown()


def check_connectivity_between_hosts(h1, h1_ip, h2, h2_ip, ping_num, success):
    ping = h1.libs.ping.ping(ping_num, h2_ip)
    if success:
        assert ping['transmitted'] == ping['received'] == ping_num,\
            'Ping between ' + h1_ip + ' and ' + h2_ip + ' failed'
    else:
        assert not ping['transmitted'] == ping['received'] == ping_num,\
            'Ping between ' + h1_ip + ' and ' + h2_ip + ' success'

    ping = h2.libs.ping.ping(ping_num, h1_ip)
    if success:
        assert ping['transmitted'] == ping['received'] == ping_num,\
            'Ping between ' + h2_ip + ' and ' + h1_ip + ' failed'
    else:
        assert not ping['transmitted'] == ping['received'] == ping_num,\
            'Ping between ' + h2_ip + ' and ' + h1_ip + ' success'


def test_subinterface_route(topology):
    """Test description.

    Topology:

        [h1] <-----> [s1] <-----> [s2] <-----> [h2]

    Objective:
        Test if subinterface routes are installed in ASIC using ping.

    Cases:
        - Execute successful pings between hosts with static routes configured.
    """
    sw1 = topology.get('sw1')
    sw2 = topology.get('sw2')
    hs1 = topology.get('hs1')
    hs2 = topology.get('hs2')

    assert sw1 is not None
    assert sw2 is not None
    assert hs1 is not None
    assert hs2 is not None

    p11 = sw1.ports['1']
    p12 = sw1.ports['2']
    p22 = sw2.ports['2']
    p24 = sw2.ports['4']

    interface = '2'
    subinterface_vlan = '10'
    sw1_subinterface_ip = '2.2.2.2'
    sw2_subinterface_ip = '2.2.2.1'
    h1_ip_address = '1.1.1.2'
    h2_ip_address = '3.3.3.3'
    sw1_l3_ip_address = '1.1.1.1'
    sw2_l3_ip_address = '3.3.3.1'
    mask = '/24'

    print("Create subinterface in both switches")
    configure_subinterface(sw1, interface,
                           sw1_subinterface_ip + mask,
                           subinterface_vlan)
    configure_subinterface(sw2, interface,
                           sw2_subinterface_ip + mask,
                           subinterface_vlan)

    print("Turning on all interfaces used in this test")
    ports_sw1 = [p11, p12]
    for port in ports_sw1:
        turn_on_interface(sw1, port)

    ports_sw2 = [p22, p24]
    for port in ports_sw2:
        turn_on_interface(sw2, port)

    print("Configure IP and bring UP in host 1")
    hs1.libs.ip.interface('1', addr=h1_ip_address + mask, up=True)

    print("Adding routes on host 1")
    hs1.libs.ip.add_route('3.3.3.0/24', '1.1.1.1')

    print("Configure IP and bring UP in host 2")
    hs2.libs.ip.interface('1', addr=h2_ip_address + mask, up=True)

    print("Adding routes on host 2")
    hs2.libs.ip.add_route('1.1.1.0/24', '3.3.3.1')

    print("Configure L3 interface IP address on switch 1")
    with sw1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.ip_address(sw1_l3_ip_address + mask)
        ctx.no_shutdown()

    print("Adding routes on Switch 1")
    with sw1.libs.vtysh.Configure() as ctx:
        ctx.ip_route('3.3.3.0/24', '2.2.2.1')

    print("Configure IP address on switch 2")
    with sw2.libs.vtysh.ConfigInterface('4') as ctx:
        ctx.ip_address(sw2_l3_ip_address + mask)
        ctx.no_shutdown()

    print("Adding routes on Switch 2")
    with sw2.libs.vtysh.Configure() as ctx:
        ctx.ip_route('1.1.1.0/24', '2.2.2.2')

    print("Ping h1 to host 2")
    check_connectivity_between_hosts(hs1, h1_ip_address, hs2, h2_ip_address,
                                     10, True)
