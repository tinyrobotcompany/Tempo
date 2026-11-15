"""PRIM's verified TLS requirements must survive clean and incremental builds."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TransportTlsConfiguration(unittest.TestCase):
    def test_existing_and_clean_builds_support_prim(self):
        required = (
            'MBEDTLS_SSL_PROTO_TLS1_3',
            'MBEDTLS_SSL_TLS1_3_KEXM_EPHEMERAL',
            'MBEDTLS_CERTIFICATE_BUNDLE',
            'MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL',
            'MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY',
        )
        for name in ('sdkconfig', 'sdkconfig.defaults'):
            lines = set((ROOT / 'firmware' / name).read_text().splitlines())
            for flag in required:
                with self.subTest(configuration=name, flag=flag):
                    self.assertIn(f'CONFIG_{flag}=y', lines)
            for flag in ('MBEDTLS_ALLOW_WEAK_CERTIFICATE_VERIFICATION',
                         'ESP_TLS_INSECURE', 'ESP_TLS_SKIP_SERVER_CERT_VERIFY',
                         'MBEDTLS_CERTIFICATE_BUNDLE_DEPRECATED_LIST'):
                self.assertNotIn(f'CONFIG_{flag}=y', lines)


if __name__ == '__main__':
    unittest.main()
