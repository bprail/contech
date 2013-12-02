#!/usr/bin/env python

import os
import sys
import re
import json

def main():
    print json.dumps(processAll(sys.argv[1:]))

def processAll(filenames):
    runs = []
    for file in filenames:
        m = re.match("(\w+)_(\w+)_(\w+)_([\w.]+).o\d{5}", file)
        root = dict()
        if m:
            root["config"] = m.group(1)
            root["input"] = m.group(2)
            root["nThreads"] = m.group(3)
            root["name"] = m.group(4)
        runs.append(process(file, root))
    return runs

def process(filename, root):

    with open(filename) as file:
        for line in file:

            # Remove color codes
            line = re.sub("\033\[[0-9;]+m", "", line)
            
            # Parse stats output 
            if "stats" in root:
                if ":" in line:
                    key, value = tuple(line.split(":"))
                    root["stats"][key.strip()] = value.strip()
                elif "stats finished in" in line: # End of output, next file
                    break
            else:    
    
                # Search for timestamps
                m = re.search(r"CT_(\w+): (\d+[.]\d+)", line)
                if m:
                    if "timestamps" not in root: root["timestamps"] = dict()
                    root["timestamps"][m.group(1)] = m.group(2)
                        
                # Search for uncompressed bytes written
                m = re.search(r"Total Uncomp Written: (\d+)", line)
                if m:
                    root["uncompressedBytes"] = int(m.group(1))
                    
                # Search for execution time
                if '{"real"' in line:
                    root["executionTime"] = json.loads(line) 
                        
                # Search for middle layer time
                m = re.search(r"Middle layer finished in (\d+[.]\d+)", line)
                if m:
                    root["middleTime"] = m.group(1) 
                        
                # Beginning of stats output
                if "Statistics for" in line:
                    root["stats"] = dict()

        # Compute duration
        if "timestamps" in root:
            t = root["timestamps"]
            if "END" in t and "START" in t and "COMP" in t:
                t["RUNTIME"] = float(t["END"]) - float(t["START"])
                t["FLUSHTIME"] = float(t["COMP"]) - float(t["END"])
                t["TOTAL"] = float(t["COMP"]) - float(t["START"])
             
    return root
            
if __name__ == "__main__":
    main()
