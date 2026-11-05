; HAND-WRITTEN version-3 example; native output may choose different labels/steps.
(primo-conflicts
  (version 3)
  (logic QF_LIA)
  (signature
    (declare-fun x () Int)
    (declare-fun y () Int))
  (cnf
    (clause (<= (+ (* 3 x) (* 2 y)) 4))
    (clause (<= 1 y))
    (clause (<= 1 (* 2 x))))
  (conflicts
    (conflict-lia
      (premises
        (a (<= (+ (* 3 x) (* 2 y)) 4))
        (b (<= 1 y))
        (c (<= 1 (* 2 x))))
      (proof
        (cut upper 3 (a 1) (b 2))          ; x <= 0
        (cut lower 2 (c 1))                ; -x <= -1
        (cut impossible 1 (upper 1) (lower 1)) ; 0 <= -1
        (contradiction impossible)))))
