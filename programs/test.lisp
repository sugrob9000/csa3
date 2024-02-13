(set lives-very-long 24342)
(progn
  (set x 1)
  (set z 3)
  (set w (+ 1 2 3)))
(while x
  (progn
    (set y x)
    (set x x)
    (set v (* z z ))))
(set x 3)
(if x
  (set x 2)
  (set y 3))
(+ "hello!" lives-very-long)