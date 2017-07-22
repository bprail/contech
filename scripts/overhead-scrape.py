#!/usr/bin/env python

import os
import sys
sys.path.append(os.path.join(os.environ["CONTECH_HOME"], "scripts"))
import util
import subprocess
import shutil
import time
import datetime
import glob
import json
import math
import csv
import re

def main(arg):
    if (len(arg)) == 1:
        print "Usage: {0} input\n".format(arg[0])
        exit()
    
    p = 1
    for perf_in in arg[1:]:
    
    # try:
        with open(perf_in, "r") as csvfile:
            #sorry to use blah, but I blanked on a name for this latest temp
            blah = csv.reader(csvfile)
            i = 0
            bench = perf_in.split('/')
            perf_in = bench[-1]
            bench = perf_in.split('.')
            perf_in = bench[-2]
            totalBuffersQueued = 0
            totalCyclesQueued = 0
            totalCyclesNotQueued = 0
            start = 0.0
            end = 0.0
            for row in blah:
                # FIND CT_START and CT_END
                if (len(row) != 4):
                    # Search for timestamps
                    try:
                        m = re.search(r"CT_(\w+): (\d+[.]\d+)", row[0])
                        if m:
                            if m.group(1) == "START":
                                start = float(m.group(2))
                            if m.group(1) == "END":
                                end = float(m.group(2))
                    except IndexError:
                        start = start
                    continue
                try:
                    if (row[0][0:2] != "T("):
                        continue
                    a = int(row[1])
                    b = int(row[2])
                    c = int(row[3])
                    totalCyclesQueued += a
                    totalCyclesNotQueued += b
                    totalBuffersQueued += c
                except IndexError:
                    i = 0
                except ValueError:
                    i = 0
            if (start == 0.0 or end == 0.0):
                continue
            if (totalCyclesQueued == 0):
                print "{}, {}".format(perf_in, end - start)
            else:
                print "{}, {}, {}, {}, {}".format(perf_in, end - start, totalCyclesQueued, totalCyclesNotQueued, totalBuffersQueued)
            #sys.stdout.write(perf_in)
            #sys.stdout.write("\n")
 
if __name__ == "__main__":
    main(sys.argv)