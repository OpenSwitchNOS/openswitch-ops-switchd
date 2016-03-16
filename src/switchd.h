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

#ifndef SWITCHD_H
#define SWITCHD_H 1

extern struct ovsdb_idl *idl;
extern unsigned int idl_seqno;
extern struct ovsdb_idl_txn *status_txn;
extern bool status_txn_try_again;
extern struct ovsrec_system switchd_null_cfg;
extern bool initial_config_done;
extern struct ovsdb_idl_txn *daemonize_txn;
extern bool switchd_exiting;
extern char *remote;

void switchd_init(char *unixctl_path, char *plugins_path);
void switchd_run();
void switchd_wait();
void switchd_exit();

#endif
