# (C) Copyright 2016 Hewlett Packard Enterprise Development LP
# All Rights Reserved.
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.


import pytest
from pytest import mark
from time import sleep
import re

TOPOLOGY = """
#
# +-------+
# |  sw1  |
# +-------+
#

# Nodes
[type=openswitch name="Switch 1"] sw1
"""


def configure_switch(sw1, step):
    # Configuring sFlow

    sampling_rate = 2048
    polling_interval = 20
    collector = '10.10.10.1'
    header_size = 100
    datagram_size = 1000

    step("### Configuring sFlow ###")
    with sw1.libs.vtysh.Configure() as ctx:
        ctx.sflow_enable()
        ctx.sflow_sampling(sampling_rate)
        ctx.sflow_polling(polling_interval)
        ctx.sflow_agent_interface('1')
        ctx.sflow_collector(collector)
        ctx.sflow_header_size(header_size)
        ctx.sflow_max_datagram_size(datagram_size)


@mark.platform_incompatible(['docker'])
@pytest.mark.timeout(1000)
def test_sflow_ft_reboot_config_persistence(topology, step):

    sw1 = topology.get('sw1')

    assert sw1 is not None
    configure_switch(sw1, step)

    step("Save Running Config and Reboot")

    step("Copying running config to startup config")
    vtysh = sw1.get_shell('vtysh')
    vtysh.send_command('copy running-config startup-config', timeout=100)

    step("Showing running-config")
    vtysh = sw1.get_shell('vtysh')
    vtysh.send_command('show running-config', timeout=100)

    step("Showing startup-config")
    vtysh = sw1.get_shell('vtysh')
    vtysh.send_command('show startup-config', timeout=100)
    sleep(20)

    step("Rebooting switch")
    sw1.libs.reboot.reboot_switch()
    sleep(30)

    # --------------Test Switch After Reboot------------------

    step("Verifying Configuration")
    output = sw1("show running-config")
    sleep(10)
    sflow_info_re = (
        r'sflow\senable\s*'
        r'\s*sflow\scollector\s10.10.10.1\s*'
        r'\s*sflow\sagent-interface\s1\s*'
        r'\s*sflow\ssampling\s2048\s*'
        r'\s*sflow\sheader-size\s100\s*'
        r'\s*sflow\smax-datagram-size\s1000\s*'
        r'\s*sflow\spolling\s20'
    )
    re_result = re.search(sflow_info_re, str(output))
    assert re_result, "Failed to verify sFlow configuration \
        persistence after reboot"
    step("\n### Passed: copy running to startup"
         " configuration after rebooting device ###")
