import subprocess
import os
import sys
import stat

# List of all parsec benchmarks 
class Benchmarks:
    # List of all parsec benchmarks
    parsecApps = ["blackscholes", "bodytrack", "ferret", "fluidanimate", "freqmine", "swaptions", "x264"] # Blacklist: facesim, raytrace, vips
    parsecKernels = ["canneal", "dedup", "streamcluster"]
    # List of all splash2 benchmarks
    splash2Apps = ["raytrace", "water_nsquared", "barnes", "fmm", "ocean_cp", "ocean_ncp", "radiosity", "volrend", "water_spatial"]
    splash2Kernels = ["radix", "cholesky", "fft", "lu_cb", "lu_ncb"]
    # Splash 2 benchmarks need to be explicitly named
    splash2Apps = map(lambda b: "splash2x." + b, splash2Apps) 
    splash2Kernels = map(lambda b: "splash2x." + b, splash2Kernels) 
    parsec = parsecApps + parsecKernels
    splash2 = splash2Apps + splash2Kernels
    all = parsec + splash2
    #nas = ["bt", "cg", "dc", "ep", "ft", "is", "lu", "mg", "sp"] #, "ua" <- does not terminate
    nas = ["mg", "cg", "ep", "ft", "is"]
    rodinia = ["bfs", "btree", "hotspot", "lavaMD", "leukocyte", "lud", "nn", "nw", "particlefilter", "pathfinder", "srad"]
    headers = ["name",     "llvmBuildTime",        "llvmTime", "llvmParallel",  "contechBuildTime",        "contechTime",     "contechFlushTime", "contechSlowdown",     "middleTime",      "llvmMemoryUsage",      "contechMemoryUsage",      "uncompressedBytes",      "compressedBytes", "Total Tasks", "Average Basic Blocks per Task", "Average MemOps per Basic Block"]

def print_header(text):
    print '\033[92m' + text + '\033[0m'
def print_warning(text):
    print '\033[93m' + text + '\033[0m'
def print_error(text):
    print '\033[91m' + text + '\033[0m'
def print_progress(text):
    print_progress.spinCount = print_progress.spinCount + 1 if print_progress.spinCount < 5 else 1
    sys.stdout.write("\r" + '\033[93m' + text + "."*print_progress.spinCount + " "*(5-print_progress.spinCount) + '\033[0m')
    sys.stdout.flush()
print_progress.spinCount = 1

def pcall(args, silent=False, suppressOutput=False, returnCode=False, outputFile=None):
    # Prepare and print the command
    command = ""
    for arg in args:
        command = command + " " + str(arg)
    if not silent:
        print command
    
    try:
        # Open a process to run the command
        if (outputFile is None):
            p = subprocess.Popen(command, stdout = subprocess.PIPE, stderr = subprocess.STDOUT, shell = True)
        else:
            p = subprocess.Popen(command, stdout = outputFile, stderr = subprocess.STDOUT, shell = True)
        o, e = p.communicate()
        # Print the output
        if o != None:
            if not suppressOutput:
                print o
        
        if returnCode:
            return p.returncode
        elif p.returncode != 0:
            # Stop the script if return code is bad
            exit(p.returncode)
        else:    
            return o
        
    except:
        print_error("Fatal error in pcall")
        exit(1)
        
    
import time

class Timer:
    name = "Unknown"
       
    def __init__(self, name):
        self.name = name
        
    def __enter__(self):
        self.start = time.time()
        return self

    def __exit__(self, *args):
        self.end = time.time()
        self.interval = self.end - self.start
          
        print "{0} finished in {1:.2f} seconds".format(self.name, self.interval)

def findContechInstall():
    # Find contech installation
    if os.environ.has_key("CONTECH_HOME"):
        return os.environ["CONTECH_HOME"]
    else:
        print_error("Error: Could not find contech installation. Set CONTECH_HOME to the root of your contech directory.")
        exit(1)

def findParsecInstall():
    # Find parsec installation
    if os.environ.has_key("PARSEC_HOME"):
        return os.environ["PARSEC_HOME"]
    else:
        print_error("Error: Could not find parsec installation. Set PARSEC_HOME to the root of your contech directory.")
        exit(1)

# Submits a script file to the job queue. Returns the job id that was assigned by the scheduler.
def quicksub(name, code, resources = [], queue = "batch"):
    # Write code to a temporary script file
    scriptFileName = "/tmp/util_py_temp_{0}.sh".format(name) 
    with open(scriptFileName, "w") as scriptFile:
        # Set job parameters
        scriptFile.write("#PBS -N {0}\n".format(name))
        for resource in resources:
            scriptFile.write("#PBS -l {0}\n".format(resource))
        # Write the script to file
        if os.environ.has_key("SHELL"):
            shell = os.environ["SHELL"]
            scriptFile.write(""" setenv TIME '{"real":%e, "user":%U, "sys":%S, "mem":%M }'\n""")
        else:
            shell = "/bin/bash"
            scriptFile.write("""TIME='{"real":%e, "user":%U, "sys":%S, "mem":%M }'\n""")
        scriptFile.write(code)
    
    # Prepare and print the command
    
    command = "qsub -j oe -S {} -q {} {}".format(shell, queue, scriptFileName)
    print command
    # Open a process to run the command
    p = subprocess.Popen(command, stdout = subprocess.PIPE, stderr = subprocess.STDOUT, shell = True)
    o, e = p.communicate()
    
    # Stop the script if return code is bad
    if p.returncode != 0:
        print_error("Quicksub crashed!")
        print o
        print e
        exit(p.returncode)
    
    # Don't submit jobs too fast
    time.sleep(0.5)
    # Clean up
    os.remove(scriptFileName)  
    # Return the job ID
    return o.split(".")[0]

# Submits a script file to the job queue. Returns the job id that was assigned by the scheduler.
def quicklocal(name, code, resources = [], queue = "batch"):
    # Write code to a temporary script file
    scriptFileName = "/tmp/util_py_temp_{0}.sh".format(name)
    shell = ""
    with open(scriptFileName, "w") as scriptFile:
        # Write the script to file
        if os.environ.has_key("SHELL"):
            shell = os.environ["SHELL"]
            scriptFile.write("#!{}\n".format(shell))
            if shell == "/bin/csh":
                scriptFile.write(""" setenv TIME '{"real":%e, "user":%U, "sys":%S, "mem":%M }'\n""")
            else:
                scriptFile.write("""TIME='{"real":%e, "user":%U, "sys":%S, "mem":%M }'\n""")
        else:
            shell = "/bin/bash"
            scriptFile.write("#!{}\n".format(shell))
            scriptFile.write("""TIME='{"real":%e, "user":%U, "sys":%S, "mem":%M }'\n""")
        scriptFile.write(code)
    
    # Prepare and print the command
    
    command = "{}".format(scriptFileName)
    os.chmod(scriptFileName, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
    print command
    outputFileName = "{}.o00000".format(name)
    with open(outputFileName, "w") as outputFile:
        # Open a process to run the command
        p = subprocess.Popen(command, stdout = outputFile, stderr = subprocess.STDOUT, shell = True)
        p.wait()
    #o, e = p.communicate()
    
    # Don't submit jobs too fast
    time.sleep(0.5)
    # Clean up
    os.remove(scriptFileName)  
    # Return the job ID
    return outputFileName
    
import xml.etree.ElementTree as ET

def quickstat():
    p = subprocess.Popen("qstat -x", stdout = subprocess.PIPE, stderr = subprocess.STDOUT, shell = True)
    o, e = p.communicate()
    root = ET.fromstring(o)
    jobs = []
    for j in root:
        job = dict()
        job["id"] = j.find("Job_Id").text.split(".")[0]
        job["name"] = j.find("Job_Name").text
        job["state"] = j.find("job_state").text
        jobs.append(job)
    return jobs

# Can add other fields as needed, uncomment to print them all
#         for d in j:
#             print d.tag, d.text
    
from datetime import datetime    
    
def waitForJobs(jobIdList):
    done = False
    while not done:
        time.sleep(5)

        waitingFor = 0
        runningJobIdList = [job["id"] for job in quickstat() if job["state"] != "C"]
        for jobId in jobIdList:
            if jobId in runningJobIdList:
                waitingFor += 1
                
        if waitingFor == 0:
            done = True
        else:
            print_progress("Waiting for {} jobs to finish".format(waitingFor))
            
    print
    print_header("Jobs completed")        
    
import glob        
def getFileNameForJob(jobId):
    files = glob.glob("*.o{}".format(jobId))
    if len(files) == 1:
        return files[0]
    else:
        raise IOError("Output file for job {} not found".format(jobId))
                      
           
     
