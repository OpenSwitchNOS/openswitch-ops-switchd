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
import os
import sys
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

if 'BUILD_ROOT' in os.environ:
    BUILD_ROOT = os.environ['BUILD_ROOT']
else:
    BUILD_ROOT = "../../.."

OPS_VSI_LIB = BUILD_ROOT + "/src/ops-vsi"
sys.path.append(OPS_VSI_LIB)

ALL_DAEMONS = "ops-sysd ops-pmd ops-tempd ops-powerd ops-ledd ops-fand"\
              " switchd ops-intfd ops-vland ops-lacpd"\
              " ops-lldpd ops-zebra ops-bgpd ovsdb-server"

PLATFORM_DAEMONS = "ops-sysd ops-pmd ops-tempd ops-powerd ops-ledd ops-fand"

CREATE_OVSDB_CMD = "/usr/bin/ovsdb-tool create /var/run/openvswitch/ovsdb.db"\
                   " /usr/share/openvswitch/vswitch.ovsschema"

CREATE_CONFIGDB_CMD = "/usr/bin/ovsdb-tool create /var/local/openvswitch/"\
                      "config.db /usr/share/openvswitch/configdb.ovsschema"

OVSDB_STARTUP_CMD_NORMAL = "/usr/sbin/ovsdb-server --remote=punix:/var/run/"\
                           "openvswitch/db.sock --detach --no-chdir --pidfile"\
                           " -vSYSLOG:INFO /var/run/openvswitch/ovsdb.db "\
                           "/var/local/openvswitch/config.db"

OVSDB_STARTUP_CMD_NO_CONFIGDB = "/usr/sbin/ovsdb-server --remote=punix:/var/"\
                                "run/openvswitch/db.sock --detach --no-chdir"\
                                " --pidfile -vSYSLOG:INFO "\
                                "/var/run/openvswitch/ovsdb.db"

OVSDB_STOP_CMD = "kill -9 `cat /var/run/openvswitch/ovsdb-server.pid`"
CFGD_DAEMON = "cfgd"
OVSDB = "/var/run/openvswitch/ovsdb.db"
CONFIGDB = "/var/local/openvswitch/config.db"
GET_SYSTEM_TABLE_CMD = "ovs-vsctl list system"


def stop_daemon(sw1, daemon):
    sw1("/bin/systemctl stop " + daemon, shell="bash")
    sw1("echo", shell="bash")


def start_daemon(sw1, daemon):
    sw1("/bin/systemctl start " + daemon, shell="bash")
    sw1("echo", shell="bash")


def status_daemon(sw1, daemon):
    out = sw1("/bin/systemctl status " + daemon + " -l", shell="bash")
    out += sw1("echo", shell="bash")
    return out


def remove_db(sw1, db):
    sw1("/bin/rm -f " + db, shell="bash")


def create_db(sw1, db):
    sw1(db, shell="bash")


def rebuild_dbs(sw1):
    remove_db(sw1, OVSDB)
    create_db(sw1, CREATE_OVSDB_CMD)
    # remove_db(sw1, CONFIGDB))
    create_db(sw1, CREATE_CONFIGDB_CMD)


def chk_cur_next_cfg(sw1):
    table_out = sw1(GET_SYSTEM_TABLE_CMD, shell="bash")
    table_out += sw1("echo", shell="bash")
    mylines = table_out.splitlines()

    found_cur = False
    found_next = False
    for x in mylines:
        pair = x.split(':')
        if "cur_cfg" in pair[0]:
            if int(pair[1]) > 0:
                found_cur = True
        elif "next_cfg" in pair[0]:
            if int(pair[1]) > 0:
                found_next = True

    return found_cur and found_next


def restart_system(sw1, option):
    # Stop all daemons
    stop_daemon(sw1, ALL_DAEMONS)

    # stop any manually started ovsdb-server
    sw1(OVSDB_STOP_CMD, shell="bash")

    # remove and recreate the dbs
    rebuild_dbs(sw1)

    # start ovsdb-server with or without configdb

    sw1(OVSDB_STARTUP_CMD_NORMAL, shell="bash")

    # start the platform daemons
    start_daemon(sw1, PLATFORM_DAEMONS)
    sleep(0.1)


def copy_startup_to_running_on_bootup(sw1):
    # Configure sFlow and copy configuration to startup config
    # Now restart the system and
    # Verify that sFlow configuration is present after bootup

    sampling_rate = 2048
    polling_interval = 20
    collector = '10.10.10.1'
    header_size = 100
    datagram_size = 1000

    print("### Configuring sFlow ###")
    with sw1.libs.vtysh.Configure() as ctx:
        ctx.sflow_enable()
        ctx.sflow_sampling(sampling_rate)
        ctx.sflow_polling(polling_interval)
        ctx.sflow_agent_interface('1')
        ctx.sflow_collector(collector)
        ctx.sflow_header_size(header_size)
        ctx.sflow_max_datagram_size(datagram_size)

    vtysh = sw1.get_shell('vtysh')
    vtysh.send_command('copy running-config startup-config', timeout=100)
    sleep(20)

    vtysh = sw1.get_shell('vtysh')
    vtysh.send_command('show running-config', timeout=100)

    vtysh = sw1.get_shell('vtysh')
    vtysh.send_command('show startup-config', timeout=100)
    sleep(20)

    restart_system(sw1, "normal")
    sleep(10)

    # Run ops_cfgd
    start_daemon(sw1, CFGD_DAEMON)
    sleep(10)

    status_daemon(sw1, CFGD_DAEMON)

    # FIXME
    output = sw1("show running-config")
    sflow_info_re = (
        r'sflow\senable\s*'
        r'\s*sflow\scollector\s10.10.10.1\s*'
        r'\s*sflow\sagent-interface\s1\s*'
        r'\s*sflow\ssampling\s2048\s*'
        r'\s*sflow\sheader-size\s100\s*'
        r'\s*sflow\smax-datagram-size\s1000\s*'
        r'\s*sflow\spolling\s20'
    )

    re_result = re.search(sflow_info_re, output)
    assert re_result
    print("\n### Passed: copy running to startup"
          " configuration on bootup ###")


@pytest.mark.timeout(1000)
def test_sflow_ft_reboot_config_persistence(topology, step):
    sw1 = topology.get('sw1')

    assert sw1 is not None

    copy_startup_to_running_on_bootup(sw1)
