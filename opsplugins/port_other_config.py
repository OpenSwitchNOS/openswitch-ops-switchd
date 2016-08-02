#!/usr/bin/env python
# Copyright (C) 2016 Hewlett Packard Enterprise Development LP
# All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may
# not use this file except in compliance with the License. You may obtain
# a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations
# under the License.

from opsrest.utils import utils
from opsvalidator.base import BaseValidator
from opsvalidator import error
from opsvalidator.error import ValidationError


class PortOtherConfigValidator(BaseValidator):
    resource = "port"

    def validate_modification(self, validation_args):
        port_row = validation_args.resource_row
        if hasattr(port_row, "other_config"):
            port_other_config = port_row.__getattr__("other_config")
            # Check if the other_config column has sFlow configuration
            sflow_enabled = port_other_config.get("sflow-enabled", None)
            if sflow_enabled is None:
                # No sFlow configuration changes
                return
            else:
                # Calling sFlow validator
                self.validate_sflow_config_supported(port_row)
                return
        else:
            # No other_config column changes
            return

    # Validates that sFlow configuration is only allowed on physical interfaces
    def validate_sflow_config_supported(self, port_row):
        interface_rows = utils.get_column_data_from_row(port_row, "interfaces")
        if len(interface_rows) == 0:
            # No interfaces associated with port entry
            return
        for interface_row in interface_rows:
            interface_type = utils.get_column_data_from_row(interface_row,
                                                            "type")
            if interface_type != "system":
                details = "sFlow configuration is only supported on "\
                          "physical interfaces"
                raise ValidationError(error.VERIFICATION_FAILED, details)
