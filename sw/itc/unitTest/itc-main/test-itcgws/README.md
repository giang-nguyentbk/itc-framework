```bash
# Setup two WSL2 distros to simulate different hosts: Ubuntu and Debian, for example.
# On first host (here Debian WSL2 as a receiver side):
$ make cla
$ make
$ make runr

# On second host (Ubuntu WSL2 as a sender side):
$ make cla
$ make
$ make runs

# After test done, kill itcgws first and then itccoord then (for both hosts).
$ make killa

# Verify if everything is cleaned up and properly finished
## No /tmp/itc directory exists
$ ls -a /tmp/itc
## No processes for itcgws and itccoord
$ ps -xj
## No listening TCP connection left
$ netstat -a | grep itc
## Look at the last logging session in itcgws.log and itccoord.log to see if a normal finalization was taken.
$ vi itcgws.log
$ vi itccoord.log

```