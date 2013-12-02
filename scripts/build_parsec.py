#!/usr/bin/env python

import os
import argparse
import subprocess
import shutil
from util import pcall, Timer
import util

def main():
	parser = argparse.ArgumentParser(description="Builds a parsec benchmark with contech")
	parser.add_argument("benchmark", help="The parsec bencmark to build.")
	parser.add_argument("--bldconf", default="contech", help="PARSEC build configuration (bldconf) to use for building.")
	parser.add_argument("--hammerOptLevel", help="Set hammer optimization level (bldconf==hammer only)")
	args = parser.parse_args()
	
	if os.environ.has_key("CONTECH_HOME"):
		CONTECH_HOME = os.environ["CONTECH_HOME"]
		stateFile = os.path.join(CONTECH_HOME, "scripts/output/", args.benchmark + ".stateFile.temp")
		os.environ["CONTECH_STATE_FILE"] = stateFile
	else:
		print ">Error: Could not find contech installation. Set CONTECH_HOME to the root of your contech directory."
		exit(1)

	if os.environ.has_key("PARSEC_HOME"):
		PARSEC_HOME = os.environ["PARSEC_HOME"]
		PARSECMGMT = os.path.join(PARSEC_HOME, "bin/parsecmgmt")
	else:
		print ">Error: Could not find parsec installation. Set PARSEC_HOME to the root of your parsec directory."
		exit(1)


	# Run the parsec benchmark
	print ">Building " + args.benchmark
	
	# Prepare state file for run
	if os.path.exists(stateFile):
		os.remove(stateFile)
		
	# Hammer: Prepare hammer nail file 
	if args.bldconf == "hammer":
		os.environ["HAMMER_NAIL_FILE"] = os.path.join(CONTECH_HOME, "backend/Hammer/compilerData/{}.bin".format(args.benchmark))
		os.environ["HAMMER_OPT_LEVEL"] = args.hammerOptLevel
		
	pcall([PARSECMGMT, "-a", "uninstall", "-p", args.benchmark, "-c", args.bldconf])
	with Timer("Build"):
		returnCode = pcall([PARSECMGMT, "-a", "build", "-p", args.benchmark, "-c", args.bldconf], returnCode=True)
	
	# Clean up
	if os.path.exists(stateFile):
		os.remove(stateFile)
	elif args.bldconf in ["contech", "contech_marker", "hammer"]: 
		util.print_warning("Warning: no statefile was generated.")
	
	if returnCode != 0:
		util.print_error("Build failed")
		exit(1)
	
	
if __name__ == "__main__":
    main()
