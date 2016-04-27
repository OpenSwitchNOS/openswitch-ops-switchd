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
OpenSwitch Test for simple static routes between nodes.
"""

from __future__ import unicode_literals, absolute_import
from __future__ import print_function, division
import re

WORKSTATION1_IP = "10.1.1.1"
WORKSTATION1_MASK = "24"
WORKSTATION2_IP = "10.1.1.2"
WORKSTATION2_MASK = "24"
SWITCH_IP = "10.1.1.5"
SWITCH_MASK = "24"
PING_COUNT = "10"
REGEX_ETH1 = "eth1\s*Link encap:\w+\s+HWaddr [\S:]+\s*inet addr:[\d.]+"

TOPOLOGY = """
# +-------+                   +-------+
# |       |     +-------+     |       |
# |  hs1  <----->  sw1  <----->  hs3  |
# |       |     +-------+     |       |
# +-------+                   +-------+

# Nodes
[type=openswitch name="OpenSwitch 1"] sw1
[type=host name="Host 1"] hs1
[type=host name="Host 2"] hs2

# Links
hs1:1 -- sw1:1
sw1:2 -- hs2:1
"""


def configure_switch(switch, p1, p2):
    switch("conf t", shell="vtysh")
    switch("vlan 10", shell="vtysh")
    switch("no shutdown", shell="vtysh")
    switch("interface vlan 10", shell="vtysh")
    switch("no shutdown", shell="vtysh")
    switch("ip address %s/%s" % (SWITCH_IP, SWITCH_MASK), shell="vtysh")
    switch("interface {p1}".format(**locals()), shell="vtysh")
    switch("no shutdown", shell="vtysh")
    switch("no routing", shell="vtysh")
    switch("vlan access 10", shell="vtysh")
    switch("interface {p2}".format(**locals()), shell="vtysh")
    switch("no shutdown", shell="vtysh")
    switch("no routing", shell="vtysh")
    switch("vlan access 10", shell="vtysh")
    switch("exit", shell="vtysh")
    switch("exit", shell="vtysh")


def get_table_list(table_name, sw1):
    table_entries = sw1("ovsdb-client dump %s" % (table_name), shell="bash")
    table_entries_list = table_entries.split("\n")
    table_entries_list.reverse()
    table_entries_list.pop()
    table_entries_list.pop()
    table_entries_list.pop()

    return table_entries_list


def configure_ip_get_mac(workstation):
    eth1 = re.search(REGEX_ETH1, workstation("ifconfig"))
    if eth1:
        workstation("ifconfig eth1 0.0.0.0")
    workstation("ip addr add %s/%s dev eth1" %
                (WORKSTATION1_IP, WORKSTATION1_MASK))
    workstn_config = workstation("ifconfig")
    eth = re.findall(r'HWaddr [\S:]+', workstn_config)
    mac = str(eth[1].split(" ")[1])

    return mac


def get_mac_learnt_list_of_dict(mac_list, port_entries_list,
                                bridge_entries_list):
    mac_learnt_list = []
    for m in mac_list:
        bridge = ""
        for b in bridge_entries_list:
            if m[5] in b:
                bridge = b.split()[9].strip("\"")
                break
        for p in port_entries_list:
            if m[5] in p:
                plst = p.split()
                mac = m[4].strip("\"")
                port = plst[16].strip("\"")
                frm = m[3]
                break
        mac_learnt_list.append({"mac": mac, "port": port, "bridge": bridge,
                                "from": frm})

    return mac_learnt_list


def check_mac_learning(mac_learnt_list, mac1, mac2, p1, p2):
    mac_learnt_flag = True
    for pair in mac_learnt_list:
        if not(((pair["mac"] == str(mac1) and pair["port"] == str(p1)) or
                (pair["mac"] == str(mac2) and pair["port"] == str(p2))) and
               pair["from"] == "learning"):
            mac_learnt_flag = False

    return mac_learnt_flag


def test_maclearning(topology, step):
    """
    Updates the Mac vs port entries in the OVSDB and verifies the same.
    """
    sw1 = topology.get('sw1')
    hs1 = topology.get('hs1')
    hs2 = topology.get('hs2')

    assert sw1 is not None
    assert hs1 is not None
    assert hs2 is not None

    p1 = sw1.ports["1"]
    p2 = sw1.ports["2"]

    # Get the mac addresses of workstations in the topology
    mac1 = configure_ip_get_mac(hs1)
    mac2 = configure_ip_get_mac(hs2)

    # Configure the switch for vlan and interfaces
    configure_switch(sw1, p1, p2)

    # Ping from workstation1 to workstation2 to update the MAC table in
    # database
    hs1("ping -c %s %s" % (PING_COUNT, WORKSTATION2_IP), shell="bash")

    # Get the mac entries in list of list format from database dump
    mac_entries_list = get_table_list("MAC", sw1)
    mac_list = []
    for m in mac_entries_list:
        mac_list.append(m.split())

    assert len(mac_list) >= 2, "MAC table not updated properly"

    # Get the bridge and port entries in list of list format from database dump
    port_entries_list = get_table_list("Port", sw1)
    bridge_entries_list = get_table_list("Bridge", sw1)

    # Create a list of dictionaries
    # Each dictionary is a combination of mac address, port number, from flag
    # and bridge name required for the verification of MAC Learning feature
    mac_learnt_list = get_mac_learnt_list_of_dict(mac_list, port_entries_list,
                                                  bridge_entries_list)
    mac_learnt_flag = check_mac_learning(mac_learnt_list, mac1, mac2, p1, p2)
    assert mac_learnt_flag, "MAC Learning failed"
