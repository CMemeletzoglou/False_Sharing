# False_Sharing
A trivial benchmark to showcase Cache Line False Sharing in action, on x86 SMT/Hyperthreading CPUs (Linux Only !!).

## Usage
<exececutable_name> [OPTIONS]

### OPTIONS
`--nofalse-sharing` : Run without false sharing

`--rand-sibling-threads` : Autodetect sibling thread pairs and choose a random one. (sibling threads are threads that share the same physical core)

`--cpu-list <space> <core0>,<core1>` : Use virtual cores `core0` and `core1`
  
The default behavior is to run with False Sharing occuring between a random pair of sibling-threads.

For more information on how caches are shared between cores in your system, run the `lstopo` command for a visual representation of your processor's topology.

It it advised to examine the stats provided by `perf stat` , such as L1d Cache Accesses/Misses, etc.

Run using `perf stat -d <executable_name> [OPTIONS]` .

