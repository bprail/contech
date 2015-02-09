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
    for file_in in arg[1:]:
        
        avg = []
        # try:
        with open(file_in, "r") as csvfile:
            #sorry to use blah, but I blanked on a name for this latest temp
            dialect = csv.excel
            dialect.skipinitialspace = True
            blah = csv.reader(csvfile, dialect=dialect)

            for row in blah:
                r = map(float, row)
                avg.append(100.0*r[0])
                    
        if (len(avg) == 0):
            continue
        print len(avg)
        if (p <= 28):
            if (len(arg) == 2):
                ax = plt.subplot(1,1, p)
            else:
                ax = plt.subplot(7,4, p)
            #ax = plt.subplot(2,2, p)
            #plt.ylim(0,24)
            box = ax.get_position()
            ax.set_position([box.x0, box.y0, box.width, box.height*0.8])
            x_val = range(0, (len(avg)))
            x_val_exp = []
            for x in x_val:
                x_val_exp.append(math.pow(2, x))
            plt.plot(x_val_exp, avg)
            if (len(arg) == 2):
                plt.xticks(fontsize=12)
                plt.yticks(fontsize=12)
                plt.rc('font', size=12)
            else:
                plt.xticks(fontsize=5)
                plt.yticks(fontsize=5)
                plt.rc('font', size=5)
                ax.set_xscale('log', basex=2)
                plt.ticklabel_format(axis='x', style='plain')
                for tick in ax.xaxis.get_major_ticks():
                    tick.label.set_rotation('vertical')
                
            #Approach 2 - Project points based on a uniformly decreasing line
            ideal_m = (avg[-1] - avg[0]) / len(avg)
            culum = avg[0]
            proj = []
            for a in avg:
                proj.append(a - culum)
                culum += ideal_m
            accel = []
            last = 0
            for pr in proj:
                accel.append(pr - last)
                last = pr
            #plt.plot(range(10,10 + (len(avg))), proj)
            #plt.plot(range(10,10 + (len(avg))), accel)
            i = 0
            last = 0
            first_green = 0
            first_red = 0
            for a in accel:
                if i == 0:
                    i += 1
                    last = a
                    continue
                if last / a < 0: #is last > 0 and a < 0 or vis versa
                    if (a > 0):
                        c = 'r'
                        if (first_red == 0):
                            first_red = i
                    else:
                        c = 'g'
                        if (first_green == 0):
                            first_green = i
                  #  plt.axvline(i - 1 + 10, linewidth=1.25, color=c)
                last = a
                i += 1
            
            arrow_pt = 0
            if (first_green == 0 or first_red < first_green):
                arrow_pt = first_red
            else:
                arrow_pt = first_green
                
            if (arrow_pt < 3):
                arrow_pt = 3
            arrow_val = avg[arrow_pt]
            arrow_pt = math.pow(2, arrow_pt)
            
            plt.arrow(arrow_pt, arrow_val, arrow_pt, arrow_val + 1)
            plt.axvline(arrow_pt, linewidth=1.25, color='r')
            
            harmony_l = file_in.split('/')
            file_in = harmony_l[-1]
            harmony_l = file_in.split('.')
            file_in = harmony_l[-2]
            plt.title(file_in, fontsize=5)
        p += 1
#    plt.subplots_adjust(left=0.1, right =(0.1*max_threads))
    plt.savefig( "temp.pdf")
    plt.savefig( "temp.png") 
    

if __name__ == "__main__":
    main(sys.argv)