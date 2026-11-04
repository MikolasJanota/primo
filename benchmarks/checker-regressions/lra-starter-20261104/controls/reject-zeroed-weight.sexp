(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature (declare-fun x () Real))
  (cnf (clause (<= x 0)) (clause (<= 1 x)))
  (conflicts (conflict-lra ((<= x 0) 0) ((<= 1 x) 1)))
)
