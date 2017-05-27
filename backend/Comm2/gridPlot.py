#!/usr/bin/env python

import os
import sys
sys.path.append(os.path.join(os.environ["CONTECH_HOME"], "scripts"))
import util
import argparse
import subprocess
import shutil
import time
import datetime
import glob
import matplotlib
#matplotlib.use('Agg') 
import numpy as np
import matplotlib.pyplot as plt
import json
import math

        
def main():

    # Parse commandline arguments
    parser = argparse.ArgumentParser(description="Runs benchmark that has been compiled with contech, generating a task graph and optionally running backend tools.")
    parser.add_argument("inFile", help="Input file, a json file of commRecords")
    parser.add_argument("outFile", help="Output file, a png of inter-thread communication.")
    args = parser.parse_args()
        
    x, y = [], []
    nThreads = 0
    print "Loading {}...".format(args.inFile)
    with open(args.inFile) as file:
        data = json.load(file)
        records = data["records"]
        print "Loaded {} records".format(len(records))
        
        for r in records:
            src = int(r["src"].split(":")[0])
            dst = int(r["dst"].split(":")[0])
            nThreads = max([nThreads, src, dst])
            x.append(dst)
            y.append(src)
    
    nThreads = nThreads + 1 # nThreads is the max, ranges need to include it    
    print "Plotting for {} threads, {} communication records...".format(nThreads, len(x))
    
#     plt.title(os.path.basename(args.inFile).replace(".json",""))
    plt.xlabel("consumer CPU")    
    plt.ylabel("producer CPU")
    tickPositions = [a + .5 for a in range(nThreads)]
    plt.xticks(tickPositions, range(nThreads))
    plt.yticks(tickPositions, range(nThreads))
    plt.hist2d(x, y, bins=nThreads, cmap=matplotlib.cm.Greys)
    plt.colorbar()
    plt.savefig(args.outFile)        
                              
if __name__ == "__main__":
    main()
