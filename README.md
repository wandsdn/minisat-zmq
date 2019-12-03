A zeromq interface to the [Minisat2](http://minisat.se/) SAT solver.
Through this interface, you can push incremental clauses to Minisat and enumerate all possible solutions.

### Minimal API Example (Python)
Our solver accepts clauses in the [DIMACS CNF](https://people.sc.fsu.edu/~jburkardt/data/cnf/cnf.html) format. For a solution, minisat-zmq the variables which are true returns, this differs from DIMACS which additionally returns false variables negated.

Here is an example in python which interfaces with the minisat-zmq.
Launch minisat-zmq as following:
```
./minisat-zmq -verb=2 -ipc-name=my_zmq
```
Then in a python interpreter
```
import zmq

ctx = zmq.Context()
socket = ctx.socket(zmq.REQ)
socket.bind("ipc:///tmp/my_zmq")
socket.send_multipart([b"r", b"-1 2 -3"])  # Register a DIMACS clause (1 or 2 or not 3)
assert socket.recv_multipart() == [b"OK"]
socket.send_multipart([b"r", "1"])  # Register another clause (... not 3) and (1)
assert socket.recv_multipart() == [b"OK"]
socket.send_multipart([b"s"])  # Ask for a solution
print(socket.recv_multipart())  # Prints ['1'] (The list of true variables which satisfy the equation)
socket.send_multipart([b"s"])
print(socket.recv_multipart())  # Prints ['1 2'], another possible solution
socket.send_multipart([b"s"])
print(socket.recv_multipart())  # Prints ['1 2 3']
socket.send_multipart([b"s"])
print(socket.recv_multipart())  # Prints ['UNSAT'], no more solutions
socket.send_multipart([b"f"])  # Finished, close the server
assert socket.recv_multipart() == [b"OK"]
```

Below are the other types of messages which minisat-zmq accepts
```
o: (O)utput the full problem in the dimacs format
d: Add clauses incrementally from a (d)imacs file
r: Add a clause incrementally [(r)eceive]
f: (F)inish and exit
s: (S)olve and return a string of true variables
p: Register (p)lacement variables which used to generate the result.
   These are disallowed on the next iteration.
c: Register variables which you (c)are about. I.e. 's' only returns these
```

### Docker Standalone Build

Rather than having to build multiple dependencies, we provide a script which uses an Alpine Linux docker to build a standalone executable to: ```./build/standalone/bin/minisat_zmq```. 

Alternatively, you can download the standalone release directly from the GitHub releases.
```
~ ./build_standalone_docker.sh
...
Standalone binary saved as ./build/standalone/bin/minisat-zmq
Returning to a shell within the docker
Type exit to exit
~ exit
Exiting ...
~ build/standalone/bin/minisat-zmq --help
USAGE: ./build/standalone/bin/minisat_zmq [options] -ipc-name=<name>

...
```

### Full Install

Download the required dependencies for Debian.
```
apt install minisat2 libzmq3-dev
```
Download and build [zmqpp](https://github.com/zeromq/zmqpp)
```
git clone https://github.com/zeromq/zmqpp
cd zmqpp
make
make install
cd ..
```
To build minisat-zmq using default settings:
```
make
make install
```
#### Useful configuration options
To override the default path to minisat2:
```
~ make config MINISAT_INCLUDE=-I<minisat2 include>
~ make config MINISAT_LIB="-L<minisat2 lib> -lminisat"
```
To override the default install location:
```
~ make config prefix=$NEW_PREFIX
```

### License
This code is based on the example code in [minisat-examples](https://github.com/niklasso/minisat-examples) and retains that original LICENSE. See the included LICENSE file.
