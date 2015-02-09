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

#This script parses the Harmony backend output to plot (thread count, exec count) tuples
def main(arg):
    if (len(arg)) == 1:
        print "Usage: {0} input\n".format(arg[0])
        exit()
        
    p = 1
    
    fig = plt.figure(figsize=(7, 7))
    for harmony_in in arg[1:]:
        execTuple = []
        threadTuple = []
        zeroTuple = []
        max_threads = 0
        avg_val = 0
        with open(harmony_in, "r") as csvfile:
            #sorry to use blah, but I blanked on a name for this latest temp
            blah = csv.reader(csvfile)
            i = 0
            for row in blah:
                
                r = map(int, row)
                if (len(r) == 2):
                    continue
                sval = r[1] # includes 0:*
                zval = r[2] # removes 0:*
                
                execTuple.append(i)
                val = zval
                
                if (sval > zval):
                    zeroTuple.append(sval)
                    val = sval
                else:
                    threadTuple.append(zval)
                    val = zval
                if val > max_threads:
                    max_threads = val
                avg_val += val
                i += 1
            if i == 0:
                continue
            avg_val /= i
            
        if (p <= 28):
            if (len(arg) == 2):
                ax = fig.add_subplot(1,1, p)
            else:
                ax = fig.add_subplot(7,4, p)
            #plt.xlim(0, 17)
            #plt.plot(threadTuple, execTuple, 'k.', linestyle='None')
            
            tHist = [0.0] * (max_threads + 1)
            zHist = [0.0] * (max_threads + 1)
            print len(tHist)
            for t in threadTuple:
                tHist[t] += 1.0
            
            for z in zeroTuple:
                zHist[z] += 1.0
                
            nsum = sum(tHist) + sum(zHist)
            tFin = []
            for t in tHist:
                tFin.append(int(100.0 * (t / nsum)))
                
            zFin = []
            for z in zHist:
                zFin.append(int(100.0 * (z / nsum)))
            
            xval = range(0, max_threads+1)
            leftv = []
            for x in xval:
                leftv.append(x - 0.4)
            plt.bar(leftv, tFin, width=0.85,color='b')
            plt.bar(leftv, zFin, width=0.85,color='g', bottom=tFin)
            #plt.hist([tFin,zFin], bins=max_threads, align='left',stacked=True)
            plt.ylim(0,100)
            plt.vlines(avg_val, 0, 100, colors='r')
            
            if (len(arg) == 2):
                plt.xticks(fontsize=12)
                plt.yticks(fontsize=12)
            else:
                
                if (p % 4 == 1):
                    p = p
                elif (p % 4 == 0):
                    ax.yaxis.tick_right()
                    ax.yaxis.set_ticks_position('both')
                else:
                    ax.set_yticklabels([])
                plt.xticks(fontsize=5)
                plt.yticks(fontsize=5)
            
            plt.tick_params(axis='y', which='minor', left='off', right='off')
            harmony_l = harmony_in.split('/')
            harmony_in = harmony_l[-1]
            harmony_l = harmony_in.split('.')
            harmony_in = harmony_l[-2]
            t = plt.title(harmony_in, fontsize=5, verticalalignment='bottom')
            (x,y) = t.get_position()
            t.set_position((x, (y- 0.07)))
            p += 1
    plt.subplots_adjust(left=0.05, top = 0.98, bottom = 0.05, wspace = 0.1, hspace = 0.27)
    fig.text(0.5, 0.02, 'Unique Contexts', ha='center', va='center', fontsize=7)
    fig.text(0.01, 0.5, 'Fraction of Contexts Executing a Basic Block', ha='center', va='center', rotation='vertical', fontsize=7)

    plt.savefig( "temp.png")
    plt.savefig( "temp.pdf")
    
        
if __name__ == "__main__":
    main(sys.argv)
