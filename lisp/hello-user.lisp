(print-str "What is your name? ")

(set str (alloc-static 10))
(set ptr str)
(while (set in (read-mem 3))
       (progn
         (set ptr (+ ptr 1))
         (write-mem ptr in)))
(write-mem str (- ptr str))

(print-str "Hello, ")
(print-str str)
(print-str "! Glad to see you!")