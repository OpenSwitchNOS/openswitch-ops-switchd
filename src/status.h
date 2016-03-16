/* Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
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

#ifndef SWITCHD_STATUS_H
#define SWITCHD_STATUS_H 1

/* Status update to database.
 *
 * Some information in the database must be kept as up-to-date as possible to
 * allow controllers to respond rapidly to network outages.  Those status are
 * updated via the 'status_txn'.
 *
 * We use the global connectivity sequence number to detect the status change.
 * Also, to prevent the status update from sending too much to the database,
 * we check the return status of each update transaction and do not start new
 * update if the previous transaction status is 'TXN_INCOMPLETE'.
 *
 * 'statux_txn' is NULL if there is no ongoing status update.
 *
 * If the previous database transaction was failed (is not 'TXN_SUCCESS',
 * 'TXN_UNCHANGED' or 'TXN_INCOMPLETE'), 'status_txn_try_again' is set to true,
 * which will cause the main thread wake up soon and retry the status update.
 */
struct ovsdb_idl_txn *status_txn;
bool status_txn_try_again;

void run_status_update(void);
void status_update_wait(void);

#endif
