(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature (declare-fun x () Real) (declare-fun y () Real))
  (cnf (clause (<= x 0)) (clause (<= 1 x)) (clause (<= y 10)))
  (conflicts (conflict-lra ((<= x 0) 1) ((<= 1 x) 1) ((<= y 10) 0)))
)
