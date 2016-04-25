from opsvalidator.base import *
from opsvalidator import error
from opsvalidator.error import ValidationError
from opsrest.utils import *


class BridgeValidator(BaseValidator):
    resource = "bridge"

    def validate_deletion(self, validation_args):
        bridge_row = validation_args.resource_row
        bridge_name = utils.get_column_data_from_row(bridge_row, "name")
        if bridge_name == 'bridge_normal':
            details = "Default bridge should not be deleted"
            raise ValidationError(error.VERIFICATION_FAILED, details)
