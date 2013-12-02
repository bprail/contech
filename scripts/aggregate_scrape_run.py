#!/usr/bin/env python

import os
import sys
import re
import json
import fileinput
from collections import defaultdict
import operator
import util

DEBUG = True

def main():
    root = []
    for i in range(1, len(sys.argv)):
        with open(sys.argv[i]) as file:
            root.extend(json.load(file))
    table = aggregate(root)
    computeSlowdown(table)
#     generateCgoTables(table)
    generateCsv(table)




# Load regressResults.json and display
#     with open(sys.argv[1]) as file:
#         generateCgoTables(json.load(file))
#     

def aggregate(root):    
            
    rows = defaultdict(dict)
    failed = set()
    # Assume input is always the same
    for r in root:
        # Standardize names
        name = r["name"].replace("splash2x.", "")

        # Coalesce to one row per benchmark
        row = rows[name]
        row["name"] = name
        
        # This result came from a run
        if "input" in r:
            
            # Grab size of task graph
            try:
                row["compressedBytes"] = bytesFormat(os.path.getsize(os.path.join(os.environ["CONTECH_HOME"], "middle/output/", "{}_{}_{}.taskgraph".format(r["name"], r["nThreads"], r["input"]))))
            except OSError:
                row["compressedBytes"] = 0.0
        
            try:
                # time and flushTime
                config = r["config"]
                if config in ["llvm", "pin"]:
                    row[config+"Time"] = float(r["executionTime"]["real"])
                    row[config+"FlushTime"] = float(0.0)
                elif config in  ["contech"]:
                    row[config+"Time"] = float(r["timestamps"]["RUNTIME"])
                    row[config+"FlushTime"] = float(r["timestamps"]["FLUSHTIME"])
                row[config+"MemoryUsage"] = bytesFormat(r["executionTime"]["mem"] * 1024) # Memory usage is given in KB
                
                if config != "llvm":    
                    row["middleTime"] = float(r["middleTime"])
                    row["uncompressedBytes"] = bytesFormat(r["uncompressedBytes"])
                    for key in ["Total Tasks", "Average Basic Blocks per Task", "Average MemOps per Basic Block"]:
                        row[key] = float(r["stats"][key])
                
            except KeyError as e:
                failed.add(name)
                warn( "{}: missing {}".format(name, e))
                
        # This result came from a build
        else:
            for key in ["contechBuildTime", "llvmBuildTime"]:
                if key in r:
                    if r[key] == "FAILED":
                        failed.add(name) 
                    else:
                        row[key] = float(r[key])
    
    # Sort results (dict to list conversion)
    table = sorted(rows.values(), key=operator.itemgetter("name"))
    # Remove failed rows
    util.print_error("Ignoring failed experiments: " + reduce(lambda x,y: x+", "+y, failed)) 
    table = [a for a in table if a["name"] not in failed]
    
    return table

def computeSlowdown(table):   
    # Compute slowdown      
    for row in table:
        for config in ["contech", "pin"]:
            try:
                row[config+"Slowdown"] = float(row[config+"Time"]) / float(row["llvmTime"])
            except:
                row[config+"Slowdown"] = float("NaN")

def generateCgoTables(table):
    # Split into two tables
    tableParsec =  [a for a in table if a["name"] in util.Benchmarks.parsec]
    tableSplash2 = [a for a in table if "splash2x." + a["name"] in util.Benchmarks.splash2]
        
    for table in [tableParsec, tableSplash2]:
        generateRunTimeTable(table, format="LaTeX")
        generateTaskGraphTable(table, format="LaTeX")

def generateCsv(table, filename):
    header = re.sub(r" +", " ", "Benchmark, LLVM Build Time [s], LLVM Run Time [s], Contech Build Time [s], Contech Run Time [s], Contech Flush Time [s],          Slowdown,  Middle Time [s], LLVM Memory Usage [MB], Contech Memory Usage [MB],   Uncompressed Size [MB],  Compressed Bytes [MB],   Total Tasks,   Average Basic Blocks per Task, Average MemOps per Basic Block ")
    line =   re.sub(r" +", " ", "   {name},     {llvmBuildTime},        {llvmTime},     {contechBuildTime},        {contechTime},     {contechFlushTime}, {contechSlowdown},     {middleTime},      {llvmMemoryUsage},      {contechMemoryUsage},      {uncompressedBytes},      {compressedBytes}, {Total Tasks}, {Average Basic Blocks per Task}, {Average MemOps per Basic Block}")
    
    with open(filename, "w") as file:
        file.write(header + "\n")
        for row in table:
            try:
                file.write(line.format(**row) + "\n")
            except KeyError as e:
                warn("{}: missing {}".format(row["name"], e))
                
    print "Results written to " + filename

def generateRunTimeTable(table, format="LaTeX"):
    # Generate table of compile/run times in TeX format
    print
    print "RunTime Table"
    print
    for row in table:
        try:
            line = "{name}, {llvmBuildTime:.2f}, {llvmTime:.2f}, {contechBuildTime:.2f}, {contechTime:.2f}, {contechFlushTime:.2f}, {contechSlowdown:.2f}".format(**row)
            if format=="LaTeX":
                line = line.replace(", ", " & ") + " \\\\"
            print line
        except KeyError as e:
            warn("{}: missing {}".format(row["name"], e))
        except ValueError as e:
            warn("{}: ValueError for row: {}".format(row["name"], row))
    
def generateTaskGraphTable(table, format="LaTeX"):
    # Generate Table of middle layer stuff
    print
    print "Taskgraph Table"
    print
    for row in table:
        try:
            line = "{name}, {middleTime}, {uncompressedBytes}, {compressedBytes}, {Total Tasks:.0f}, {Average Basic Blocks per Task:.0f}, {Average MemOps per Basic Block:.2f}".format(**row)
            if format=="LaTeX":
                line = line.replace(", ", " & ") + " \\\\"
            print line
        except KeyError as e:
            warn("{}: missing {}".format(row["name"], e))
        except ValueError as e:
            warn("{}: ValueError for row: {}".format(row["name"], row))
        
def warn(msg):
    if DEBUG: util.print_warning(msg)

def bytesFormat(num):
    return "{}".format(num / (1024*1024))

#     for x in ['bytes','KB','MB','GB','TB']:
#         if num < 1024.0:
#             return "%3.1f %s" % (num, x)
#         num /= 1024.0
    
if __name__ == "__main__":
    main()
