from opsvalidator.base import *
from opsvalidator import error
from opsvalidator.error import ValidationError
from opsrest.utils import *


class VrfValidator(BaseValidator):
    resource = "vrf"

    def validate_deletion(self, validation_args):
        vrf_row = validation_args.resource_row
        vrf_name = utils.get_column_data_from_row(vrf_row, "name")
        if vrf_name == 'vrf_default':
            details = "Default vrf cannot be deleted"
            raise ValidationError(error.VERIFICATION_FAILED, details)
