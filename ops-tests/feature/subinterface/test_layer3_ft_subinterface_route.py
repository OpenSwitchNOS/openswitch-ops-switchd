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

from pytest import mark

TOPOLOGY = """
# +-------+                                 +-------+  +-------+
# |       |     +-------+     +-------+     |       |  |       |
# |  hs1  <----->  sw1  <----->  sw2  <----->  hs2  |  |  hs3  |
# |       |     +-------+     +-------+     |       |  +--------
# +-------+                       |         +-------+     |
#                                 |-----------------------|

# Nodes
[type=openswitch name="Switch 1"] sw1
[type=openswitch name="Switch 2"] sw2
[type=host name="Host 1"] hs1
[type=host name="Host 2"] hs2
[type=host name="Host 3"] hs3

# Links
hs1:1 -- sw1:1
sw1:2 -- sw2:2
sw2:4 -- hs2:1
sw2:3 -- hs3:1
"""


def configure_subinterface(sw, interface, ip_addr, vlan):
    with sw.libs.vtysh.ConfigSubinterface(interface, vlan) as ctx:
        ctx.no_shutdown()
        ctx.ip_address(ip_addr)
        ctx.encapsulation_dot1_q(vlan)


def turn_on_interface(sw, interface):
    with sw.libs.vtysh.ConfigInterface(interface) as ctx:
        ctx.no_shutdown()


def check_route(buf, network, nexthop, cli):
    if cli:
        for item in buf:
            if item['id'] == network and\
               item['next_hops'][0]['via'] == nexthop:
                return True
    else:
        if network in buf and nexthop in buf:
            return True
    return False


def configure_l2_interface(sw, interface, vlan):
    with sw.libs.vtysh.ConfigInterface(interface) as ctx:
        ctx.no_shutdown()
        ctx.no_routing()
        ctx.vlan_access(vlan)


@mark.gate
@mark.platform_incompatible(['docker'])
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
    hs3 = topology.get('hs3')

    assert sw1 is not None
    assert sw2 is not None
    assert hs1 is not None
    assert hs2 is not None
    assert hs3 is not None

    sw1p1 = sw1.ports["1"]
    sw1p2 = sw1.ports["2"]
    sw2p2 = sw2.ports["2"]
    sw2p3 = sw2.ports["3"]
    sw2p4 = sw2.ports["4"]

    subinterface_vlan = '10'
    sw1_subinterface_ip = '2.2.2.2'
    sw2_subinterface_ip = '2.2.2.1'
    h1_ip_address = '1.1.1.2'
    h2_ip_address = '3.3.3.3'
    h3_ip_address = '3.3.3.4'
    sw1_l3_ip_address = '1.1.1.1'
    sw2_l3_ip_address = '3.3.3.1'
    mask = '/24'

    print("Create subinterface in both switches")
    configure_subinterface(sw1, sw1p2,
                           sw1_subinterface_ip + mask,
                           subinterface_vlan)
    configure_subinterface(sw2, sw2p2,
                           sw2_subinterface_ip + mask,
                           subinterface_vlan)

    print("Turning on all interfaces used in this test")
    turn_on_interface(sw1, sw1p1)
    turn_on_interface(sw1, sw1p2)
    turn_on_interface(sw2, sw2p2)
    turn_on_interface(sw2, sw2p4)

    print("Configure IP and bring UP in host 1")
    hs1.libs.ip.interface('1', addr=h1_ip_address + mask, up=True)

    print("Adding routes on host 1")
    hs1.libs.ip.add_route('3.3.3.0/24', '1.1.1.1')

    print("Configure IP and bring UP in host 2")
    hs2.libs.ip.interface('1', addr=h2_ip_address + mask, up=True)

    print("Adding routes on host 2")
    hs2.libs.ip.add_route('1.1.1.0/24', '3.3.3.1')

    print("Configure IP and bring UP in host 3")
    hs3.libs.ip.interface('1', addr=h3_ip_address + mask, up=True)

    print("Configure L3 interface IP address on switch 1")
    with sw1.libs.vtysh.ConfigInterface(sw1p1) as ctx:
        ctx.ip_address(sw1_l3_ip_address + mask)
        ctx.no_shutdown()

    print("Adding routes on Switch 1")
    with sw1.libs.vtysh.Configure() as ctx:
        ctx.ip_route('3.3.3.0/24', '2.2.2.1')

    print("Configure IP address on switch 2")
    with sw2.libs.vtysh.ConfigInterface(sw2p4) as ctx:
        ctx.ip_address(sw2_l3_ip_address + mask)
        ctx.no_shutdown()

    print("Adding routes on Switch 2")
    with sw2.libs.vtysh.Configure() as ctx:
        ctx.ip_route('1.1.1.0/24', '2.2.2.2')

    print("Check routes")
    pass_flag = 0
    attemps = 3
    while pass_flag == 0 and attemps > 0:
        switch1_routes = sw1.libs.vtysh.show_ip_route()
        switch2_routes = sw2.libs.vtysh.show_ip_route()
        sw1_route = sw1("ip netns exec swns route", shell='bash')
        sw2_route = sw2("ip netns exec swns route", shell='bash')
        if check_route(switch1_routes, '3.3.3.0', '2.2.2.1', True) and\
           check_route(switch2_routes, '1.1.1.0', '2.2.2.2', True) and\
           check_route(sw1_route, '3.3.3.0', '2.2.2.1', False) and\
           check_route(sw2_route, '1.1.1.0', '2.2.2.2', False):
            pass_flag = 1
        elif check_route(switch1_routes, '1.1.1.0', '2.2.2.2', True) and\
            check_route(switch2_routes, '3.3.3.0', '2.2.2.1', True) and\
            check_route(sw1_route, '1.1.1.0', '2.2.2.2', False) and\
                check_route(sw2_route, '3.3.3.0', '2.2.2.1', False):
                pass_flag = 1
        else:
            pass_flag = 0
            attemps -= 1
    assert pass_flag == 1, "Routes not configured"

    print("Ping h1 to host 2")
    pass_flag = 0
    ping_num = 10
    ping = hs1.libs.ping.ping(ping_num, h2_ip_address)
    if ping['transmitted'] == ping['received'] == ping_num:
        pass_flag = 1
    if pass_flag == 0:
        sw1.libs.vtysh.show_running_config()
        sw1.libs.vtysh.show_ip_route()
        sw1.libs.vtysh.show_interface(sw1p1)
        sw1.libs.vtysh.show_interface(sw1p2)
        sw2.libs.vtysh.show_running_config()
        sw2.libs.vtysh.show_ip_route()
        sw2.libs.vtysh.show_interface(sw2p2)
        sw2.libs.vtysh.show_interface(sw2p4)
    assert ping['transmitted'] == ping['received'],\
        'Ping between ' + h1_ip_address + ' and ' + h2_ip_address + ' failed'
    turn_on_interface(sw2, sw2p3)

    print("Configure Vlan 10")
    with sw2.libs.vtysh.ConfigVlan(subinterface_vlan) as ctx:
        ctx.no_shutdown()

    print("Configure l2 interface with vlan " + subinterface_vlan)
    configure_l2_interface(sw2, sw2p3, subinterface_vlan)
    print("Ping h2 to host 3")
    ping = hs2.libs.ping.ping(10, h3_ip_address)
    assert ping['received'] == 0,\
        'Ping between ' + h1_ip_address + ' and ' + h2_ip_address + ' passed'
