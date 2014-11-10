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
        if (p <= 30):
            ax = plt.subplot(7,4, p)
            #ax = plt.subplot(2,2, p)
            #plt.ylim(0,24)
            box = ax.get_position()
            ax.set_position([box.x0, box.y0, box.width, box.height*0.8])
            plt.plot(range(10,10 + (len(avg))), avg)
            plt.xticks(fontsize=5)
            plt.yticks(fontsize=5)
            plt.rc('font', size=5)
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