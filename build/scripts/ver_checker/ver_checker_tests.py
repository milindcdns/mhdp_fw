"""
"""

from ver_checker import VersionChecker
import unittest
import io

class TestClass(unittest.TestCase):

    def test_correct(self):
        version_checker = VersionChecker('2.13.1', '15.15.255', True)
        self.assertTrue(version_checker.is_correct_version())
        self.assertEqual(0x2D01, version_checker.parse_version())

        version_checker = VersionChecker('21.32.12', '100.100.100', False)
        self.assertTrue(version_checker.is_correct_version())
        self.assertEqual(213212, version_checker.parse_version())

    def test_invalid(self):
        version_checker = VersionChecker('2.23x.1', '15.15.255', True)
        self.assertFalse(version_checker.is_correct_version())

    def test_no_version_list(self):
        version_checker = VersionChecker('2.23.1', '15.15.255', True)
        with self.assertRaises(TypeError):
            version_checker.parse_version()

    def test_of_mask(self):
        version_checker = VersionChecker('100.23.1', '15.15.15', True)
        self.assertFalse(version_checker.is_correct_version())

        version_checker = VersionChecker('2.23.1', '10.10.10', False)
        self.assertFalse(version_checker.is_correct_version())

    def test_only_dots(self):
        version_checker = VersionChecker('..', '100.100.100', False)
        self.assertFalse(version_checker.is_correct_version())

    def test_invalid_mask(self):
        version_checker = VersionChecker('2.23.1', '0.0.0', True)
        self.assertFalse(version_checker.is_correct_version())

        version_checker = VersionChecker('2.23.1', '55.55.55', True)
        self.assertFalse(version_checker.is_correct_version())

        version_checker = VersionChecker('2.23.1', '0.0.0', False)
        self.assertFalse(version_checker.is_correct_version())

        version_checker = VersionChecker('2.23.1', '55.55.55', False)
        self.assertFalse(version_checker.is_correct_version())

        version_checker = VersionChecker('2.23.1', '1.1.1', True)
        self.assertFalse(version_checker.is_correct_version())

        version_checker = VersionChecker('2.23.1', '1.1.1', False)
        self.assertFalse(version_checker.is_correct_version())

if __name__ == '__main__':
    unittest.main()

