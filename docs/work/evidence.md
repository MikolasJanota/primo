# Evidence work still open

These are unresolved provenance or checking tasks, not undocumented solver
algorithms. The original records remain available and retain their limitations.

| Task | What is needed to close it |
| --- | --- |
| Published QF_LRA tables | Identify exact Primo commits, manifests, commands, and raw artifacts for Tables 1–2 in the September 2026 paper; verify every aggregate against the ledger. The [report](../papers/primo-qflra-results.md) currently supplies publication-level results only. |
| Quantifier reference-less UNSAT answers | Resolve the nine unconfirmed answers in the historical pool-check record, or retain them as explicitly unconfirmed. Do not infer confirmation from a later unrelated gate. |
| Quantifier export trust | Independently check the conservative-extension conditions of witness exports if proof-like validation is required. The current checker trusts that export contract; see [the checking argument](../evidence/quantifiers.md). |
| Initial preprocessing-bound qualification | Retain the [blocked qualification](../leads/preprocessing-bound-tightening.md#qualification-is-blocked) and its three quarantined QF_UFLRA resource-abort cases. Later studies that exclude those inputs do not close them. |
| External documentation links | The {download}`20 November check <../coverage/external-links-20261120.json>` records a failing `make docs-check-external` run: request/access errors, two reported missing anchors and an unresolved DOI. Recheck destinations and access without treating a timeout or HTTP 403 as proof that a paper moved. |
| Historical records with missing metadata | Preserve the original evidence status and name the missing command, revision or artifact. Do not backfill guesses or count them as qualification. |

Results-storage migration is [separate work](results-storage.md). Changing the
storage format would not supply missing experiment identities or strengthen an
incomplete correctness check.

## Publication preparation inspection, 2026-10-02

The ledger at `f95ba8a` contained 709 historical/qualification entries. Searches
for the published QF_LRA aggregate values and likely local paper/tuning artifact
directories did not establish a matching experiment identity. A numerical
substring hit in an unrelated incremental-UF record was rejected as evidence.
The paper report therefore retains its publication-level attribution and makes
no raw-artifact reproducibility claim. The owner may have the tuning manifests
and measurements elsewhere; no values or commit identities have been guessed.
