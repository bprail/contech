
#include "llvm/Support/CommandLine.h"


//VCA
#include "DynamicAnalysis.h"


static cl::opt<bool> PrintVolatile("interpreter-print-volatile", cl::Hidden,
          cl::desc("make the interpreter print every volatile load and store"));


//===----------------------------------------------------------------------===//
//                     VCA: Command line options for application analysis
//
// Command line documentation:http://llvm.org/docs/CommandLine.html
//===----------------------------------------------------------------------===//
static cl::opt <bool> PerTaskCommFactor("per-task-comm-factor", cl::desc("Use per task comm factors. Default FALSE"), cl::init(false));

static cl::opt<float> CommFactorRead("comm-factor-read", cl::desc("Communication probability factor to scale cache read latency, default 0"), cl::init(0)); 

static cl::opt<float> CommFactorWrite("comm-factor-write", cl::desc("Communication probability factor to scale cache write latency, default 0"), cl::init(0)); 

static cl::opt<uint32_t> ContextNumber("context-number", cl::desc("Context # to be analyzed, default 0"), cl::init(0));

static cl::opt <unsigned> MemoryWordSize("memory-word-size", cl::desc("Specify the size in bytes of a data item. Default value is 8 (double precision)"),cl::init(8));

static cl::opt <unsigned> CacheLineSize("cache-line-size", cl::desc("Specify the cache line size (B). Default value is 64 B"),cl::init(64));

static cl::opt <unsigned> L1CacheSize("l1-cache-size", cl::desc("Specify the size of the L1 cache (in bytes). Default value is 32 KB"),cl::init(32768));

static cl::opt <unsigned> L2CacheSize("l2-cache-size", cl::desc("Specify the size of the L2 cache (in bytes). Default value is 256 KB"),cl::init(262144));

static cl::opt <unsigned> LLCCacheSize("llc-cache-size", cl::desc("Specify the size of the L3 cache (in bytes). Default value is 20 MB"),cl::init(20971520));


static cl::opt<string> Microarchitecture("uarch", cl::desc("Name of the microarchitecture"),
                                      cl::init("SB"));

static cl::list <float> ExecutionUnitsLatency("execution-units-latency",  cl::CommaSeparated, cl::desc("Specify the execution latency of the nodes(cycles). Default value is 1 cycle"));


static cl::list <float> ExecutionUnitsThroughput("execution-units-throughput",  cl::CommaSeparated, cl::desc("Specify the execution bandwidth of the nodes(ops executed/cycles). Default value is -1 cycle"));

static cl::list <int> ExecutionUnitsParallelIssue("execution-units-parallel-issue",  cl::CommaSeparated, cl::desc("Specify the number of nodes that can be executed in parallel based on ports execution. Default value is -1 cycle"));

static cl::list<unsigned>  MemAccessGranularity("mem-access-granularity", cl::CommaSeparated, cl::desc("Specify the memory access granularity for the different levels of the memory hierarchy (bytes). Default value is memory word size"));

static cl::opt <int> IFB("instruction-fetch-bandwidth", cl::desc("Specify the size of the reorder buffer. Default value is infinity"),cl::init(-1));

static cl::opt <unsigned> ReservationStation("reservation-station-size", cl::desc("Specify the size of a centralized reservation station. Default value is infinity"),cl::init(0));

static cl::opt <unsigned> ReorderBuffer("reorder-buffer-size", cl::desc("Specify the size of the reorder buffer. Default value is infinity"),cl::init(0));

static cl::opt <unsigned> LoadBuffer("load-buffer-size", cl::desc("Specify the size of the load buffer. Default value is infinity"),cl::init(0));

static cl::opt <unsigned> StoreBuffer("store-buffer-size", cl::desc("Specify the size of the store buffer. Default value is infinity"),cl::init(0));

static cl::opt <unsigned> LineFillBuffer("line-fill-buffer-size", cl::desc("Specify the size of the fill line buffer. Default value is infinity"),cl::init(0));

static cl::opt < bool > ReportOnlyPerformance("report-only-performance", cl::Hidden, cl::desc("Reports only performance (op count and span)"),cl::init(false));

static cl::opt <string> TaskGraphFileName("taskgraph-file", cl::desc("File with Contech Task Graph"), cl::value_desc("filename"));
