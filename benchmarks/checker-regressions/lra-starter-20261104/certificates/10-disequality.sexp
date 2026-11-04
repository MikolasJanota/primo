(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun z () Real)
    (declare-fun primo_cnf_0 () Bool))
  (cnf
    (clause (not primo_cnf_0) (<= x y))
    (clause (not primo_cnf_0) (<= y x))
    (clause primo_cnf_0 (not (<= x y)) (not (<= y x)))
    (clause (not primo_cnf_0))
    (clause (<= x z))
    (clause (<= z y))
    (clause (<= y x)))
  (conflicts
    (conflict-lra ((<= z y) 1) ((not (<= x y)) 1) ((<= x z) 1))))
