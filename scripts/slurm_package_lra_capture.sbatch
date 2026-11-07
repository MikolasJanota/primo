#!/usr/bin/env bash
#SBATCH --job-name=primo-lra-capture-package
#SBATCH --cpus-per-task=2
#SBATCH --mem=2G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err

set -euo pipefail
: "${PRIMO_WORKDIR:?qualified workspace}"
: "${CAPTURE_RESULT_ROOTS:?colon-separated completed capture result roots}"
cd "$PRIMO_WORKDIR"
export CAPTURE_PACKAGE_OUT="$PWD/results/lra-capture-feedback-$SLURM_JOB_ID"
python3 - <<'PY'
import gzip
import hashlib
import json
import os
import shutil
import tarfile
from pathlib import Path

out = Path(os.environ['CAPTURE_PACKAGE_OUT'])
out.mkdir(parents=True, exist_ok=False)
package = out / 'feedback'
package.mkdir()
def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(65536), b''):
            h.update(block)
    return h.hexdigest()
def save(path, obj):
    path.write_text(json.dumps(obj, indent=2) + '\n')

rows = []
smallest = None
for root in os.environ['CAPTURE_RESULT_ROOTS'].split(':'):
    paths = sorted(Path(root).glob('case-*/summary.json'))
    if not paths:
        raise ValueError('capture result root has no summaries')
    for path in paths:
        row = json.loads(path.read_text())
        if not row.get('finished'):
            raise ValueError('incomplete capture stage')
        export = row['export']
        proof = path.parent / export['gzip_file']
        if sha(proof) != export['gzip_sha256'] or proof.stat().st_size != export['gzip_bytes']:
            raise ValueError('compressed proof changed')
        h = hashlib.sha256()
        count = 0
        with gzip.open(proof, 'rb') as stream:
            for block in iter(lambda: stream.read(65536), b''):
                count += len(block)
                h.update(block)
        if count != export['raw_bytes'] or h.hexdigest() != export['raw_sha256']:
            raise ValueError('gzip CRC/raw hash/byte verification failed')
        record = {'summary': str(path), 'summary_sha256': sha(path),
                  'proof': str(proof), 'gzip_crc_and_raw_hash_checked': True,
                  'capture': row}
        rows.append(record)
        target = package / f"job-{row['job_id']}"
        target.mkdir()
        for name in ('summary.json', 'export-progress.jsonl', 'producer.err',
                     'kernel.out', 'kernel.err', 'kernel.resources.txt', 'kernel-exits.txt'):
            if (path.parent / name).exists():
                shutil.copyfile(path.parent / name, target / name)
        if row['case_name'] == 'shard-2-case-006' and row['status'] == 'verified':
            smallest = (row, proof)
if not smallest:
    raise ValueError('expected verified five-clock export is missing')
row, proof = smallest
source = Path(row['source'])
case = package / 'five-clock'
case.mkdir()
for name, meta in row['payloads'].items():
    if sha(source / name) != meta['sha256']:
        raise ValueError('saved checker input changed')
    shutil.copyfile(source / name, case / name)
challenge = source / 'challenge.umd'
if sha(challenge) != row['challenge_sha256']:
    raise ValueError('saved challenge changed')
with challenge.open('rb') as incoming, gzip.open(case / 'challenge.umd.gz', 'wb', compresslevel=1) as packed:
    shutil.copyfileobj(incoming, packed, 65536)
with gzip.open(proof, 'rb') as incoming, gzip.open(case / 'proof-prefix.umd.gz', 'wb', compresslevel=1) as packed:
    packed.write(incoming.read(1024**2))
guide = '''# Micromath QF_LRA staged-export feedback

The manifest records saved proof locations, raw/compressed SHA256 and byte
counts, complete/partial status, progress, stage results and pinned binaries.
All listed captured gzip files were fully decompressed in SLURM and their CRC,
raw byte count and raw SHA256 checked. Full proofs remain at the recorded
cluster artifact paths; they are too large for this small feedback bundle.

five-clock contains the independently audited original input, exported-CNF,
native CORE certificate, exact saved challenge compressed with gzip, and ONLY
the first 1 MiB of the exported binary proof (proof-prefix.umd.gz). That prefix
is a sample, NOT a complete proof and must not be counted as verification.
The complete five-clock proof was accepted by the pinned independent kernel;
see the captured stage summaries for the other cases and historical caps.
Source-to-CNF preprocessing/clausification is still trusted.

On an allocated compute node, from the pinned Micromath root, set CASE to the
absolute extracted five-clock directory and CADICAL to the pinned LRAT build:

    set -euo pipefail
    gzip -dc "$CASE/challenge.umd.gz" > challenge.umd
    ./smt/prove-lra challenge.umd "$CASE/native.sexp" - --raw --cadical "$CADICAL" |
      gzip -1 > proof.umd.gz
    gzip -dc proof.umd.gz | ./check-solution challenge.umd - > kernel.out
    grep -Fxq 'challenge proven' kernel.out
    ! grep -Fxq 'Inconsistency proven!' kernel.out

Use external time/memory/disk bounds matching the manifest. Generation and
replay are separate phases and include compression/decompression/I/O overhead.
No checker internals were inspected/modified and no upstream fetch occurred.
No delta minimization was performed.
'''
(package / 'README.md').write_text(guide)
report = {'job_id': os.environ['SLURM_JOB_ID'], 'rows': rows,
          'source_to_cnf_verified': False, 'proof_prefix_raw_bytes': 1024**2,
          'files': {str(p.relative_to(package)): {'sha256': sha(p), 'bytes': p.stat().st_size}
                    for p in sorted(package.rglob('*')) if p.is_file()}}
save(package / 'manifest.json', report)
archive = out / 'lra-capture-feedback.tar.gz'
with tarfile.open(archive, 'w:gz') as packed:
    for path in sorted(package.rglob('*')):
        if path.is_file():
            packed.add(path, arcname=str(path.relative_to(package)))
save(out / 'summary.json', {'job_id': os.environ['SLURM_JOB_ID'], 'captures_checked': len(rows),
                          'archive': {'file': archive.name, 'sha256': sha(archive), 'bytes': archive.stat().st_size},
                          'statuses': [r['capture']['status'] for r in rows], 'finished': True})
PY
