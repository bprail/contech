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

def makeUnit(v):
    unit = "B"
    if (v > 10000):
        v /= 1024
        unit = "KB"
    if (v > 10000):
        v /= 1024
        unit = "MB"
    if (v > 10000):
        v /= 1024
        unit = "GB"
    v = int(v)
    return str(v) + unit

def main(arg):
    if (len(arg)) == 1:
        print "Usage: {0} input\n".format(arg[0])
        exit()
    
    p = 1
    for harmony_in in arg[1:]:
        
        # try:
        with open(harmony_in, "r") as csvfile:
            #sorry to use blah, but I blanked on a name for this latest temp
            blah = csv.reader(csvfile)
            rowNum = 0
            meanSize = 0
            totalAlloc = 0
            sizeList = []
            freqList = []
            for row in blah:
                if (rowNum == 0):
                    stats = []
                    for v in row[1:-1]:
                        stats.append(makeUnit(int(v)))
                    #stats = row[1:]
                    stats.append(str(100.0*float(row[-1])))
                    rowNum += 1
                    continue
                if (len(row) < 3):
                    rowNum += 1
                    continue
                    
                r = map(int, row)
                size = r[0]
                numAlloc = r[1]
                meanSize += size
                totalAlloc += numAlloc
                sizeList.append( size)
                freqList.append(numAlloc)
            
            if (rowNum == 0):
                continue
            
            if (totalAlloc > 0):
                meanSize = meanSize / totalAlloc
                medianNum = (totalAlloc + 1) / 2
                pos = 0
                count = 0
                for a in sizeList:
                    count += freqList[pos]
                    if (count >= medianNum):
                        medianSize = a
                        break
                    pos += 1
            
            harmony_l = harmony_in.split('/')
            harmony_in = harmony_l[-1]
            harmony_l = harmony_in.split('.')
            harmony_in = harmony_l[-2]
            pstr = harmony_in
            pstats = ','.join(s for s in stats)
            meanStr = makeUnit(meanSize)
            medianStr = makeUnit(medianSize)
            pstr = pstr + "," + pstats + ", {0}, {1}".format(meanStr, medianStr)
            print pstr
            
if __name__ == "__main__":
    main(sys.argv)
