#!/usr/bin/env python

import os
import argparse
import subprocess
import shutil
import util
import glob

#/net/tinker/brailing/contech/benchmarks/parsec-3.0/bin/parsecmgmt -a run -p fluidanimate -c llvm -d /tmp/bpr -n 16 -i simmedium -s "perf stat -e L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores,L1-dcache-store-misses,branches,branch-misses,L1-icache-loads,L1-icache-load-misses,instructions,cycles /net/tinker/brailing/contech/../pin/pin-2.13-62732-gcc.4.4.7-linux/pin -t /net/tinker/brailing/contech/../pin/pin-2.13-62732-gcc.4.4.7-linux/source/tools/ManualExamples/obj-intel64/contech_fe.so --"

def main():
    CONTECH_HOME = util.findContechInstall()
    script = """
    cd /net/tinker/brailing/contech/scripts
    
    ./build{3}.py {0}
    ./run{3}.py --traceOnly --discardTrace {0} -n 16 -i {2} --time "{1}"
    ./run{3}.py --traceOnly --discardTrace {0} -n 16 -i {2} --time "{1}" --pinFrontend
    {4}
"""
    bset = util.Benchmarks.all
    time = "perf stat -x, -e L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores,L1-dcache-store-misses,branches,branch-misses,L1-icache-loads,L1-icache-load-misses,instructions,cycles"
    for b in bset:
        util.quicksub(name="{}_{}".format(b, "perf"), code=script.format(b, time, "simmedium", "_parsec",nativeRunParsec(b, 16, "simmedium", time)), resources=["nodes=1:ppn=24"], queue="newpasta")
    for b in util.Benchmarks.nas:
        util.quicksub(name="{}_{}".format(b, "perf"), code=script.format(b, time, "A", "_nas", nativeRunNas(b, 16, "A", time)), resources=["nodes=1:ppn=24"], queue="newpasta") 
        
def nativeRunNas(benchmark, n, input, time):
    PARSEC_HOME = util.findParsecInstall()
    script = """
    mkdir /tmp/$USER
    cp $NAS_HOME/bin-llvm/{0}.{2}.x /tmp/$USER
    cd /tmp/$USER
    setenv OMP_NUM_THREADS {1}
    {3} /tmp/$USER/{0}.{2}.x 
    cd -
    rm -f /tmp/$USER/*
"""
    script = script.format(benchmark, n, input, time)
    
    return script
    
def nativeRunParsec(benchmark, n, input, time):
      
    PARSEC_HOME = util.findParsecInstall()
    script = """
    mkdir /tmp/$USER
    $PARSEC_HOME/bin/parsecmgmt -a run -p {0} -c llvm -d /tmp/$USER -n {1} -i {2} -s "{3}"
"""
    script = script.format(benchmark, n, input, time)
    
    return script

if __name__ == "__main__":
    main()