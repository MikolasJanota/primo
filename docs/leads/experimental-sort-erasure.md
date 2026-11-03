# Experimental erasure of exported object sorts

**Status: temporary export-only experiment for external-checker testing.**
`--experimental-export-erase-sorts` requires `--export-cnf`. It is disabled by
default and is intended to disappear when the checking framework handles the
original many-sorted signature. It is not a preprocessing or solving option.

## Original design basis and proof obligation

This is an original design note, rather than an implementation of a published
sort-erasure algorithm. The preconditions are the exporter's existing ground
QF_UF fragment, nonempty uninterpreted object sorts, distinct function-symbol
identities, and unchanged built-in Boolean semantics. Quantifiers and interpreted
object sorts are excluded. [SMT-LIB Core](https://smt-lib.org/theories-Core.shtml)
fixes `Bool` to two values; it is never erased.

For a well-sorted ground formula F, replace each uninterpreted object sort by U
in symbol declarations, preserving symbol names and every term/literal. Call
this E(F). The proof obligation is equisatisfiability of F and E(F).

Given a model of F, consider only the finitely many ground terms in F and their
values. Embed those values into one infinite object domain, using disjoint tagged
representatives for different original sorts. Preserve the two Boolean values.
For each uninterpreted function, define its value on every observed argument
tuple to be the representative of its original result. This is well-defined:
equal encoded argument tuples came from equal original argument tuples for that
same symbol, whose original result was unique. Extend the function arbitrarily
on all unobserved tuples, respecting its Boolean or object result sort. The
result satisfies E(F). Conversely, interpret every original object sort using
the common domain of any model of E(F), and keep the functions at their original
typed signatures. Ground evaluations and Boolean truth values are preserved.

The same argument applies independently to each conflict conjunction, so a
valid original theory conflict remains inconsistent after erasure. SAT literals,
propositional clauses and lemma selection are unchanged. Distinct original
function symbols must not be merged even when their erased signatures match.
Infinitude is a model-construction device, not an axiom emitted in the artifact.

This argument does not justify erasing quantified sorts. For example,
`forall x:A. x=a` together with `b1 != b2`, with `b1,b2:B`, has a model with
one A-object and two B-objects. Erasure makes it inconsistent. Interpreted finite
sorts also require their own treatment.

## Implementation checklist and representation

The owner is {exportexperimentsource}`append_signature <src/smt/qf_uf_export.cpp>`. It borrows the
frontend's immutable `core::SymbolTable` and appends owned declaration strings
to `smtlib::ConflictDocument`. The option is an independent Boolean in
{exportexperimentsource}`CheckSatOptions::experimental_export_erase_sorts <src/smt/check_sat.hpp>`; neither the term manager nor solver state is changed.

1. Reserve all original symbol and sort names in a local owned set.
2. Choose a fresh `primo_uf_sort` name, adding a numeric suffix on collision.
   This prefix differs from the clausifier auxiliary prefix `primo_cnf_`.
3. Emit exactly one uninterpreted-sort declaration, including when the original
   signature has zero or one object sorts. An unused nonempty sort is harmless.
4. Validate every function's original argument and result sorts. Print `Bool`
   unchanged and every uninterpreted object sort as the fresh common name.
   Continue rejecting interpreted object sorts.
5. Preserve function identifiers, term printing, literal polarity and order,
   CNF contents, conflict contents and reduction mode. The original solver still
   uses the original many-sorted terms.
6. Reject use without an export filename in
   {exportexperimentsource}`check_sat_smtlib <src/smt/check_sat.cpp>` and reject
   {exportexperimentsource}`SmtSession <src/smt/smt_session.cpp>` streaming use.
   Existing quantifier/extended-term restrictions remain in force.

The local name set and common-sort string live only while the signature is
constructed. No borrowed symbol or term reference crosses mutation of its owner;
only the separate output document grows. Expected extra work is linear in names
and declarations, plus string processing. No performance claim is made.
`:export.sort-erasure.enabled` and `:export.sort-erasure.input-sorts` expose
whether the signature rewrite ran and how many original object sorts it merged.

The {exportexperimentsource}`CLI/reference tests <tests/cnf_export_cli.py>`
cover cross-sort function chains, original name collisions, `Bool`
arguments/results, zero object sorts, unchanged default output, modifier
validation, and rejection of quantified/interpreted input. The {exportexperimentsource}`reference gate <scripts/slurm_cnf_export_gate.sbatch>`
includes multi-sort inputs with erasure both enabled and disabled. The {micromathworkflowsource}`Micromath workflow <scripts/slurm_micromath_check.sbatch>`
requires replay of erased multi-sort and ordinary single-sort artifacts. Erasure addresses its object-sort restriction; it does not add
support for positive-arity Boolean-valued functions, Boolean arguments or Boolean conflict facts.

For example, `embed : A -> B`, `a,b : A`, `a=b`, and `embed(a)!=embed(b)` become
`embed : U -> U` and the same two literals. Congruence still refutes the conflict.
The emitted certificate establishes the erased exported CNF; no source-to-CNF
conversion certificate or separate erasure-proof object is currently emitted.
