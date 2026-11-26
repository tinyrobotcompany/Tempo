"""Manufacturing admission using operator Entra identity; accepts PUBLIC keys only."""
import argparse
import base64
import os
import time
from pathlib import Path

from .app import azure_store
from .protocol import device_id, hostname


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--public-key", type=Path)
    parser.add_argument("--suffix", help="Must match the configured Tempo namespace")
    parser.add_argument("--serial", default=os.environ.get("MANUFACTURING_SERIAL"))
    args = parser.parse_args()
    suffix = os.environ.get("DEVICE_SUFFIX", "")
    if args.suffix and args.suffix != suffix:
        parser.error("Suffix must match the configured Tempo namespace")
    if not suffix:
        parser.error("Tempo namespace is not configured")
    pem = (args.public_key.read_text() if args.public_key else
           base64.b64decode(os.environ["MANUFACTURING_PUBLIC_KEY"], validate=True).decode())
    identifier = device_id(pem)
    if not args.serial or len(args.serial) > 64:
        parser.error("A manufacturing serial of at most 64 characters is required")
    azure_store().register({"PartitionKey": identifier, "RowKey": "identity", "public_key": pem,
                            "hostname": hostname(identifier, suffix), "enabled": True,
                            "serial": args.serial, "registered_at": int(time.time())})
    print(hostname(identifier, suffix))


if __name__ == "__main__":
    main()
