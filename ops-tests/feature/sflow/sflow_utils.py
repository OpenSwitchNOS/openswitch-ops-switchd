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
sFlow utility functions used in test cases.
"""


def check_ping_sample(sflow_output, host1, host2, agent_address, family):
    """
    Parse sflowtool output to look for a specific ping request and
    ping response between two hosts.

    :param str sflow_output: dict of parsed sflowtool output
    :param str host1: IP address of host which sends the ping rquest
    :param str host2: IP address of host which sends the ping response
    :param str agent_address: sFlow agent IP address
    :return bool result: A boolean value to indicate presence of ping packets
                         in sFlow samples (Both request and response)
    """

    assert sflow_output
    assert host1, host2
    assert agent_address
    assert family

    ping_request = False
    ping_response = False

    for packet in sflow_output['packets']:
        if ping_request is False and \
                family == 'ipv6' and \
                packet['packet_type'] == 'FLOW' and \
                int(packet['icmp_type']) == 0 and \
                packet['ip_protocol'] == '58' and \
                packet['src_ip'] == host1 and \
                packet['dst_ip'] == host2:
                    ping_request = True
                    assert packet['agent_address'] == agent_address
        if ping_response is False and \
                family == 'ipv6' and \
                packet['packet_type'] == 'FLOW' and \
                int(packet['icmp_type']) == 0 and \
                packet['ip_protocol'] == '58' and \
                packet['src_ip'] == host2 and \
                packet['dst_ip'] == host1:
                    ping_response = True
                    assert packet['agent_address'] == agent_address
        if ping_request is False and \
                family == 'ipv4' and \
                packet['packet_type'] == 'FLOW' and \
                int(packet['icmp_type']) == 8 and \
                packet['src_ip'] == host1 and \
                packet['dst_ip'] == host2:
                    ping_request = True
                    assert packet['agent_address'] == agent_address
        if ping_response is False and \
                family == 'ipv4' and \
                packet['packet_type'] == 'FLOW' and \
                int(packet['icmp_type']) == 0 and \
                packet['ip_protocol'] == '1' and \
                packet['src_ip'] == host2 and \
                packet['dst_ip'] == host1:
                    ping_response = True
                    assert packet['agent_address'] == agent_address
        if ping_request and ping_response:
            break

    # TODO: Need to check for both ping request and response(CR 1983)
    result = ping_request or ping_response
    return result
