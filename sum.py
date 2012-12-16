#!/usr/bin/python
import sys

if len(sys.argv) != 2:
    sys.exit("usage: sum.py [file_with_numbers]")

sum = 0
for line in open(sys.argv[1]):
    val = long(line)
    sum += val

print sum
