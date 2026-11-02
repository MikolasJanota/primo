; Illustrative version-1 artifact; not output from an implemented exporter.
(primo-conflicts
  (version 1)
  (logic QF_UF)
  (signature
    (declare-sort U 0)
    (declare-sort V 0)
    (declare-fun a () U)
    (declare-fun b () U)
    (declare-fun c () U)
    (declare-fun |left endpoint| () U)
    (declare-fun f (U) U)
    (declare-fun g (U) V)
    (declare-fun p (U) Bool)
    (declare-fun h (Bool) V))
  (cnf
    (clause (= a b))
    (clause (not (= (f a) (f b)))))
  (conflicts
    ; Congruence.
    (conflict (= a b) (not (= (f a) (f b))))
    ; Transitivity.
    (conflict (= a b) (= b c) (not (= a c)))
    ; Direct contradiction, with a duplicate literal.
    (conflict (= a b) (= a b) (not (= a b)))
    ; Congruence across two sorts, with a quoted symbol.
    (conflict (= |left endpoint| b)
              (not (= (g |left endpoint|) (g b))))
    ; Predicate congruence.
    (conflict (= a b) (p a) (not (p b)))
    ; Boolean arguments retain their ordinary two-valued meaning.
    (conflict (p a) (not (= (h (p a)) (h true))))
    ; Repeated records are permitted.
    (conflict (= a b) (not (= (f a) (f b))))))
