#!/usr/bin/env python
import os
import sys
import util
import glob
import json
import argparse
import scrape_build
import scrape_run
import aggregate_scrape_run

def main(args):

    input = "qscrape"

    root = scrape_run.processAll(args[1:])
    
    # Aggregate output
    table = aggregate_scrape_run.aggregate(root)
    aggregate_scrape_run.computeSlowdown(table)
    aggregate_scrape_run.generateRunCsv(table, "results-{}.csv".format(input))

        
if __name__ == "__main__":
    main(sys.argv)