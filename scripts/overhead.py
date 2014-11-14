#!/usr/bin/env python

import os
import argparse
import subprocess
import shutil
import util
import glob

def main():
    CONTECH_HOME = util.findContechInstall()
    script = """
    cd /net/tinker/brailing/contech/scripts
    
    ./build{3}.py {0}
    ./run{3}.py --traceOnly {0} -n 16 -i {2} --time "{1}"
"""
    bset = util.Benchmarks.all
    time = "/usr/bin/time"
    for b in bset:
        util.quicksub(name="{}_{}".format(b, "perf"), code=script.format(b, time, "simmedium", "_parsec"), resources=["nodes=1:ppn=24"], queue="newpasta")
    for b in util.Benchmarks.nas:
        util.quicksub(name="{}_{}".format(b, "perf"), code=script.format(b, time, "A", "_nas"), resources=["nodes=1:ppn=24"], queue="newpasta") 
        

if __name__ == "__main__":
    main()
