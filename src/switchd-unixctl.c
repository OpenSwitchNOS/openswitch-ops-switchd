/* Copyright (C) 2015, 2016 Hewlett-Packard Development Company, L.P.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include "openvswitch/vlog.h"
#include "unixctl.h"
#include "switchd.h"

VLOG_DEFINE_THIS_MODULE(unixctl);

static unixctl_cb_func ops_switchd_exit;
static struct unixctl_server *unixctl;

void
switchd_unixctl_init(char *unixctl_path)
{
    int retval;

    retval = unixctl_server_create(unixctl_path, &unixctl);
    if (retval) {
        exit(EXIT_FAILURE);
    }
    unixctl_command_register("exit", "", 0, 0, ops_switchd_exit, &switchd_exiting);
}

void
switchd_unixctl_run()
{
    unixctl_server_run(unixctl);
}

void
switchd_unixctl_wait()
{
    unixctl_server_wait(unixctl);
}

void
switchd_unixctl_exit()
{
    unixctl_server_destroy(unixctl);
}

static void
ops_switchd_exit(struct unixctl_conn *conn, int argc OVS_UNUSED,
                  const char *argv[] OVS_UNUSED, void *exiting_)
{
    bool *exiting = exiting_;
    *exiting = true;
    unixctl_command_reply(conn, NULL);
}
