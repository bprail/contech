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
import matplotlib as matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.colors as colors
import json
import math
import csv

def mergeBar(mergeCycle, barValLeft, barValHeight, barValBottom, barValColor):
    l = []
    h = []
    b = []
    c = []
    
    l.append(barValLeft[0])
    h.append(barValHeight[0])
    b.append(barValBottom[0])
    c.append(barValColor[0])
    
    for pos in range(1, len(barValLeft)):
        if (barValLeft[pos] == l[-1] and barValColor[pos] == c[-1]):
            if ((barValBottom[pos] - (b[-1] + h[-1])) < mergeCycle):
                h[-1] = (barValHeight[pos] + barValBottom[pos]) - b[-1]
                continue
        if (barValHeight[pos] < mergeCycle):
            continue
        l.append(barValLeft[pos])
        h.append(barValHeight[pos])
        b.append(barValBottom[pos])
        c.append(barValColor[pos])
    
    return (l, h, b, c)

#This script parses the Harmony backend output to plot (thread count, exec count) tuples
def main(arg):
    if (len(arg)) == 1:
        print "Usage: {0} input\n".format(arg[0])
        exit()

    for iVal in range(3, 5):#range(0, 4):
        fig = plt.figure(figsize=(10, 6)) #(7,7)
        p = 1
        for harmony_in in arg[1:]:
            
            nRes = 0
            cycBaselineRes = []
            
            barValLeft = []
            barValHeight = []
            barValBottom = []
            barValColor = []
            
            with open(harmony_in, "r") as csvfile:
                inputCSV = csv.reader(csvfile)
                
                #Critical Path CSV file has three sections:
                # Line 0: ConfHist, <int>* // These integers are the number of tasks with this config / resource as the bottleneck
                # Line 1: SMALL, // Smallest task with a slowdown
                # Line 2: SPEEDUP, ROI Time, Baseline Time, Ideal* Time, Ideal Time when Bottleneck Accelerated, Slack, 
                #            Cycles spent in Non-Work in Ideal Path,
                #            Time if only Bottleneck Accelerated, Length of Path based on Bottlenecks **
                #            Time with possible 2X all resources, Time with possible 200X all resources,
                #            Time with Ideal and Static Path
                # Line 3: PATH_IDEAL, (x, y, z, a)*
                #     x - TaskId
                #     y - start
                #     z - duration
                #     a - type of speedup (see below)
                # Line 4..N***: Cycles of Baseline Path in Resource, Cycles Saved of Resource in Bottlenecked Tasks in Ideal Path, 
                #                    # of Baseline Path Tasks Bottlenecked on Resource,
                #                    Cycles saved with 2x all resources,
                #                    Cycles saved with 200x all resources
                # Line N+4..Q: L->F, Conf0..N // Transitions between LastBB to FirstBB and which config is selected
                #   * Ideal - No Reconfiguration modeling
                #     Slack - Cycles in any context that are "idle" (i.e., slack)
                #   ** Each critical path can be different depending on what resource is accelerated and by how much
                #   *** Last "resource" should capture all non-resource tasks (i.e., non-work)
                # Line Q..Q+C: C is number of contexts
                #     C#, x, y, z, x', y', z' ...
                #     x - start of task
                #     y - duration of task
                #     z - type of speedup (0 - NONE, 1 - Integer, 2 - Float, 3 - Mem, 4 - No Speedup)
                line = 0
                gStaticSum = 0.0
                gStaticMax = 0
                perStatic = []
                perCon = []
                idealSpeedupLen = 0
                minH = 0
                nCon = 0
                for row in inputCSV:
                    if (line < 2):
                        line += 1
                        continue
                    if "->" in row[0]:
                        if iVal != 2: 
                            continue
                        val = map(int, row[1:])
                        sum = 0.0
                        max = 0
                        for v in val:
                            if v > max:
                                max = v
                            sum += v
                        if (sum == 0):
                            continue
                        perStatic.append(max / sum)
                        gStaticMax += max
                        gStaticSum += sum
                        continue
                    if "PERCONTEXT" in row[0]:
                        if iVal != 0: 
                            continue
                        val = map(float, row[1:])
                        perCon = val
                        continue
                    if "SPEED" in row[0]:
                        val = map(int, row[1:])
                        idealSpeedupLen = val[2]
                        minH = idealSpeedupLen
                        continue
                    if "SYN" in row[0]:
                        continue
                    if "PATH_IDEAL" in row[0] or "C" in row[0]:
                        if (iVal != 4 and iVal != 3):
                            continue
                        tuples = (len(row) - 1) / 4
                        count = 0
                        nCon = 16
                        for pos in range(0, tuples):
                            cnumStr = row[1 + pos*4].split(":")
                            cnum = int(cnumStr[0])
                            spos = 2 + pos * 4
                            val = map(int, row[spos:spos+3])
                            if (cnum > nCon):
                                nCon = cnum
                            t = 0
                            bot = 0
                            height = 0
                            hc = 0
                            for v in val:
                                if t == 0:
                                    t = 1
                                    bot = v
                                elif t == 1:
                                    t = 2
                                    height = v
                                    if (v < minH and v > 0):
                                        minH = v
                                elif t == 2:
                                    t = 0
                                    if (v == 0):
                                        continue
                                    elif (v == 1):
                                        c = 'g'
                                    elif (v == 2):
                                        c = 'r'
                                    elif (v == 3):
                                        c = 'b'
                                    elif (v == 4):
                                        c = 'k'
                                    if (height == 0):
                                        continue
                                    count += 1
                                    barValBottom.append(bot)
                                    barValHeight.append(height)
                                    barValColor.append(c)
                                    barValLeft.append(cnum - 0.5)
                        nCon += 1
                        continue
                    # if "C" in row[0]:
                        # if (iVal == 3):
                            # cnum = int(row[0][1:])
                            # val = map(int, row[1:])
                            # nCon += 1
                            # t = 0
                            # bot = 0
                            # height = 0
                            # hc = 0
                            # count = 0
                            # for v in val:
                                # if t == 0:
                                    # t = 1
                                    # bot = v
                                # elif t == 1:
                                    # t = 2
                                    # height = v
                                    # if (v < minH and v > 0):
                                        # minH = v
                                # elif t == 2:
                                    # t = 0
                                    # if (v == 0):
                                        # continue
                                    # elif (v == 1):
                                        # c = 'g'
                                    # elif (v == 2):
                                        # c = 'r'
                                    # elif (v == 3):
                                        # c = 'b'
                                    # elif (v == 4):
                                        # c = 'k'
                                    # if (height == 0):
                                        # continue
                                    # count += 1
                                    # barValBottom.append(bot)
                                    # barValHeight.append(height)
                                    # barValColor.append(c)
                                    # barValLeft.append(cnum - 0.5)
                        # continue
                    if (iVal > 1): 
                        continue
                    val = map(int, row)
                    cycBaselineRes.append(val[iVal])
                    nRes+=1
            if (iVal == 2):
                if (gStaticSum > 0):
                    print harmony_in, (gStaticMax / gStaticSum), np.median(perCon) #, perStatic
                continue
                    
            if (nRes == 0 and len(barValLeft) == 0):
                continue

            if (p <= 28):
                
            
                if (len(arg) == 2):
                    ax = fig.add_subplot(1,1, p)
                else:
                    ax = fig.add_subplot(4,5, p)#((5,4,p)
                xval = range(0, nRes)
                leftv = []
                for x in xval:
                    leftv.append(x - 0.4)
                
                if (iVal == 4):
                    barValLeft.reverse()
                    barValHeight.reverse()
                    barValBottom.reverse()
                    barValColor.reverse()
                
                if (iVal in range(0, 3)):
                    plt.bar(leftv, cycBaselineRes)
                    plt.xlim(0, nRes)
                elif (iVal == 3 or iVal == 4):
                    mergeCycle = 10
                    lenPrev = 0
                    while (len(barValLeft) > 10000 and (len(barValLeft) != lenPrev or len(barValLeft) > 15000)):
                        lenPrev = len(barValLeft)
                        (barValLeft, barValHeight, barValBottom, barValColor) = mergeBar(mergeCycle, barValLeft, barValHeight, barValBottom, barValColor)
                        print len(barValLeft), mergeCycle
                        mergeCycle *= 2
                    maxC = 0
                    for l in barValLeft:
                        if l > maxC:
                            maxC = l
                    nCon = maxC + 1
                    plt.bar(barValLeft, barValHeight, width = 1.0, bottom = barValBottom, color = barValColor, edgecolor = [])
                    plt.xlim(-0.5, nCon - 0.5)
                    #plt.ylim(0, 2500000000) # REMOVE
                
                if (len(arg) == 2):
                    plt.xticks(fontsize=12)
                    plt.yticks(fontsize=12)
                else:
                    plt.xticks(fontsize=5)
                    plt.yticks(fontsize=5)
                    plt.rc('font', size=5)
                harmony_l = harmony_in.split('/')
                harmony_in = harmony_l[-1]
                harmony_l = harmony_in.split('.')
                harmony_in = harmony_l[-2]
                
                if (iVal == 3 or iVal == 4):
                    print harmony_in, len(barValLeft), minH, mergeCycle / 2
                t = plt.title(harmony_in, fontsize=5, verticalalignment='bottom')
                (x,y) = t.get_position()
                t.set_position((x, (y- 0.05)))
                p += 1
        plt.subplots_adjust(left=0.05, right = 0.98, top = 0.95, bottom = 0.06, wspace = 0.2, hspace = 0.35)
        if (iVal == 0):
            atext = "Cycles in Each Resource Along Baseline Critical Path"
            btext = " "
        elif (iVal == 1):
            atext = "Cycles Saved in Each Resource Along Ideal Speedup Path"
            btext = " "
        elif(iVal == 2):
            atext = "Number of Tasks for Each Resource Along Baseline Critical Path"
            btext = " "
        elif(iVal == 3):
            atext = "Context Number"
            btext = "Time in Cycles"
        elif(iVal == 4):
            atext = "Context Number"
            btext = "Time in Cycles"
        fig.text(0.5, 0.02, atext, ha='center', va='center', fontsize=8)
        fig.text(0.01, 0.5, btext, ha='center', va='center', rotation='vertical', fontsize=8)     
        if (iVal == 0):
            plt.savefig("baseRes.pdf")
        elif (iVal == 1):
            plt.savefig("saveRes.pdf")
        elif (iVal == 2):
            plt.savefig("taskCount.pdf")
        elif (iVal == 3):
            #plt.savefig("resVTime.pdf")
            plt.savefig("resVTime.png")
        elif (iVal == 4):
            plt.savefig("critPathTime.pdf")

if __name__ == "__main__":
    main(sys.argv)
