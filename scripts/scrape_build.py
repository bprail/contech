#!/usr/bin/env python

import os
import sys
import re
import json
import fileinput

def main():
    print json.dumps(processAll(sys.argv[1:]))

def processAll(filenames):
    root = []
    for file in filenames:
        root.extend(process(file))
    return root
                 
def process(filename):
    results = []
    with open(filename) as file:
        for line in file:
            
            # Remove color codes
            line = re.sub("\033\[[0-9;]+m", "", line)
            
            m = re.search(r"(\w+)-(\w+) finished in (\d+[.]\d+) seconds", line)
            if m:
                row = dict()
                row["name"] = m.group(1)
                config = m.group(2)
                time = m.group(3)
                row[config + "BuildTime"] = time
                results.append(row)
            m = re.search(r"(\w+)-(\w+) failed", line)
            if m:
                row = dict()
                row["name"] = m.group(1)
                config = m.group(2)
                row[config + "BuildTime"] = "FAILED"
                results.append(row)
                 
    return results

    
    
if __name__ == "__main__":
    main()
