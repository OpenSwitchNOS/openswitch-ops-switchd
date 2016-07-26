# -*- coding: utf-8 -*-
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

"""
Test suite entry point module.
"""

# Add here all your test suites and import from them every test case.
# These test cases will be documented by autoapi:
# http://autoapi.readthedocs.org/

from .test_ovs_openflow import test_ovs_openflow

# Don't forget to list all your test cases here:
__all__ = [
    'test_ovs_openflow'
]

