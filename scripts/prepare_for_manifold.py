#!/usr/bin/env python

import os
import shutil
from util import pcall
import util
import argparse

def main():
    CONTECH_HOME = util.findContechInstall()
    CONTECH_WRAPPER = os.path.join(CONTECH_HOME, "scripts/contech_wrapper.py")
    CONTECH_MARKER = os.path.join(CONTECH_HOME, "scripts/contech_marker.py")
    stateFile = os.path.join(CONTECH_HOME, "scripts/output/contechStateFile.temp")
    
    OBJPARSE = os.path.join(CONTECH_HOME, "scripts/objparse.py")
    RUN = os.path.join(CONTECH_HOME, "scripts/run.py")

    parser = argparse.ArgumentParser(description='Generates a task graph with a matching marked object file for a parsec benchmark')
    parser.add_argument('benchmark', help='The simple benchmark to build.')
    args = parser.parse_args()

    # Reset state file
    if os.path.exists(stateFile):
        os.remove(stateFile)
    
    # Marker run
    pcall([CONTECH_MARKER, args.benchmark + ".c", "-o", args.benchmark, "-lpthread", "-O3"])
    
    # Objparse
    markedObjectFile = os.path.join(CONTECH_HOME, "middle/output/" + os.path.basename(args.benchmark) + ".mo")
    pcall([OBJPARSE, args.benchmark, markedObjectFile])
    
    # Reset state file
    os.remove(stateFile)
    
    # Actual build
    pcall([CONTECH_WRAPPER, args.benchmark + ".c", "-o", args.benchmark, "-lpthread", "-O3"])
    
    # Generate taskgraph
    pcall([RUN, args.benchmark])
        
    
if __name__ == "__main__":
    main()
