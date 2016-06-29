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

import os
import sys
from time import sleep

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
CFGD_CMD = "/usr/bin/ops_cfgd"
CFGD_DAEMON = "cfgd"
CFG_TBL_NOT_FOUND_MSG = "No rows found in the config table"
CFG_DATA_FOUND_MSG = "Config data found"
CUR_CFG_SET_MSG = "cur_cfg already set"
OVSDB = "/var/run/openvswitch/ovsdb.db"
CONFIGDB = "/var/local/openvswitch/config.db"
OVSDB_CLIENT_TRANSACT_CMD = "/usr/bin/ovsdb-client -v transact "
ADD_STARTUP_ROW_FILE = "./src/ops-cfgd/tests/add_startup_row"
ADD_TEST_ROW_FILE = "./src/ops-cfgd/tests/add_test_row"
GET_SYSTEM_TABLE_CMD = "ovs-vsctl list system"

'''
For now, only one function by the name of test is supported. To enable
multiple tests I wrote separate functions for each test and call them from the
main test function.
'''


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
    if (option == "noconfig"):
        sw1(OVSDB_STARTUP_CMD_NO_CONFIGDB, shell="bash")
    else:
        sw1(OVSDB_STARTUP_CMD_NORMAL, shell="bash")
    sleep(0.2)

    # start the platform daemons
    start_daemon(sw1, PLATFORM_DAEMONS)
    sleep(0.1)


def copy_startup_to_running_on_bootup(sw1):
    print("\n########## Test to copying startup to "
          "running config on bootup #########")
    # Configure sFlow and copy configuration to startup config
    # Now restart the system and
    # Verify that sFlow configuration is present after bootup

    sw1("configure terminal")
    sw1("sflow enable")
    sw1("sflow sampling 2048")
    sw1("sflow polling 20")
    sw1("sflow agent-interface 1")
    sw1("sflow collector 10.10.10.1")
    sw1("sflow header-size 100")
    sw1("sflow max-datagram-size 1000")
    sw1("exit")
    # sw1("copy running-config startup-config")
    sw1.libs.vtysh.copy_running_config_startup_config()
    sleep(5)
    # output = sw1("show running-config")
    sw1.libs.vtysh.show_running_config()

    # output = sw1("show startup-config")
    sw1.libs.vtysh.show_startup_config()

    restart_system(sw1, "normal")
    sleep(10)

    # Run ops_cfgd
    start_daemon(sw1, CFGD_DAEMON)
    sleep(10)

    status_daemon(sw1, CFGD_DAEMON)

    # FIXME
    output = sw1("show running-config")
    out = output.split('\n')
    word_index = out.index('sflow enable')

    assert "sflow collector 10.10.10.1" in out[word_index+1] \
           and "sflow agent-interface 1" in out[word_index+2] \
           and "sflow sampling 2048" in out[word_index+3] \
           and "sflow header-size 100" in out[word_index+4] \
           and "sflow max-datagram-size 1000" in out[word_index+5] \
           and "sflow polling 20" in out[word_index+6], 'Failed'
    print("\n### Passed: copy running to startup"
          " configuration on bootup ###")

    assert chk_cur_next_cfg(sw1)


def test_sflow_ft_resilient(topology, step):
    sw1 = topology.get('sw1')

    assert sw1 is not None

    copy_startup_to_running_on_bootup(sw1)
