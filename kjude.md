# Kjude ideas

- reference counting
- autodereference
- NO null pointers (Option)
- NO pointer arithmetics
- pointers just tell to pass by reference and not by value
- this over self
- elif over else if

- varname : type = value ;
- funcname : (args...) -> ret = { body }
- main : (args: [string]) -> int = { body }

- passing default parameters matches names
  foo : (a: int, b: int = 5, c: bool = false) = {}
  c := true;
  foo(69, c, b=3);
  // positional match for required params, then name match (order is not relevant)
