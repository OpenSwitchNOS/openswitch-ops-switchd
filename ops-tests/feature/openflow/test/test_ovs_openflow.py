#
# Copyright (C) 2016 Broadcom Limited
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

# -*- coding: utf-8 -*-

# ################## TEST DESCRIPTION #######################
# This script validates the flow / group configuration for
# Bridging scenario through Open Flow 1.3
# using ovs-ofctl utility on openswitch
# ####################### END ###############################

from time import sleep
import re
import sys
from six import string_types

TOPOLOGY = """
# This string is known as a SZN string, it describes the topology that is to be
# used by every test case in this suite.
# Lines that begin with a # sign (as this one) are comments and are ignored by
# the SZN parser.
#
# This is a diagram of the topology, it consists of one openswitch
#
# +-------+
# |       |
# |  ops1 |
# |       |
# +-------+
#
# Every element in the topology is known as a node. The node type defines its
# behavior and capabilities.
[type=openswitch name="OpenSwitch 1"] ops1
ops1:port1
"""
# Global variables for tcp port, flow / group configuration
# in a dictionary for multi-purpose use
# "config" dictionary will be parsed to create a ops-ofctl
# command for add / get flows / groups
# We have defined predefined attribute list for flow / groups
# for verifying the flow / group stats
tcp_port = 6643
config = {
              "flow": [{
                "table": 10,
                "in_port": 3,
                "dl_vlan": 100,
                "actions": [{
                    "set_field": {
                        "tunnel_id": 343
                    },
                    "goto_table": 20
                }]
              }, {
                "table": 50,
                "tunnel_id": 343,
                "dl_dst": "00:00:00:00:00:11",
                "actions": [{
                    "group": "0x00640004",
                    "goto_table": 60
                }]
              }],
              "group": [{
                "group_id": "0x00640004",
                "type": "all",
                "bucket": [{
                    "output": 4
                }]
              }]
        }
group_attrs = ["group_id", "type", "bucket"]
flow_attrs = ["table", "in_port", "dl_vlan", "actions", "goto_table", "tun_id", "dl_dst"]
ports_list = [3, 4]


# Test case method contains pre-defined variables
# with default values and initialized the node identifier
# Using this method we are parsing the "config"
# dictionary to prepare ovs-ofctl add flow / group commands
# We are creating a user defined bridge as "bridge_ovs_openflow"
# and adding ports to it.
# Parse and verification process of the installed flow / group values with
# configures ones will be done using a user defined method
def test_ovs_openflow(topology):
    bridge_name = "bridge_ovs_openflow"
    open_flow = "OpenFlow13"
    ops1 = topology.get("ops1")
    ovs_ofctl_cmd = "ovs-ofctl -O "+open_flow
    flow_cmds = list()
    flow_del_cmds = list()
    group_cmds = list()
    flow_tables = list()
    group_ids = list()
    temp = ""
    try:
        # Creating user defined bridge and enabling
        # the open flow agent in passive mode
        # After successfully enabling the agent, we are configuring ports and
        # verifying the same
        # Verifying the agent is up and running
        ops1("ovs-vsctl add-br "+str(bridge_name), shell="bash")
        if len(ports_list) > 0:
            for port in ports_list:
                ops1("ovs-vsctl add-port {} {}".format(bridge_name, port), shell="bash")
            print("Fetching and verifying the configured ports from {}".format(bridge_name))
            ports = ops1("ovs-vsctl list-ports {}".format(bridge_name), shell="bash")
            for port in ports.split("\n"):
                port = str(port)
                if int(port) not in ports_list:
                    print("ERROR: Ports matching failed ...")
                    assert False
        else:
            print("ERROR: Setup is not defined with ports")
        sleep(5)
        bridge_details = ops1(ovs_ofctl_cmd+" show "+bridge_name, shell="bash")
        re.search('\saddr:(.*)\s', bridge_details).group(1)
        # Iterating the "config" dictionary w.r.t
        # flow and groups to prepare the ovs-ofctl commands.
        # Installing flows /groups with the commands
        # and verifying them by fetching them back
        # Once the verification is successful,
        # we are deleting the flow / group and verifying the same.
        for label, data in config.items():
            if not isinstance(data, string_types):
                if label == "flow":
                    for flow in data:
                        flow_cmd = ovs_ofctl_cmd+" add-flow "+bridge_name+" "
                        flow_del_cmd = ovs_ofctl_cmd+" del-flows "+bridge_name+" "
                        actions = " "
                        dummy_str = ""
                        if isinstance(flow, dict):
                            flow_tables.append(flow.get("table"))
                            for field, value in flow.items():
                                if field != "actions":
                                    flow_cmd = flow_cmd+field+"="+str(value)+","
                                    dummy_str = dummy_str+field+"="+str(value)+","
                                else:
                                    actions = field+"="
                                    if not isinstance(value, string_types):
                                        for sub_field in value:
                                            for act_field, act_val in sub_field.items():
                                                if act_field != "set_field":
                                                    if act_field == "goto_table":
                                                        go_to_table = str(act_field)+":"+str(act_val)
                                                    else:
                                                        actions = actions+str(act_field)+":"+str(act_val)+","
                                                else:
                                                    actions = actions+act_field+":"
                                                    if isinstance(act_val, dict):
                                                        for action, sub_value in act_val.items():
                                                            actions = actions+str(sub_value)+'-\>'+action+","
                                                    else:
                                                        pass
                                            actions = "{0},{1}".format(actions, go_to_table)
                                    else:
                                        pass
                        else:
                            pass
                        flow_cmd = flow_cmd+actions
                        flow_cmds.append(flow_cmd.strip(","))
                        flow_del_cmds.append(flow_del_cmd+'"{}"'.format(dummy_str.strip(",")))
                elif label == "group":
                    for group in data:
                        group_cmd = ovs_ofctl_cmd+" add-group "+bridge_name+" "
                        bucket = "bucket="
                        grp_id = ""
                        grp_type = ""
                        if isinstance(group, dict):
                            for field, value in group.items():
                                if field != "bucket":
                                    if field == "group_id":
                                        grp_id = field+"="+str(value)
                                        group_ids.append(str(value))
                                    elif field == "type":
                                        grp_type = field+"="+str(value)
                                else:
                                    for bucket_data in value:
                                        for bkt_field, bkt_value in bucket_data.items():
                                            bucket = bucket+bkt_field+":"+str(bkt_value)+","
                            group_cmd = "{0} {1},{2},".format(group_cmd, grp_id, grp_type)
                        else:
                            pass
                    group_cmds.append(group_cmd+bucket.strip(","))
                    print(group_cmds)
                else:
                    pass
            else:
                pass
        if len(flow_tables) <= 0:
            print("ERROR: Flow tables are not found.")
            assert False
        if(len(group_cmds) > 0):
            for grp_cmd in group_cmds:
                ops1(grp_cmd, shell="bash")
                parse_verify_output("group", ops1(ovs_ofctl_cmd+" dump-groups "+bridge_name, shell="bash"), group_id="0x00640004")
        if(len(flow_cmds) > 0):
            flow_index = 0
            for flw_cmd in flow_cmds:
                ops1(flw_cmd, shell="bash")
                parse_verify_output("flow", ops1(ovs_ofctl_cmd+" dump-flows "+bridge_name+" table="+str(flow_tables[flow_index]),
                                    shell="bash"), table=str(flow_tables[flow_index]))
                flow_index = flow_index+1
        if len(flow_del_cmds) > 0:
            for flow_del in flow_del_cmds:
                delete_verify_config("flow", ops1, flow_del)
        if len(group_ids) > 0:
            for grp_id_value in group_ids:
                grp_cmd = "{} del-groups {} group_id='{}'".format(ovs_ofctl_cmd, bridge_name, grp_id_value)
                delete_verify_config("group", ops1, grp_cmd)
        # set_trace()
    except Exception as e:
        print(print_exception(e))
        assert False


# Method to fetch the ip address from ifconfig eth0 command result
# This method accepts the switch object to execute the required command
def get_eth_ip_address(device):
    assert device is not None
    try:
        if_config = device("ifconfig eth0", shell="bash")
        ip_address = re.search('inet\saddr:(.+?)\s', if_config).group(1)
        return ip_address
    except Exception as e:
        print(print_exception(e))
        assert False


# Method to check the open-switchd process
# and re-starting for one time if it is not started
def restart_open_switchd(openswitch, process, flag):
    if process and not process.isspace():
        pass
    else:
        if flag == 0:
            openswitch("killall ops-switchd", shell="bash")
            sleep(5)
            openswitch("/usr/sbin/ops-switchd --no-chdir "
                       "--pidfile --detach -vSYSLOG:INFO &", shell="bash")
            sleep(10)
            process = openswitch("netstat -at | "
                                 "grep "+str(tcp_port), shell="bash")
            restart_open_switchd(openswitch, process, 1)
        else:
            print(print_exception("ops-switchd process is not found."
                  "Hence terminating the execution ...."))
            assert False


# Method to parse add verify the configuration
# based on the provided arguments with the "config" dictionary
def parse_verify_output(type, dump, **kwargs):
    try:
        output = [a.split('=') for a in dump.strip().replace(",", " ").split()]
        if len(output) > 0:
            for sub_list in output:
                    if type == "flow":
                        if sub_list[0] in flow_attrs or re.search("goto_table:\d", sub_list[0]):
                            flow_config = config.get(type)
                            if not isinstance(flow_config, string_types):
                                for flow_data in flow_config:
                                    if str(flow_data.get("table")) == str(kwargs.get("table")):
                                        if isinstance(flow_data, dict):
                                            if re.search("goto_table:\d", sub_list[0]):
                                                sub_list = ["actions", sub_list[0]]
                                            if sub_list[0] != "actions":
                                                if str(sub_list[0]) == "tun_id":
                                                    if str(flow_data.get("tunnel_id")) != str(int(str(sub_list[1]), 16)):
                                                        print("Mismatch in {} flow field {} with configured value {}".format(str(sub_list[0]),
                                                              str(flow_data.get(str(sub_list[0]))), int(str(sub_list[1]), 16)))
                                                        assert False
                                                else:
                                                    if str(flow_data.get(str(sub_list[0]))) != str(sub_list[1]):
                                                        print("Mismatch in {} flow field {} with configured value {} ".format(str(sub_list[0]),
                                                              str(flow_data.get(str(sub_list[0]))), str(sub_list[1])))
                                                        assert False
                                            else:
                                                action_list = flow_data.get(str(sub_list[0]))
                                                if not isinstance(action_list, string_types):
                                                    for action in action_list:
                                                        if isinstance(action, dict):
                                                            for field, value in action.items():
                                                                if not re.search("set_field:(.*)", sub_list[1]) and field != "set_field":
                                                                    field_values = sub_list[1].split(":")
                                                                    value = value if field != "group" else int(str(value), 16)
                                                                    if (field_values[0] == field) and (str(field_values[1]) != str(value)):
                                                                        print("Mismatch in {} flow field {} with configured value {}".format(str(sub_list[0]),
                                                                              "{}:{}".format(field, int(value)), sub_list[1]))
                                                                        assert False
                                                                else:
                                                                    if isinstance(value, dict) and re.search("set_field:(.*)", sub_list[1]):
                                                                        for set_fields, field_values in value.items():
                                                                            # set_field:0x157->tun_id
                                                                            if set_fields == "tunnel_id":
                                                                                if sub_list[1] != "{}:{}->{}".format(field, hex(field_values), "tun_id"):
                                                                                    print("Mismatch in {} flow field {} with configured value {}".format(str(sub_list[0]),
                                                                                          "{}:{}->{}".format(field, hex(field_values), "tun_id"),
                                                                                          sub_list[1]))
                                                                                    assert False
                                                                            else:
                                                                                pass
                    elif type == "group":
                        if sub_list[0] in group_attrs:
                            group_config = config.get(type)
                            if not isinstance(group_config, string_types):
                                for group_data in group_config:
                                    if str(group_data.get("group_id")) == str(kwargs.get("group_id")):
                                        if isinstance(group_data, dict):
                                            if sub_list[0] != "bucket":
                                                if sub_list[0] == 'group_id':
                                                    if int(str(group_data.get(str(sub_list[0]))), 16) != int(sub_list[1]):
                                                        print("Mismatch in {} group field with configured value".format(str(sub_list[0])))
                                                        assert False
                                                else:
                                                    if str(group_data.get(str(sub_list[0]))) != str(sub_list[1]):
                                                        print("Mismatch in {} group field with configured value".format(str(sub_list[0])))
                                                        assert False
                                            else:
                                                bucket_data = group_data.get(str(sub_list[0]))
                                                if not isinstance(group_config, string_types):
                                                    for bucket in bucket_data:
                                                        if isinstance(bucket, dict):
                                                            for field, value in bucket.items():
                                                                if sub_list[2] != "{}:{}".format(field, value):
                                                                    print("Mismatch in {} group field with configured value".format(str(field)))
                                                                    assert False
                                                        else:
                                                            raise ValueError("Input is not in a valid format.")
                                                            assert False
                                                else:
                                                    raise ValueError("Input is not in a valid format.")
                                                    assert False
                                        else:
                                            raise ValueError("Input is not in a valid format.")
                                            assert False
                            else:
                                raise ValueError("Input is not in a valid format.")
                                assert False
                    else:
                        raise AttributeError("Invalid type while parsing the output.")
                        assert False
        else:
            raise AttributeError("Found empty output.")
            assert False
    except Exception as e:
        print(print_exception(e))
        assert False


# Method to delete and verify the configuration
def delete_verify_config(type, switch, command):
    switch(command, shell="bash")
    if type == "flow":
        data = switch(command.replace("del-flows", "dump-flows"), shell="bash")
    elif type == "group":
        data = switch(command.replace("del-groups", "dump-groups"), shell="bash")
    else:
        raise AttributeError("Invalid type")
        assert False
    output = [a.split('=') for a in data.strip().replace(",", " ").split()]
    for sub_list in output:
        if sub_list[0] in flow_attrs or sub_list[0] in group_attrs:
            print("ERROR: {} not deleted successfully".format(type.upper()+command.replace("dump-groups", "del-groups")))
            assert False


# Common method to print the exception message
# along with the line number for easy understanding
def print_exception(message):
    import linecache
    exc_type, exc_obj, tb = sys.exc_info()
    f = tb.tb_frame
    lineno = tb.tb_lineno
    filename = f.f_code.co_filename
    linecache.checkcache(filename)
    line = linecache.getline(filename, lineno, f.f_globals)
    return 'ERROR : {}, LINE {} "{}" {}'.format(message, lineno, line.strip(), exc_obj)


# Conversion method used to convert
# the unicode string to a ascii string
def unicode_to_string(input_data):
    import unicodedata
    try:
        return unicodedata.normalize('NFKD', input_data).encode('ascii', 'ignore')
    except Exception as e:
        print(str(e))
        sys.exit(2)

