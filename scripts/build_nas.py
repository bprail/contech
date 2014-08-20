#!/usr/bin/env python

import os
import argparse
import subprocess
import shutil
from util import pcall, Timer
import util

def main():
    parser = argparse.ArgumentParser(description="Builds a nas benchmark with contech")
    parser.add_argument("benchmark", help="The nas benchmark to build.")
    parser.add_argument("-i", "--input", help="The input size to use.", default="S")
    parser.add_argument("-c", "--config", help="The configuration to use.", default="contech")
    
    args = parser.parse_args()
    
    if os.environ.has_key("CONTECH_HOME"):
        CONTECH_HOME = os.environ["CONTECH_HOME"]
        stateFile = os.path.join(CONTECH_HOME, "scripts/output/", args.benchmark + ".stateFile.temp")
        os.environ["CONTECH_STATE_FILE"] = stateFile
    else:
        print ">Error: Could not find contech installation. Set CONTECH_HOME to the root of your contech directory."
        exit(1)

    if os.environ.has_key("NAS_HOME"):
        NAS_HOME = os.environ["NAS_HOME"]
    else:
        print ">Error: Could not find NAS installation. Set NAS_HOME to the root of your NAS directory."
        exit(1)
    
    # Prepare state file for run
    if os.path.exists(stateFile):
        os.remove(stateFile)
        
    with Timer("Build"):
        #Change directory to NAS_HOME
        savedPath = os.getcwd()
        os.chdir(NAS_HOME)
        pcall(["rm -f {0}/*.o {0}/*.ll {0}/*.bc".format(args.benchmark.upper())])
        returnCode = pcall(["make", args.benchmark, "CLASS={}".format(args.input), "CONFIG={}.def".format(args.config)], returnCode=True)
        os.chdir(savedPath)
    
    # Clean up
    if os.path.exists(stateFile):
        os.remove(stateFile)
    
    if returnCode != 0:
        util.print_error("Build failed")
        exit(1)
    
    
if __name__ == "__main__":
    main()
