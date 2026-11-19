#!/usr/bin/env python3
"""Record CPU sibling identities on allocated benchmark nodes."""

import json
import os
import socket
from pathlib import Path

assert os.environ.get("SLURM_JOB_ID")
cpus = {}
for cpu in Path("/sys/devices/system/cpu").glob("cpu[0-9]*"):
    if not (cpu / "topology/core_id").exists():
        continue
    cpus[cpu.name[3:]] = {
        field: (cpu / "topology" / field).read_text().strip()
        for field in ("physical_package_id", "core_id", "thread_siblings_list")
    }
value = {"host": socket.gethostname(), "job": os.environ["SLURM_JOB_ID"], "cpus": cpus}
out = Path("results/topology")
(out / (value["host"] + ".json")).write_text(json.dumps(value, indent=2) + "\n")
