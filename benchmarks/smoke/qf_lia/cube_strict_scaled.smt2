; CAV 2024 Section 4.2: exercise a strict bound over a scaled auxiliary.
; Cube completion is tested with patching/cuts and integer preparation disabled.
(set-option :produce-models true)
(set-logic QF_LIA)
(set-info :status sat)
(declare-const x Int)
(declare-const y Int)
(assert (<= (+ (* 3 x) y) 9))
(assert (not (<= (+ (* 3 x) y) 2)))
(check-sat)
(get-model)
