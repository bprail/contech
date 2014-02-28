#!/usr/bin/env python
import os
import util
import glob
import json


def main():
    launchBackend("parhist")

def launchBackend(backend):
    CONTECH_HOME = util.findContechInstall()
    script = """
    cd /net/tinker/brailing/contech/scripts
    
    ./run_parsec.py --cached {0} -n 16 -i simmedium --backend {1}
"""
    for b in util.Benchmarks.all:
        util.quicksub(name="{}_{}".format(b, backend), code=script.format(b, backend), resources=["nodes=1:ppn=20"]) 
    
def buildLocal(benchmark):
    CONTECH_HOME = util.findContechInstall()
    os.chdir(os.path.join(CONTECH_HOME, "scripts"))
    try:
        util.pcall(["./build_parsec.py {0}".format(benchmark)])
    except:
        pass

def compilationTimeCompare(benchmarks):
    CONTECH_HOME = util.findContechInstall()
    script = """
    cd $CONTECH_HOME/scripts
    
"""
#     extract = """grep "Build finished in" | sed s/'Build finished in'/{{\"{0}\":/ | sed s/seconds/}}/"""
    label = "sed s/'Build'/'{0}'/ "
    
    for benchmark in benchmarks:
        test = """
    # {0}
    ./build_parsec.py {0} --bldconf llvm | {1}   
    ./build_parsec.py {0} --bldconf contech | {2}
    
"""
        script += test.format(benchmark, label.format(benchmark+"-llvm"), label.format(benchmark+"-contech"))
    
#     print script
    return util.quicksub(name=("timed_compilation"), code=script)

def build(benchmark, bldconf="contech"):
    
    CONTECH_HOME = util.findContechInstall()
    script = """
    cd $CONTECH_HOME/scripts
    
    ./build_parsec.py {0} --bldconf {1}
"""
    script = script.format(benchmark, bldconf)
    return util.quicksub(name=("build_" + benchmark), code=script)
    
def mark(benchmark):
    
    CONTECH_HOME = util.findContechInstall()
    script = """
    cd $CONTECH_HOME/scripts
    
    ./mark_parsec.py {0}
"""
    script = script.format(benchmark)
    return util.quicksub(name=("mark_" + benchmark), code=script)

def nativeRun(benchmark, n, input):
      
    PARSEC_HOME = util.findParsecInstall()
    script = """
    $PARSEC_HOME/bin/parsecmgmt -a run -p {0} -c llvm -n {1} -i {2} -s "/usr/bin/time"
"""
    script = script.format(benchmark, n, input)
    jobName = "llvm_{}_{}_{}".format(input,  n, benchmark)
    print jobName
    return util.quicksub(name=jobName, code=script, resources=["nodes=1:ppn=24"], queue="newpasta")
    
def statsRun(benchmark, n, input, option):
       
    CONTECH_HOME = util.findContechInstall()
    script = """
    cd $CONTECH_HOME/scripts
    
    ./run_parsec.py {0} -n {1} -i {2} {3} --backends stats
    rm -f --verbose /tmp/{0}.contech.trace
    rm -f --verbose /tmp/{0}.taskgraph;
"""
    options = {"discard": "--discardTrace",
               "pin" : "--pinFrontend",
               "contech" :  "",
               "contechmarker" : ""}
    
    script = script.format(benchmark, n, input, options[option])
    jobName = "{}_{}_{}_{}".format(option, input,  n, benchmark)
    print jobName
    return util.quicksub(name=jobName, code=script, resources=["nodes=1:ppn=24"], queue="newpasta")
    
    
if __name__ == "__main__":
    main()
