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
OpenSwitch Test for ECMP Resilient Hash
"""
from pytest import mark
from time import sleep
from logging import getLogger
import threading

TOPOLOGY = """
#                             +-------+
#                             |       |
#                    +-------->  nh1  |
#                    |        |       |
#                    |        +-------+
#                    |
# +-------+          |        +-------+
# |       |     +----v--+     |       |
# |  hs1  <----->  sw1  <----->  nh2  |
# |       |     +----^--+     |       |
# +-------+          |        +-------+
#                    |
#                    |        +-------+
#                    |        |       |
#                    +-------->  nh3  |
#                             |       |
#                             +-------+

# Nodes
[type=openswitch name="Switch 1"] sw1
[type=host name="Host 1"] hs1
[type=host name="Nexthop 1"] nh1
[type=host name="Nexthop 2"] nh2
[type=host name="Nexthop 3"] nh3

# Links
sw1:1 -- nh1:1
sw1:2 -- nh2:1
sw1:3 -- nh3:1
sw1:4 -- hs1:1
"""

exit_flag = False   # 1 writer, many readers
nexthop_lock = threading.Lock()
nexthop_list = []   # All can write
threads = []
log = getLogger(__name__)


def my_assert(cond, msg=None):
    global exit_flag
    global threads
    global log
    if not cond:
        exit_flag = True
        for t in threads:
            t.join(5)
        if msg:
            log.error(msg)
    assert cond


class GeneratorThread (threading.Thread):
    '''Generate traffic from the host until a nexthop is found'''
    def __init__(self, thread_id, host, src, dst):
        threading.Thread.__init__(self)
        self.thread_id = thread_id
        self.host = host
        self.src = src
        self.dst = dst
        self.daemon = True

    def run(self):
        global exit_flag
        global log
        log.debug("Generator start")
        while not exit_flag:
            generated_traffic = self.host('ping -c 10 -i 1 '
                                          '{0}'.format(self.dst))
            my_assert(generated_traffic is not None, "no ping output")
        log.debug("Generator exit")


class ReceiverThread (threading.Thread):
    '''Receive traffic on the nexthop via tcpdump
       Update the nexthop_list when packets are found
    '''
    def __init__(self, thread_id, host, src, dst):
        threading.Thread.__init__(self)
        self.thread_id = thread_id
        self.host = host
        self.src = src
        self.dst = dst
        self.daemon = True

    def run(self):
        global nexthop_lock
        global nexthop_list
        global exit_flag
        global log
        log.debug("Receiver {} start".format(self.thread_id))
        while not exit_flag:
            dumped_traffic = self.host('timeout 5 tcpdump -ni eth1 '
                                       'dst {0}'.format(self.dst))
            m = dumped_traffic.find("{0} > {1}".format(self.src, self.dst))
            log.debug("{}: {}".format(self.thread_id, m))
            if m != -1 and (self.thread_id not in nexthop_list):
                log.debug("{}: MATCH".format(self.thread_id))
                if nexthop_lock.acquire(False):
                    # Main thread may have cleared the list by the time we got
                    # here
                    nexthop_list.append(self.thread_id)
                    nexthop_lock.release()
                log.debug("{}: {}".format(self.thread_id, nexthop_list))
        log.debug("Receiver {} exit".format(self.thread_id))


def run_ecmp_cycle(hs1, nh1, nh2, nh3):
    '''Run a test cycle.
       1. Create the receiver and generator threads
       2. Launch the threads to see which nexthop(s) see traffic
       3. Shut down the threads
    '''
    global nexthop_list
    global exit_flag
    global threads

    exit_flag = False
    threads = []

    nh1_thread = ReceiverThread('1.0.0.1', nh1, '20.0.0.10', '70.0.0.1')
    nh2_thread = ReceiverThread('2.0.0.1', nh2, '20.0.0.10', '70.0.0.1')
    nh3_thread = ReceiverThread('3.0.0.1', nh3, '20.0.0.10', '70.0.0.1')
    hs1_thread = GeneratorThread('20.0.0.10', hs1, '20.0.0.10', '70.0.0.1')

    threads.append(nh1_thread)
    threads.append(nh2_thread)
    threads.append(nh3_thread)
    threads.append(hs1_thread)

    nexthop_list = []

    # Make sure all the next hops are in the ARP cache
    hs1.libs.ping.ping(1, '1.0.0.1')
    hs1.libs.ping.ping(1, '2.0.0.1')
    hs1.libs.ping.ping(1, '3.0.0.1')

    nh1_thread.start()
    nh2_thread.start()
    nh3_thread.start()
    sleep(2)
    hs1_thread.start()
    sleep(2)

    attempts = 5
    # ReceiverThreads will append IPs to nexthop_list when they see traffic
    while not nexthop_list and attempts > 0:
        attempts -= 1
        sleep(15)

    exit_flag = True
    for t in threads:
        t.join(5)


@mark.test_id(10000)
@mark.platform_incompatible(['docker'])
def test_ecmp(topology):
    """
    Set network addresses and static routes between a host, switch, and 3
    hosts acting as next hops. Use the response from ping to ensure flows
    not disrupted when a next hop is removed or added
    """

    global nexthop_list
    global log

    sw1 = topology.get('sw1')
    hs1 = topology.get('hs1')
    nh1 = topology.get('nh1')
    nh2 = topology.get('nh2')
    nh3 = topology.get('nh3')

    assert sw1 is not None
    assert hs1 is not None
    assert nh1 is not None
    assert nh2 is not None
    assert nh3 is not None

    ps1hs1 = sw1.ports['4']
    ps1nh1 = sw1.ports['1']
    ps1nh2 = sw1.ports['2']
    ps1nh3 = sw1.ports['3']
    phs1 = hs1.ports['1']
    pnh1 = nh1.ports['1']
    pnh2 = nh2.ports['1']
    pnh3 = nh3.ports['1']

    # Configure IP and bring UP host 1 interfaces
    hs1.libs.ip.interface('1', addr='20.0.0.10/24', up=True)

    # Configure IP and bring UP nexthop 1 interfaces
    nh1.libs.ip.interface('1', addr='1.0.0.1/24', up=True)

    # Configure IP and bring UP nexthop 2 interfaces
    nh2.libs.ip.interface('1', addr='2.0.0.1/24', up=True)

    # Configure IP and bring UP nexthop 3 interfaces
    nh3.libs.ip.interface('1', addr='3.0.0.1/24', up=True)

    # Configure IP and bring UP switch 1 interfaces
    # sw1 <-> hs1
    with sw1.libs.vtysh.ConfigInterface('4') as ctx:
        ctx.ip_address('20.0.0.1/24')
        ctx.no_shutdown()

    # sw1 <-> nh1
    with sw1.libs.vtysh.ConfigInterface('1') as ctx:
        ctx.ip_address('1.0.0.2/24')
        ctx.no_shutdown()

    # sw1 <-> nh2
    with sw1.libs.vtysh.ConfigInterface('2') as ctx:
        ctx.ip_address('2.0.0.2/24')
        ctx.no_shutdown()

    # sw1 <-> nh3
    with sw1.libs.vtysh.ConfigInterface('3') as ctx:
        ctx.ip_address('3.0.0.2/24')
        ctx.no_shutdown()

    # now wait for interfaces to come up
    show_int1 = ''
    show_int2 = ''
    show_int3 = ''
    show_int4 = ''
    attempts = 10
    while (
      ("Interface {ps1hs1} is up".format(**locals()) not in show_int1) and
      ("Interface {ps1nh1} is up".format(**locals()) not in show_int2) and
      ("Interface {ps1nh2} is up".format(**locals()) not in show_int3) and
      ("Interface {ps1nh3} is up".format(**locals()) not in show_int4) and
      attempts > 0):
        sleep(2)
        show_int1 = sw1('show interface {ps1hs1}'.format(**locals()))
        show_int2 = sw1('show interface {ps1nh1}'.format(**locals()))
        show_int3 = sw1('show interface {ps1nh2}'.format(**locals()))
        show_int4 = sw1('show interface {ps1nh3}'.format(**locals()))
        attempts = attempts - 1

    # Set ECMP static routes in switch
    with sw1.libs.vtysh.Configure() as ctx:
        ctx.ip_route('70.0.0.0/24', '1.0.0.1')
        ctx.ip_route('70.0.0.0/24', '2.0.0.1')
        ctx.ip_route('70.0.0.0/24', '3.0.0.1')

    sw1('ip netns exec swns ip route list', shell="bash")

    # Set gateway in host
    hs1.libs.ip.add_route('default', '20.0.0.1')

    # Next hops need a route back to the hs1 for the icmp replies
    nh1.libs.ip.add_route('20.0.0.0/24', '1.0.0.2')
    nh2.libs.ip.add_route('20.0.0.0/24', '2.0.0.2')
    nh3.libs.ip.add_route('20.0.0.0/24', '3.0.0.2')

    run_ecmp_cycle(hs1, nh1, nh2, nh3)

    # Make sure we found one and only one next hop
    my_assert(len(nexthop_list) == 1,
              "nexthop_list len = {}".format(len(nexthop_list)))

    # Make a note of the first nexthop selected
    first_nexthop = nexthop_list[0]
    log.info("first_nexthop:{}".format(first_nexthop))

    # Shut off one of the unused next hops; make sure the current one is not
    # disturbed
    if nexthop_list[0] == '1.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('2') as ctx:
            ctx.shutdown()
    if nexthop_list[0] == '2.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('3') as ctx:
            ctx.shutdown()
    if nexthop_list[0] == '3.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('1') as ctx:
            ctx.shutdown()

    log.info("Shut down a non-selected port")
    sw1('ip netns exec swns ip route list', shell="bash")

    run_ecmp_cycle(hs1, nh1, nh2, nh3)

    # Make sure we found one and only one next hop
    my_assert(len(nexthop_list) == 1,
              "nexthop_list len = {}".format(len(nexthop_list)))
    # Make sure the flow didn't move to another nexthop
    my_assert(nexthop_list[0] == first_nexthop,
              "nexthop_list is {} but should be {}".format(nexthop_list,
                                                           first_nexthop))

    # Turn the unused next hop back on; make sure the current one is not
    # disturbed
    if first_nexthop == '1.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('2') as ctx:
            ctx.no_shutdown()
    if first_nexthop == '2.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('3') as ctx:
            ctx.no_shutdown()
    if first_nexthop == '3.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('1') as ctx:
            ctx.no_shutdown()

    log.info("Re-Enable a non-selected port")
    sw1('ip netns exec swns ip route list', shell="bash")

    run_ecmp_cycle(hs1, nh1, nh2, nh3)

    # Make sure we found one and only one next hop
    my_assert(len(nexthop_list) == 1,
              "nexthop_list len = {}".format(len(nexthop_list)))
    # On adding a next hop, it is possible flows will shift
    # my_assert(nexthop_list[0] == first_nexthop,
    #           "nexthop_list is {} but should be {}".format(nexthop_list,
    #                                                        first_nexthop))

    # Now we want to turn off the next hop in use
    if first_nexthop == '1.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('1') as ctx:
            ctx.shutdown()
    if first_nexthop == '2.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('2') as ctx:
            ctx.shutdown()
    if first_nexthop == '3.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('3') as ctx:
            ctx.shutdown()

    log.info("Shut down the selected port")
    sw1('ip netns exec swns ip route list', shell="bash")

    run_ecmp_cycle(hs1, nh1, nh2, nh3)

    # Because we lock, clear, and release the nexthop_list
    # there should only be one entry
    my_assert(len(nexthop_list) == 1,
              "nexthop_list len = {}".format(len(nexthop_list)))
    # Make sure the flow goes to another next hop
    my_assert(first_nexthop not in nexthop_list,
              "first_nexthop {} is in nexthop_list {}".format(first_nexthop,
                                                              nexthop_list))

    second_nexthop = nexthop_list[0]
    log.info("second_nexthop:{}".format(second_nexthop))

    # Re-enable the first next hop
    if first_nexthop == '1.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('1') as ctx:
            ctx.no_shutdown()
    if first_nexthop == '2.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('2') as ctx:
            ctx.no_shutdown()
    if first_nexthop == '3.0.0.1':
        with sw1.libs.vtysh.ConfigInterface('3') as ctx:
            ctx.no_shutdown()

    log.info("Re-enable the selected port")
    sw1('ip netns exec swns ip route list', shell="bash")

    run_ecmp_cycle(hs1, nh1, nh2, nh3)

    # Because we lock, clear, and release the nexthop_list
    # there should only be one entry
    my_assert(len(nexthop_list) == 1,
              "nexthop_list len = {}".format(len(nexthop_list)))
    # On adding a next hop, it is possible flows will shift
    # my_assert(first_nexthop not in nexthop_list,
    #           "first_nexthop {} is in nexthop_list {}".format(first_nexthop,
    #                                                           nexthop_list))
