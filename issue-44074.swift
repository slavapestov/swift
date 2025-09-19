let a = [1]
let x = [a.lazy.map({$0 + 1}), a.lazy.map({$0 + 2}), a.lazy.map({$0 + 3})]
