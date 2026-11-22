(defcode add1 (x) #x3001 #x4770)
(add1 41)
(defcode mul (x y) #x4348 #x4770)
(mul 6 7)
(add1 (mul 10 10))
(pprintall)
"end"
