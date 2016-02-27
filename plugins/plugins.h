/*
 * Copyright (c) 2008, 2009, 2011 Nicira, Inc.
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
 *
 * Hewlett-Packard Company Confidential (C) Copyright 2015 Hewlett-Packard Development Company, L.P.
 */

#ifndef PLUGINS_H
#define PLUGINS_H 1

#ifdef  __cplusplus
extern "C" {
#endif

#ifdef __linux__
void plugins_init(const char *path);
void plugins_run(void);
void plugins_wait(void);
void plugins_destroy(void);
void plugins_netdev_register(void);
void plugins_ofproto_register(void);
void plugins_bufmon_register(void);
#else
#define plugins_init(path)
#define plugins_run()
#define plugins_wait()
#define plugins_destroy()
#define plugins_netdev_register()
#define plugins_ofproto_register()
#define plugins_bufmon_register()
#endif

#ifdef  __cplusplus
}
#endif

#endif /* plugins.h */
