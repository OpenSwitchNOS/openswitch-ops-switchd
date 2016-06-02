# -*- coding: utf-8 -*-
#
# (c) Copyright 2016 Hewlett Packard Enterprise Development LP
#
#  Licensed under the Apache License, Version 2.0 (the "License"); you may
#  not use this file except in compliance with the License. You may obtain
#  a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#  License for the specific language governing permissions and limitations
#  under the License.

"""
OpenSwitch Test for vlan related configurations.
"""

from pytest import mark
import threading
from time import sleep
import datetime
from .helpers import wait_until_interface_up

# from pytest import set_trace

TOPOLOGY = """
# +-------+                                    +-------+
# |       |     +--------+      +--------+     |       |
# |  h1   <---->    s1   <----->   s2    <----->  h2   |
# |       |     +--------+      +--------+     |       |
# +-------+                                    +-------+

# Nodes
[type=openswitch name="OpenSwitch 1"] s1
[type=openswitch name="OpenSwitch 2"] s2
[type=host name="Host 1"] h1
[type=host name="Host 2"] h2

# Links
h1:1 -- s1:1
s1:2 -- s2:2
s2:1 -- h2:1
"""

nping_succeeded = False
swnscmd = 'ip netns exec swns '
vsctlcmd = swnscmd + 'ovs-vsctl '
appctlcmd = swnscmd + 'ovs-appctl '


class GeneratorThread (threading.Thread):
    '''Generate traffic from the host'''
    def __init__(self, thread_id, host, src_ip, dst_ip, src_eth, dst_eth):
        threading.Thread.__init__(self)
        self.thread_id = thread_id
        self.host = host
        self.src_ip = src_ip
        self.dst_ip = dst_ip
        self.src_eth = src_eth
        self.dst_eth = dst_eth
        self.daemon = False

    def run(self):
        # global exit_flag
        print("===========")
        print("Generator start")
        # send contrived traffic
        # no broadcast, all unicast with well formed mac addrs
        nping_cmd = ('nping --udp '
                     '--source-port 42000 --dest-port 42000 '
                     '--source-ip ' + self.src_ip + ' ' +
                     '--dest-ip ' + self.dst_ip + ' ' +
                     '--dest-mac ' + self.dst_eth + ' ' +
                     '--source-mac ' + self.src_eth + ' ' +
                     '--ether-type 0x0800 '
                     '-c 1')
        print(nping_cmd)

        self.host(nping_cmd, shell='bash')

        print("Generator exit")


class ReceiverThread (threading.Thread):
    '''Receive traffic via tcpdump
       Update nping_succeeded when packets are found
       Waits 5 seconds for a packet
    '''
    def __init__(self, thread_id, host, src_ip, dst_ip):
        threading.Thread.__init__(self)
        self.thread_id = thread_id
        self.host = host
        self.src_ip = src_ip
        self.dst_ip = dst_ip
        self.daemon = False

    def run(self):
        global nping_succeeded
        print("===========")
        print("Receiver Start {}".format(self.thread_id))
        print(datetime.datetime.now())
        dumped_traffic = self.host('timeout 5 tcpdump -ni eth1 '
                                   'udp port 42000 '
                                   'and dst {0} '
                                   'and src {1}'.format(self.dst_ip,
                                                        self.src_ip))
        # dumped_traffic = self.host('timeout 5 tcpdump -ni eth1 '
        #                            'udp port 42000 ')
        sleep(10)
        print(datetime.datetime.now())
        print("DEBUG: dumped traffic dump:: ----")
        print(dumped_traffic)
        m = dumped_traffic.find("{0}.42000 > {1}.42000".format(self.src_ip,
                                self.dst_ip))
        print("{}: {}".format(self.thread_id, m))
        if m != -1:
            print("Receiver_{}: MATCH".format(self.thread_id))
            nping_succeeded = True
        else:
            print("Traffic not received")
        print("Receiver {} exit".format(self.thread_id))


@mark.test_id(1000)
def test_vxlan_simplest(topology):
    """
    Test ping between 2 hosts connected through VxLAN tunnel
    between 2 VTEPs OpenSwitch switches.

    Build a topology of two switches and two hosts and connect
    the hosts to the switches.
    Setup a VxLAN between switches and VLANs between ports
    and switches.
    Ping from host 1 to host 2.
    """
    h1_ip = '10.0.0.10'
    h2_ip = '10.0.0.20'
    s1_tun_ip = '20.0.0.1'
    s2_tun_ip = '20.0.0.2'
    all_mask = '/24'

    s1 = topology.get('s1')
    s2 = topology.get('s2')
    h1 = topology.get('h1')
    h2 = topology.get('h2')

    assert s1 is not None
    assert s2 is not None
    assert h1 is not None
    assert h2 is not None

    # Configure the swtich interfaces
    with s1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.no_routing()
        ctx.no_shutdown()

    with s1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.ip_address(s1_tun_ip + all_mask)
        ctx.no_shutdown()

    with s2.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.no_routing()
        ctx.no_shutdown()

    with s2.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.ip_address(s2_tun_ip + all_mask)
        ctx.no_shutdown()

    # Configure host names (only useful for debug purposes)
    # s1('hostname s1', shell='bash')
    # s2('hostname s2', shell='bash')
    # h1('hostname h1')
    # h2('hostname h2')

    # Configure host interfaces
    old_addr = None
    show_int = h1.libs.ip.show_interface('eth1')
    if (show_int):
        h1_mac = show_int['mac_address']
        if 'inet' in show_int.keys():
            old_addr = show_int['inet']
    if (old_addr):
        h1.libs.ip.remove_ip('1', "{}/{}".format(old_addr,
                             show_int['inet_mask']))
    h1.libs.ip.interface('1', addr=h1_ip + all_mask, up=True)

    old_addr = None
    show_int = h2.libs.ip.show_interface('eth1')
    if (show_int):
        h2_mac = show_int['mac_address']
        if 'inet' in show_int.keys():
            old_addr = show_int['inet']
    if (old_addr):
        h2.libs.ip.remove_ip('1', "{}/{}".format(old_addr,
                             show_int['inet_mask']))
    h2.libs.ip.interface('1', addr=h2_ip + all_mask, up=True)

    # Wait until switch interfaces are up
    for switch, portlbl in [(s1, '1'), (s1, '2'),
                            (s2, '1'), (s2, '2')]:
        wait_until_interface_up(switch, portlbl, timeout=15,
                                polling_frequency=3)

    # Wait until host interfaces are up
    hsp1_1 = h1.ports['1']
    hsp2_1 = h2.ports['1']
    show_hsp1_1 = ''
    show_hsp2_1 = ''
    attempts = 10
    while (
      ("{hsp1_1}: <BROADCAST,MULTICAST,UP,LOWER_UP>".format(**locals())
       not in show_hsp1_1) and
      ("{hsp2_1}: <BROADCAST,MULTICAST,UP,LOWER_UP>".format(**locals())
       not in show_hsp2_1) and
      attempts > 0):
        sleep(2)
        show_hsp1_1 = h1('ip link show {hsp1_1}'.format(**locals()))
        show_hsp2_1 = h2('ip link show {hsp2_1}'.format(**locals()))
        attempts -= 1

    # quit if interfaces never came up
    assert(attempts > 1)

    # is the tunnel peer reachable?
    # HACK: sending this traffic seems to be required before we
    # have our vport come up and are able to bind macs (no idea why)
    result = s1(swnscmd + 'ping ' + s2_tun_ip + ' -c 3', shell='bash')
    assert(result.find("64 bytes from") != -1)

    # Suggested debugging point...
    # set_trace()

    config_for_unimplemented_cli(s1, s2, s1_tun_ip, s2_tun_ip,
                                 h1_mac, h2_mac, '8000')

    # Suggested debugging point...
    # set_trace()

    pretend_some_mac_binding_happened(s1, s2, h1_mac, h2_mac, '8000')

    h2_thread = ReceiverThread('2', h2, h1_ip, h2_ip)
    h1_thread = GeneratorThread('1', h1, h1_ip, h2_ip,
                                h1_mac, h2_mac)

    h2_thread.start()
    sleep(1)
    h1_thread.start()

    h1_thread.join()
    h2_thread.join()
    assert(nping_succeeded)


# config_for_unimplemented_cli ()
# in both s1 and s2, we need to
#  1. create the logcal switch
#  2. bind a port to the lsw
#  3. create the tunnel interface/(v)port
#
# We do not have it yet, but one anticipates an eventual PI layer handling
# these commands via cli or rest or openflow or...
# At that point in the future, this fuction should be refactored away.
def config_for_unimplemented_cli(s1, s2, s1_tun_ip, s2_tun_ip,
                                 h1_mac, h2_mac, tunkey):

    global swnscmd
    global vsctlcmd
    global appctlcmd

    # instantiate Logical Switch on asic 0 in broadcom
    s1(appctlcmd + 'lsw 0 ' + tunkey,
       shell='bash')

    # bind port1 to our tunnel in OVSDB
    s1(vsctlcmd +
       'set port 1 vlan_options:tunnel_key=' + tunkey,
       shell='bash')
    # HACK. vxlan bug. These commands take too long, run them slowly
    sleep(2)

    # configure vxlan tunnel, vx1, in OVSDB
    s1(vsctlcmd +
       'add-port bridge_normal vx1 '
       '-- set Interface vx1 type=vxlan '
       'options:remote_ip=' + s2_tun_ip + ' '
       'options:key=' + tunkey,
       shell='bash')
    # HACK. vxlan bug. These commands take too long, run them slowly
    sleep(2)

    # reveal some debug dump for the log
    s1(appctlcmd +
       'tnl/dump', shell='bash')
    # HACK. vxlan bug. These commands take too long, run them slowly
    sleep(2)
    s1(appctlcmd +
       'vport/dump', shell='bash')

    # All done with s1
    # Now for s2

    # instantiate Logical Switch on asic 0 in broadcom
    s2(appctlcmd + 'lsw 0 ' + tunkey,
       shell='bash')
    # HACK. vxlan bug. These commands take too long, run them slowly
    sleep(2)

    # bind port1 to our tunnel in OVSDB
    s2(vsctlcmd +
       'set port 1 vlan_options:tunnel_key=' + tunkey,
       shell='bash')

    # HACK. vxlan bug. These commands take too long, run them slowly
    sleep(2)

    # configure vxlan tunnel, vx1, in OVSDB
    s2(vsctlcmd +
       'add-port bridge_normal vx1 '
       '-- set Interface vx1 type=vxlan '
       'options:remote_ip=' + s1_tun_ip + ' '
       'options:key=' + tunkey,
       shell='bash')
    # HACK. vxlan bug. These commands take too long, run them slowly
    sleep(2)

    # reveal some debug dump for the log
    s2(appctlcmd +
       'tnl/dump', shell='bash')
    s2(appctlcmd +
       'vport/dump', shell='bash')


# pretend_some_mac_binding_happened()
# BUM traffic is not yet handled. ARP is one part of BUM traffic. Without
# ARP, we do not have mac-learning/mac-binding in working order.
#
# One anticipates BUM Traffic handling and mac-learning/binding being
# implemented in the future. At that point, this fucntion should simply
# disapear (not be refactored)
def pretend_some_mac_binding_happened(s1, s2, h1_mac, h2_mac, tunkey):

    global appctlcmd

    # bind h2 (dst)mac to port vx1 in broadcom
    bindcmd = (appctlcmd +
               'bind/mac vx1 ' +
               tunkey + ' ' + h2_mac + ' 0')
    print(bindcmd)
    result = s1(bindcmd, shell='bash')
    assert(result.find("Sucess binding MAC") != -1)

    # bind h2 (dst)mac to port 1 in broadcom
    bindcmd = (appctlcmd +
               'bind/mac 1 ' +
               tunkey + ' ' + h2_mac + ' 1')
    print(bindcmd)
    # result = s2(appctlcmd +
    #               'bind/mac 1 ' +
    #               tunkey + ' ' + h2_mac + ' 1', shell='bash')
    result = s2(bindcmd, shell='bash')
    assert(result.find("Sucess binding MAC") != -1)


def cleanup(h1, h2, s1, s2, tunkey):

    global vsctlcmd
    global appctlcmd
    global s1_tun_ip
    global s2_tun_ip
    global h1_mac
    global h2_mac

    # remove host config (addreses on eth1)
    for h in [h1, h2]:
        old_addr = None
        show_int = h.libs.ip.show_interface('eth1')
        if (show_int):
            if 'inet' in show_int.keys():
                old_addr = show_int['inet']
        if (old_addr):
            h.libs.ip.remove_ip('1', "{}/{}".format(old_addr,
                                                    show_int['inet_mask']))

    # remove switch config (non-cli)
    cmd = (appctlcmd + 'unbind/mac ' + tunkey + ' ' + h2_mac)
    print('S1 & S2 Attempting: ' + cmd)
    s1(cmd, shell='bash')
    s2(cmd, shell='bash')

    # BUG: deleting vxlan tunnel port crashes ops-switchd
    # cmd = (vsctlcmd + 'del-port vx1 bridge_normal')
    # print('S1 & S2 Attempting: ' + cmd)
    # s1(cmd, shell='bash')
    # s2(cmd,shell='bash')

    cmd = (vsctlcmd + 'clear port 1 vlan_options')
    print('S1 & S2 Attempting: ' + cmd)
    s1(cmd, shell='bash')
    s2(cmd, shell='bash')

    # BUG / Feature Request: implement an inversion of ovs-appctl lsw 0 8000
    # cmd = (ppctlcmd + 'do some thing to delet lsw 0')
    # print('S1 & S2 Attempting: ' + cmd)
    # s1(cmd, shell='bash')
    # s2(cmd,shell='bash')

    # remove switch config (cli)
    for s in [s1, s2]:
        with s.libs.vtysh.ConfigInterface('1') as ctx:
            ctx.routing()
            ctx.shutdown()

    with s1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.remove_ip(s1_tun_ip)
        ctx.shutdown()

    with s2.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.remove_ip(s2_tun_ip)
        ctx.shutdown()
