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
    else:
        harmony_in = arg[1]
        
    max_threads = 0
    max_value = 0
    bbData = [[]]
    # try:
    with open(harmony_in, "r") as csvfile:
        #sorry to use blah, but I blanked on a name for this latest temp
        blah = csv.reader(csvfile)
        i = 0
        for row in blah:
            r = map(int, row)
            bbData.append(r)
            j = 0
            for d in r:
                if (d != 0 and j > max_threads):
                    max_threads = j
                if (d > max_value):
                    max_value = d
                j += 1
            i += 1
    # except:
        # print "Read failed\n"
        # exit()
    print "Max Threads: {0}\tMax Value: {1}\n".format(max_threads, max_value)
    
    ax = plt.subplot(111)
    plt.xlim(1,max_threads+1)
    plt.ylim(0,len(bbData))
    ypos = 0
    max_log = math.log(max_value, 10)
    xset = []
    yset = []
    dset = []
    cset = []
    for r in bbData:
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
    plt.hist2d(xset,yset,weights=dset,cmap=matplotlib.cm.Blues,norm=colors.LogNorm(),range=np.array([(1, max_threads+1), (0, len(bbData))]),bins=[max_threads,len(bbData)])
    plt.xlabel("Active Threads")
    plt.ylabel("Basic Block ID")
    tickPositions = [a + .5 for a in range(1, max_threads+1)]
    plt.xticks(tickPositions, range(1, max_threads+1))
    plt.colorbar()
#     plt.subplots_adjust(left=0.1, right =(0.1*max_threads))
    plt.savefig( "temp.png")
    
    

if __name__ == "__main__":
    main(sys.argv)