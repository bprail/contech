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
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.colors as colors
import matplotlib as matplotlib
import json
import math
import csv

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
            sys.stdout.write(perf_in)
            for row in blah:
                if (len(row) != 2):
                    continue
                try:
                    a = int(row[0])
                    if (i == 0):
                        i = a
                    else:
                        v = float(a) / float(i)
                        sys.stdout.write(", {}".format(v))
                        i = 0
                except IndexError:
                    i = 0
                except ValueError:
                    i = 0
            sys.stdout.write("\n")
 
if __name__ == "__main__":
    main(sys.argv)