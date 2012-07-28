kinterval
=========

Generic in-kernel interval tree manipulation.

This kernel module provide a generic infrastructure to efficiently keep track
of intervals.

An interval is represented by a triplet (start, end, type). The values (start,
end) define the bounds of the range. The type is a generic property associated
to the interval. The interpretation of the type is left to the user.

Multiple intervals associated to the same object are stored in an interval tree
(augmented rbtree) [1], with tree ordered on starting address. The tree cannot
contain multiple entries of different interval types which overlap; in case of
overlapping intervals new inserted intervals overwrite the old ones (completely
or in part, in the second case the old interval is shrunk or split
accordingly).

Reference:
  [1] "Introduction to Algorithms" by Cormen, Leiserson, Rivest and Stein
