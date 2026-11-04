(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature (declare-fun x () Real))
  (cnf (clause (<= x 0)) (clause (< 0 x)))
  (conflicts (conflict-lra ((<= x 0) 1) ((<= 0 x) 1)))
)
