#!/usr/bin/env python

'''
Let's assume all metadata fields (capital letters) are same-sized: 1 byte.
The graph of modules (lowercase letters) is like this:

a - b       h       l - m
     \     / \     /
      e - f - g - i - j - k
     /
c - d

(directional; arrows are from left to right)

The module-field access table:

        reads       writes
a                   A
b       A           B
c                   B
d                   A
e       A           C
f                   D
g       B, D
h       D           D
i                   E, F
j       
k       E
l       C, F        A
m       A

Then the scope segments will be like this:
A1: a b d e
A2: l m
B:  b c d e f g h
C:  e f g h i l
D:  f g h
E:  i j k
F:  i l

As a result, the scope segment graph (undirectional) is:

     A2
     |
     |
A1 - C -- F
|  / | \  |
| /  |  \ |
B -- D    E

Final offset assignments obtained with greedy coloring (happens to be optimal):
{'C': 2, 'B': 1, 'E': 0, 'D': 0, 'F': 1, 'A1': 0, 'A2': 0}
'''

import collections

Scope = collections.namedtuple('Scope', ['name', 'modules'])

A1 = Scope('A1', {'a', 'b', 'd', 'e'})
A2 = Scope('A2', {'l', 'm'})
B  = Scope('B',  {'b', 'c', 'd', 'e', 'f', 'g', 'h'})
C  = Scope('C',  {'e', 'f', 'g', 'h', 'i', 'l'})
D  = Scope('D',  {'f', 'g', 'h'})
E  = Scope('E',  {'i', 'j', 'k'})
F  = Scope('F',  {'i', 'l'})

scope_segments = [A1, A2, B, C, D, E, F]

# map of scope name -> offset
offsets = {}    

# NOTE: This is a non-recursive version; it doesn't do depth-first search.
# You can also make it recursive as the current C implementation does.
# (i.e., visit a segment, color it greedily, and visit its neighbor segments)
# I expect the recursive version would yield better assignments in general,
# but non-recursive version is easier to implement.
for i in scope_segments:
    occupied = set()
    for j in scope_segments:
        if i.name == j.name:
            continue

        # if i and j overlap and j has an assigned offset,
        # i must choose another offset
        if not i.modules.isdisjoint(j.modules) and j.name in offsets:
            occupied.add(offsets[j.name])

    off = 0
    while off in occupied:
        off += 1

    offsets[i.name] = off

print offsets
