#!/usr/bin/python
import sys, re

if len(sys.argv) != 2:
    sys.exit("usage: sum.py [/proc/armperf_output]")

nsamples = 0
counter = re.compile(r'PMU.counter\[(\d+)\]= (\d+)')

sum = [ 0, 0, 0, 0, 0, 0 ]
for line in open(sys.argv[1]):
    m = counter.match(line)
    if m:
        idx = int(m.group(1))
        if idx == 0:
            nsamples += 1
        val = long(m.group(2))
        sum[idx] += val

print nsamples, " samples"
print sum
