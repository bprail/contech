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
import scipy.cluster.vq as vq

def main(arg):

    p = 1
    fig = plt.figure(figsize=(7, 7))
    subval = int(arg[1])
    obs = []
    for harmony_in in arg[2:]:
    
        with open(harmony_in, "r") as csvfile:
            #sorry to use blah, but I blanked on a name for this latest temp
            dialect = csv.excel
            dialect.skipinitialspace = True
            blah = csv.reader(csvfile, dialect=dialect)

            xset = []
            yset = []
            dset = []
            xavg = []
            yavg = []
            xmed = []
            ymed = []
            cset = []
            bbid = -1
            maxBBID = 0
            maxBucket = 0
            count = 0
            grandSum = 0
            grandCount = 0
            memSum = 0
            irSum = 0
            critSum = 0
            containCall = 0
            nonCallSum = 0
            for row in blah:
                if (len(row) == 0):
                    continue
                r = map(int, row)
                #bbid = r[0]
                if (subval == 0):
                    bbid = count
                else:
                    bbid = r[subval]
                memOp = r[3]
                irOp = r[4]
                critOp = r[5]
                
                
                if (len(row) == 9 and r[7] == 1):
                    continue
                if (r[2] != containCall):
                    continue
                
                if (bbid > maxBBID):
                    maxBBID = bbid
                i = 0
                count += 1
                
                sum = 0
                icount = 0
                for d in r[6:-2]:
                    if (i == 0):
                        bucket = d
                        if (bucket > 0 and math.log(bucket,2) > maxBucket):
                            maxBucket = math.log(bucket,2)
                        i = 1
                    else:
                        value = d
                        if (value > 0):
                            xset.append(bbid)
                            yset.append(math.log(bucket,2))
                            #yset.append(bucket)
                            dset.append(value)
                            #dset.append(math.log(value, 10))
                            sum += bucket * value
                            memSum += memOp * value
                            irSum += irOp * value
                            critSum += critOp * value
                            icount += value
                            #print bbid, bucket, value
                        i = 0
                if (sum > 0):
                    xavg.append(bbid)
                    #print sum, icount
                    #yavg.append(sum / icount)
                    yavg.append(math.log((sum / icount), 2))
                    grandSum += sum
                    grandCount += icount
                
                i = 0
                tgtValue = icount / 2
                tcount = 0
                for d in dset:
                    if tcount + d > tgtValue:
                        break
                    i += 1
                    tcount += d
                xmed.append(bbid)
                ymed.append(yset[i])
            
            containCall = 1
            vlinePoint1 = count
            csvfile.seek(0)
            blah = csv.reader(csvfile, dialect=dialect)
            for row in blah:
                if (len(row) == 0):
                    continue
                r = map(int, row)
                #bbid = r[0]
                if (subval == 0):
                    bbid = count
                else:
                    bbid = r[subval]
                memOp = r[3]
                irOp = r[4]
                critOp = r[5]
                
                
                if (len(row) == 9 and r[7] == 1):
                    continue
                if (r[2] != containCall):
                    continue
                
                if (bbid > maxBBID):
                    maxBBID = bbid
                i = 0
                count += 1
                
                sum = 0
                icount = 0
                for d in r[6:-2]:
                    if (i == 0):
                        bucket = d
                        if (bucket > 0 and math.log(bucket,2) > maxBucket):
                            maxBucket = math.log(bucket,2)
                        i = 1
                    else:
                        value = d
                        if (value > 0):
                            xset.append(bbid)
                            yset.append(math.log(bucket,2))
                            #yset.append(bucket)
                            dset.append(value)
                            #dset.append(math.log(value, 10))
                            sum += bucket * value
                            memSum += memOp * value
                            irSum += irOp * value
                            critSum += critOp * value
                            icount += value
                            #print bbid, bucket, value
                        i = 0
                if (sum > 0):
                    xavg.append(bbid)
                    #print sum, icount
                    #yavg.append(sum / icount)
                    yavg.append(math.log((sum / icount), 2))
                    grandSum += sum
                    grandCount += icount
                
                i = 0
                tgtValue = icount / 2
                tcount = 0
                for d in dset:
                    if tcount + d > tgtValue:
                        break
                    i += 1
                    tcount += d
                xmed.append(bbid)
                ymed.append(yset[i])
            nonCallSum = grandSum
            vlinePoint2 = count
            csvfile.seek(0)
            blah = csv.reader(csvfile, dialect=dialect)
            for row in blah:
                if (len(row) == 0):
                    continue
                r = map(int, row)
                #bbid = r[0]
                if (subval == 0):
                    bbid = count
                else:
                    bbid = r[subval]
                memOp = r[3]
                irOp = r[4]
                critOp = r[5]
                
                
                if (len(row) == 9 and r[7] == 1):
                    continue
                if (r[1] != containCall):
                    continue
                
                if (bbid > maxBBID):
                    maxBBID = bbid
                i = 0
                count += 1
                
                sum = 0
                icount = 0
                for d in r[6:-2]:
                    if (i == 0):
                        bucket = d
                        if (bucket > 0 and math.log(bucket,2) > maxBucket):
                            maxBucket = math.log(bucket,2)
                        i = 1
                    else:
                        value = d
                        if (value > 0):
                            xset.append(bbid)
                            yset.append(math.log(bucket,2))
                            #yset.append(bucket)
                            dset.append(value)
                            #dset.append(math.log(value, 10))
                            sum += bucket * value
                            memSum += memOp * value
                            irSum += irOp * value
                            critSum += critOp * value
                            icount += value
                            #print bbid, bucket, value
                        i = 0
                if (sum > 0):
                    xavg.append(bbid)
                    #print sum, icount
                    #yavg.append(sum / icount)
                    yavg.append(math.log((sum / icount), 2))
                    grandSum += sum
                    grandCount += icount
                
                i = 0
                tgtValue = icount / 2
                tcount = 0
                for d in dset:
                    if tcount + d > tgtValue:
                        break
                    i += 1
                    tcount += d
                xmed.append(bbid)
                ymed.append(yset[i])
                
            if count == 0:
                continue
            
            harmony_l = harmony_in.split('/')
            harmony_in = harmony_l[-1]
            harmony_l = harmony_in.split('.')
            harmony_in = harmony_l[-2]
            rates = []
            rates.append((float(grandSum) / grandCount))
            rates.append((float(grandSum) / memSum))
            rates.append((float(grandSum) / irSum))
            rates.append(float(grandSum) / critSum)
            obs.append(rates)
            print harmony_in, maxBBID, maxBucket, (nonCallSum / float(grandSum)), (float(grandSum) / grandCount), (float(grandSum) / memSum), (float(grandSum) / irSum), (float(grandSum) / critSum)
            ax = plt.subplot(7,4, p)
            plt.subplots_adjust(hspace=0.45, wspace=0.1)
            box = ax.get_position()     
            tickPositions = [a + .5 for a in range(0, maxBBID)]
            plt.xticks(fontsize=5)
            plt.yticks(fontsize=5)
            if (p % 4 == 1):
                p = p
            elif (p % 4 == 0):
                ax.yaxis.tick_right()
                ax.yaxis.set_ticks_position('both')
            else:
                ax.set_yticklabels([])
            plt.hist2d(xset,yset,weights=dset,cmap=matplotlib.cm.Blues,norm=colors.LogNorm(),bins=[maxBBID,maxBucket],range=np.array([(0, maxBBID+1), (0, 25)]))
            if (subval == 0):
                plt.plot(xavg,yavg, linewidth=1, color='k')
                plt.plot(xmed,ymed, linewidth=1, color='r')
                # plt.axvline(average, linewidth=1.25, color='r')
                plt.axvline(vlinePoint1, linewidth=1.25, color='g')
                plt.axvline(vlinePoint2, linewidth=1.25, color='g')
            #plt.hist2d(xset,yset,weights=dset,cmap=matplotlib.cm.Blues,norm=colors.LogNorm(),range=np.array([(1, maxBBID+1), (0, maxBucket)]),bins=[maxBBID,maxBucket])
            plt.rc('font', size=5)

            plt.title(harmony_in, fontsize=6)
            p += 1
        
    plt.savefig( "temp.png", bbox_inches='tight')
    plt.savefig( "temp.pdf", bbox_inches='tight')
    (centroid, labels) = vq.kmeans2(np.array(obs), 4, 100)
    print centroid
    print labels

if __name__ == "__main__":
    main(sys.argv)