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
    fig = plt.figure(figsize=(7, 7))
    for harmony_in in arg[1:]:
        max_threads = 0
        max_value = 0
        bbData = [[]]
        bbSort = { }
        bbSum = []
        max_weight = 0
        # try:
        with open(harmony_in, "r") as csvfile:
            #sorry to use blah, but I blanked on a name for this latest temp
            blah = csv.reader(csvfile)
            i = 0
            for row in blah:
                #first row is histogram
                if (i == 0):
                    i += 1
                    continue
                r = map(int, row)
                nz = 0
                for d in r:
                    if d > 0:
                        nz = 1
                        break
                if nz == 0:
                    continue
                
                j = 0
                sum_value = 0
                wsum_value = 0
                for d in r:
                    if (d != 0 and j > max_threads):
                        max_threads = j
                    if (d > max_value):
                        max_value = d
                    wsum_value += d * j
                    sum_value += d
                    j += 1
                if (sum_value < 10):
                    #NB Since nothing is appended, don't increment i
                    continue
                bbSort[i] = wsum_value / sum_value
                bbSum.append(sum_value)
                bbData.append(r)
                if bbSort[i] > max_weight:
                    max_weight = bbSort[i]
                i += 1
        # except:
            # print "Read failed\n"
            # exit()
        if (max_threads == 0):
            continue
        print "Max Threads: {0}\tMax Value: {1}\tMax Weight: {2}\tNonZero BBV: {3}".format(max_threads, max_value, max_weight, len(bbData))
        
        if (p <= 30):
            ax = plt.subplot(7,4, p)
            plt.xlim(1,max_threads+1)
            plt.ylim(0,len(bbData))
            ypos = 0
            max_log = math.log(max_value, 10)
            xset = []
            yset = []
            dset = []
            cset = []
            # Add sort step using average parallelism
            s = sorted(bbSort, key=lambda k:(bbSort[k],k))
            current_weight = max_weight
            sumSort = {}
            for e in s:
                # Note that bbData[0] = [[]], while 1 .. n are valid
                val = bbSum[e - 1]
                w = bbSort[e]
                if (w == current_weight):
                    sumSort[e] = val
                    continue
                else:
                    f = sorted(sumSort, key=lambda k:(sumSort[k], k))
                    for i in f:
                        r = bbData[i]
                        xpos = 0
                        for d in r:
                            if (d == 0):
                                xpos += 1
                                continue
                            t = math.log(d, 10)
                            c = (0,0, 1.0 * (t / max_log))
                            #ax.plot( xpos, ypos, color=c, marker='.')
                            xset.append(xpos)
                            yset.append(ypos)
                            dset.append(d)
                            cset.append(c)
                            xpos += 1
                        ypos += 1
                    sumSort.clear()
                    sumSort[e] = val
                    current_weight = w
            f = sorted(sumSort, key=lambda k:(sumSort[k], k))
            for i in f:
                r = bbData[i]
                xpos = 0
                for d in r:
                    if (d == 0):
                        xpos += 1
                        continue
                    t = math.log(d, 10)
                    c = (0,0, 1.0 * (t / max_log))
                    #ax.plot( xpos, ypos, color=c, marker='.')
                    xset.append(xpos)
                    yset.append(ypos)
                    dset.append(d)
                    cset.append(c)
                    xpos += 1
                ypos += 1
            #plt.hexbin(xset,yset,C=dset, bins='log',gridsize=(max_threads,len(bbData)))
            plt.xticks(fontsize=5)
            plt.yticks(fontsize=5)
            plt.hist2d(xset,yset,weights=dset,cmap=matplotlib.cm.Blues,norm=colors.LogNorm(),range=np.array([(1, max_threads+1), (0, len(bbData))]),bins=[max_threads,len(bbData)])
            #plt.xlabel("Active Threads")
            #plt.ylabel("Basic Block ID")
            #tickPositions = [a + .5 for a in range(1, max_threads+1)]
            #plt.xticks(tickPositions, range(1, max_threads+1))
            #plt.colorbar()
            harmony_l = harmony_in.split('/')
            harmony_in = harmony_l[-1]
            harmony_l = harmony_in.split('.')
            harmony_in = harmony_l[-2]
            plt.title(harmony_in, fontsize=5)
            p = p + 1
#     plt.subplots_adjust(left=0.1, right =(0.1*max_threads))
    plt.savefig( "temp.png")
    plt.savefig( "temp.pdf")
    
    

if __name__ == "__main__":
    main(sys.argv)