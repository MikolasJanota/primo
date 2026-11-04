(primo-conflicts
  (version 2)
  (logic QF_LRA)
  (signature
    (declare-fun x () Real)
    (declare-fun y () Real)
    (declare-fun primo_cnf_0 () Bool)
    (declare-fun primo_cnf_1 () Bool))
  (cnf
    (clause (not primo_cnf_0) (<= 1 x))
    (clause (not primo_cnf_0) (<= x 0))
    (clause primo_cnf_0 (not (<= 1 x)) (not (<= x 0)))
    (clause (not primo_cnf_1) (<= 1 y))
    (clause (not primo_cnf_1) (<= y 0))
    (clause primo_cnf_1 (not (<= 1 y)) (not (<= y 0)))
    (clause primo_cnf_0 primo_cnf_1))
  (conflicts
    (conflict-lra ((<= 1 x) 1) ((<= x 0) 1))
    (conflict-lra ((<= 1 y) 1) ((<= y 0) 1))))
