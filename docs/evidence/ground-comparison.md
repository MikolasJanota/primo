(ground-comparison)=
# Ground-theory comparison gallery

[Open the comparison page](../_static/ground-comparison/index.html).
It brings together Primo, Z3, cvc5, Yices and OpenSMT for QF_UF, QF_LRA,
QF_LIA, QF_UFLRA and QF_UFLIA. These are the five quantifier-free ground
divisions in this evaluation. The page redraws existing qualified measurements;
it does not represent a new run of current HEAD.

## Two measurement cohorts

The **full-corpus archive** uses the qualified 9 November 2026 native runs.
Every division uses one fixed measured Primo configuration, selected by the
most full-corpus solves, then lowest PAR2. QF_LIA selects the optional integer
profile; the other divisions select defaults. The choice is fixed before
filtering and is never made per input. The source is `7dd8874c`, with
implementation `cab1ebc4`. All 20,351 successful native SAT timing rows across
the original measured profiles passed separate direct Dolmen validation.

| Division | Full-corpus inputs | Selected Primo configuration |
| --- | ---: | --- |
| QF_UF | 7,503 | Default |
| QF_LRA | 1,753 | Default |
| QF_LIA | 13,306 | Integer profile, including uniform cubes |
| QF_UFLRA | 1,284 | Default |
| QF_UFLIA | 659 | Default |

The **hybrid integer selections** use the 12 November comparison: 200 selected
QF_LIA inputs and the 300-input SMT-COMP 2026 QF_UFLIA selection. They show
both the opt-in hybrid policy (`9e01197e`, production-equivalent to `cd258f6a`)
and the frozen private baseline (`c49585a0`) beside the same four references.
Private-branch factor one, an unlimited generated-atom cap and the existing
arithmetic defaults are used.
Every successful native SAT row was validated separately. Certora diagnostic
repetitions and regression followups are excluded from these plots.

The hybrid selection is newer but smaller; it does not replace the full-corpus
measurements or establish their ranking. In particular, the hybrid LIA selection
solves 134 inputs versus 104 for private, while Z3 solves 199, cvc5 164,
Yices 170 and OpenSMT 193. The
[hybrid report](lia-hybrid-20261111.md) retains the gains, losses and repeated
regressions.

## Shared limits and display rules

All panels use a 120-second cutoff, one physical CPU and an enforced 8 GiB
address-space cap. Reference runs are from 24 October 2026: Z3 5.1.0,
cvc5 1.3.4, Yices 2.7.0 and OpenSMT 2.9.2. Their original 1,200-second
measurements are censored at 120 seconds. The archived reference rows match
each native cohort by filename and input content hash. Different measurement
dates and builds remain a limitation of this comparison.

The x-axis is the per-instance time budget on a log scale; the y-axis is the
percentage of the displayed cohort solved. Higher is better. A solve requires
`sat` or `unsat`, exit status zero and successful completion within the cutoff.
Unknown, timeout, memory-out, unsupported and error rows remain unsolved.
Reference errors stay in the counts. Times below 10 ms share the left edge.

The optional filtered view removes only inputs every displayed solver solves
in under one second. The chart, solved counts and resource/PAR2 table all use
that same retained cohort. PAR2 assigns 240 seconds to every unsolved input;
the page displays its mean per input. There is no combined ranking across
theories or cohorts.

## Reproduce the page

```bash
python3 scripts/qualification/ground_comparison/report.py
```

The generator reads the content-pinned uniform timing export and hybrid paired
rows. It checks qualification hashes, identical input cohorts, decisive-answer
agreement, model coverage, original solved/resource/PAR2 totals and the fixed
profile selection before producing 14 SVGs, the interactive HTML page and JSON
results. This is local metadata analysis, with no solver execution.

[Source and output hashes](../../benchmarks/ground-comparison-20261112/provenance.json)
pin the generator, template, timing files, qualification records and artifacts.
The [uniform qualification](uniform120.md) and
[hybrid qualification](lia-hybrid-20261111.md) provide the original SLURM jobs,
binary identities, configurations and model-validation evidence.
