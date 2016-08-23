What's interesting here is not the actual autocompletion (I just implemented a
couple of simple methods of autocompletion, one using vectors and one using
ternary trees).

The more interesting thing is that the Completer provides an interface where one
of the methods can either be implemented (presumably for efficiency's sake) by
the underlying engine, or (if not provided) can fall back to being satisfied in
terms of other methods. This is achieved by the usual methods of SFINAE (which
may improve in the future).

Good interfaces should be complete, expressive and efficient: this approach
allows completeness and expressiveness to vary independently of efficiency
(which can be taken arbitrarily far by some implementation). The approach is
similar to that taken by many Haskell typeclasses.
