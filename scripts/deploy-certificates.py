#!/usr/bin/env python3
"""Prepare a scoped Azure what-if; deployment requires explicit --apply.

No implicit active subscription, plaintext credential parameters, image tags, or
local service mode. Build/push the image in Azure ACR after foundation deployment.
"""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--subscription", required=True)
    parser.add_argument("--environment", choices=("staging", "production"), required=True)
    parser.add_argument("--location", default="westeurope")
    parser.add_argument("--owner", required=True)
    parser.add_argument("--cost-center", default="tempo")
    parser.add_argument("--image", help="Private ACR image with immutable sha256 digest; omit for foundation only")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-fA-F-]{36}", args.subscription):
        parser.error("Specify an explicit subscription UUID")
    if args.image and not re.fullmatch(r"[a-z0-9]+\.azurecr\.io/[a-z0-9/_-]+@sha256:[0-9a-f]{64}", args.image):
        parser.error("Use an immutable image in the private Azure registry")
    template = Path(__file__).resolve().parents[1] / "infra/certificates/main.bicep"
    command = ["az", "deployment", "sub", "create" if args.apply else "what-if", "--subscription", args.subscription,
               "--location", args.location, "--name", "tempo-certificates-" + args.environment,
               "--template-file", str(template), "--parameters", "environment=" + args.environment,
               "location=" + args.location, "owner=" + args.owner, "costCenter=" + args.cost_center,
               "deployCompute=" + ("true" if args.image else "false"), "image=" + (args.image or "")]
    subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
