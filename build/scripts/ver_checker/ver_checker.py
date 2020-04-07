"""
"""

import argparse
import sys
import re
import math


class VersionChecker():
    """ Class that check version pattern and parse it basing on mask"""

    def __init__(self, version, mask, shift):
        self._version = version
        self._mask = mask
        self._shift = shift
        self._version_as_list = None
        self._mask_as_list = None

    def _check_version_fields(self):

        def check_field(version, mask):
            return 0 <= version <= mask

        vmajor, vminor, vupdate = self._version_as_list
        mmajor, mminor, mupdate = self._mask_as_list

        return check_field(vmajor, mmajor) and \
               check_field(vminor, mminor) and \
               check_field(vupdate, mupdate)

    def is_correct_version(self):
        """
        Checks if version and mask parameters are valid
        """

        def check_pattern(version):
            return re.fullmatch(r'\d+\.\d+\.\d+', version) is not None

        def version_to_list(version):
            return list(map(int, version.split(sep='.')))

        def validate_and_parse_version(version):
            """
            Checks if input (version or mask) are compatible with pattern
            and parse it into list if is
            """
            if_version_correct = check_pattern(self._version)
            version_as_list = None

            if if_version_correct:
                version_as_list = version_to_list(version)

            return if_version_correct, version_as_list

        def check_mask_base():
            if self._shift:
                # Mask values should be power of 2
                if_mask_base_correct = all([x > 0 and math.log2(x+1) % 1 == 0 \
                                            for x in self._mask_as_list])
            else:
                # Mask values should be power of 10
                if_mask_base_correct = all([x > 1 and math.log10(x) % 1 == 0 \
                                            for x in self._mask_as_list])
            return if_mask_base_correct

        if_version_correct, self._version_as_list = validate_and_parse_version(self._version)
        if_mask_correct, self._mask_as_list = validate_and_parse_version(self._mask)

        if if_version_correct and if_mask_correct:
            if_mask_correct = check_mask_base()

        return if_version_correct and if_mask_correct and self._check_version_fields()

    def parse_version(self):
        """
        Parse string value stored in list to numeric
        """

        if self._version_as_list is None or self._mask_as_list is None:
            raise TypeError

        vmajor, vminor, vupdate = self._version_as_list
        mminor, mupdate = self._mask_as_list[1:]

        if not self._shift:
            return (vmajor * mminor * mupdate) + \
                   (vminor * mupdate) + \
                    vupdate

        mupdate_log = int(math.log2(mupdate + 1))
        return (vmajor << (int(math.log2(mminor + 1)) + mupdate_log)) + \
               (vminor << mupdate_log) + \
                vupdate


def main():
    """
    Main function for version checker application
    """

    parser = argparse.ArgumentParser(description="Silly script to checking FW version structure")
    parser.add_argument("--version", type=str, required=True,
                        help="Number of version to check and parse")
    parser.add_argument("--mask", type=str, required=True,
                        help="Mask used to calculate numeric value of version."
                             " Treat it as maximum value for each field")
    parser.add_argument("--shift", required=False, action='store_true',
                        help="Set to use binary way (bit shifting). "
                             "If not set, decimal way used")

    args = parser.parse_args()
    version_checker = VersionChecker(**vars(args))

    if not version_checker.is_correct_version():
        sys.stdout.write("Please check your version or mask! Example: 2.2.2 "
                         "Your version: {version} Your mask: {mask} "
                         "Is shifting used: {shift}\n".format(**vars(args)))
        sys.exit(1)

    parsed_version = version_checker.parse_version()

    sys.stdout.write(str(parsed_version))
    sys.exit(0)

if __name__ == "__main__":
    main()
