/*****************************************************************************************[Main.cc]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007-2011, Niklas Sorensson
Copyright 2019 Richard Sanger, Wand Network Research Group

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

#include <algorithm>
#include <array>
#include <errno.h>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string>
#include <vector>

#include <zmqpp/zmqpp.hpp>

#include <minisat/core/Dimacs.h>
#include <minisat/simp/SimpSolver.h>
#include <minisat/utils/Options.h>
#include <minisat/utils/ParseUtils.h>
#include <minisat/utils/System.h>

#define VERSION "0.1"
using namespace Minisat;

//=================================================================================================

void printStats(Solver &solver) {
    double cpu_time = cpuTime();
    double mem_used = memUsedPeak();
    printf("restarts              : %" PRIu64 "\n", solver.starts);
    printf("conflicts             : %-12" PRIu64 "   (%.0f /sec)\n", solver.conflicts, solver.conflicts / cpu_time);
    printf("decisions             : %-12" PRIu64 "   (%4.2f %% random) (%.0f /sec)\n", solver.decisions,
           (float)solver.rnd_decisions * 100 / (float)solver.decisions, solver.decisions / cpu_time);
    printf("propagations          : %-12" PRIu64 "   (%.0f /sec)\n", solver.propagations,
           solver.propagations / cpu_time);
    printf("conflict literals     : %-12" PRIu64 "   (%4.2f %% deleted)\n", solver.tot_literals,
           (solver.max_literals - solver.tot_literals) * 100 / (double)solver.max_literals);
    if (mem_used != 0)
        printf("Memory used           : %.2f MB\n", mem_used);
    printf("CPU time              : %g s\n", cpu_time);
}

//=================================================================================================
// Main:

static Solver *solver;
// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int) {
    printf("\n");
    printf("*** INTERRUPTED ***\n");
    if (solver->verbosity > 0) {
        printStats(*solver);
        printf("\n");
        printf("*** INTERRUPTED ***\n");
    }
    _exit(1);
}

//=================================================================================================
// Main:

using namespace std;

template <class T> void add_clause(T &s, string &clause, string &full_dimacs) {
    // void add_clause(Solver& s, string &clause) {
    int var;
    stringstream ss(clause);
    vec<Lit> lits;

    full_dimacs += clause + " 0\n";

    // Loop all vars included
    while (ss >> var) {
        if (var == 0) { // Allow DIMACS style, with 0 terminator
            s.addClause(lits);
            lits.clear();
            continue;
        }
        // minisat is 0 indexed dimcas is 1
        int abs_var = abs(var) - 1;
        while (abs_var >= s.nVars())
            s.newVar();
        lits.push((var > 0) ? mkLit(abs_var) : ~mkLit(abs_var));
    }
    s.addClause(lits);
}

void collect_clause(string &clause, vector<vector<Lit>> &full_problem) {
    int var;
    stringstream ss(clause);
    vector<Lit> lits;

    // Loop all vars included
    while (ss >> var) {
        // minisat is 0 indexed dimcas is 1
        int abs_var = abs(var) - 1;
        lits.push_back((var > 0) ? mkLit(abs_var) : ~mkLit(abs_var));
    }
    full_problem.push_back(lits);
}

vector<Var> parse_vars(string &var_string) {
    // Parse a string of variable numbers in list of Var's
    int var;
    vector<Var> ret;
    stringstream ss(var_string);

    while (ss >> var)
        ret.push_back(var - 1);

    return ret;
}

void print_solution(Solver &solv) {
    cout << "SAT\n";
    for (int i = 0; i < solv.nVars(); i++)
        if (solv.model[i] != l_Undef)
            cout << ((i == 0) ? "" : " ") << ((solv.model[i] == l_True) ? "" : "-") << i + 1;
    cout << " 0\n";
}

/*
o: (O)utput the full problem in the dimacs format
d: Add clauses from a (d)imacs file (only incremental), now UNUSED
r: Add a clause incrementally [(r)eceive]
f: (F)inish and exit
s: (S)olve and return a string of true variables
p: Register (p)lacement variables which used to generate the result.
   These are disallowed on the next iteration.
c: Register variables which you (c)are about. I.e. 's' only returns these
*/

template <class T> void handle_requests(T &s, bool incr, const char *ipc_path, int verb) {
    string endpoint = "ipc:///tmp/";
    string full_dimacs;
    endpoint += ipc_path;
    zmqpp::context context;
    vector<vector<Lit>> full_problem;
    vector<Lit> blocking_clause;

    zmqpp::socket_type type = zmqpp::socket_type::rep;
    zmqpp::socket socket(context, type);

    if (verb)
        cout << "Connecting to " << endpoint << endl;
    socket.connect(endpoint);
    vector<Var> cares;
    vector<Var> places;

    while (1) {
        uint8_t type;
        zmqpp::message message_recv;
        zmqpp::message message_send;
        string a;
        gzFile in;
        T new_solver;
        int n_clauses;
        socket.receive(message_recv);

        message_recv >> type;
        switch (type) {
        case 'o':
            // Return in dimacs format
            n_clauses = count(full_dimacs.begin(), full_dimacs.end(), '\n');
            a += "p cnf ";
            a += to_string(s.nVars()); // Number of vars
            a += " ";
            a += to_string(n_clauses);
            a += "\n";
            a += full_dimacs;
            message_send.add(a);
            break;
        case 'd':
            if (incr) {
                message_recv >> a;
                if (verb)
                    cout << "Loading file: " << a;
                in = gzopen(a.c_str(), "rb");
                parse_DIMACS(in, s);
                gzclose(in);
                message_send.add("OK");
            } else {
                message_send.add("NOT_SUPPORTED");
            }
            break;
        case 'p':
            message_recv >> a;
            if (verb)
                cout << "Place vars are : " << a << endl;
            places = parse_vars(a);
            // What do we care about?
            message_send.add("OK");
            break;
        case 'c':
            message_recv >> a;
            if (verb)
                cout << "We care about : " << a << endl;
            cares = parse_vars(a);
            // What do we care about?
            message_send.add("OK");
            // s.eliminate(false);
            s.simplify();
            if (verb)
                cout << "Simplify" << endl;
            break;
        case 'r':
            // Sadly only new versions of zmqpp have remaining()
            // 1st part was the 'type'
            for (size_t i = 1; i < message_recv.parts(); i++) {
                message_recv >> a;
                if (incr) {
                    add_clause(s, a, full_dimacs);
                } else {
                    collect_clause(a, full_problem);
                }
            }
            message_send.add("OK");
            break;
        case 'f':
            if (verb)
                cout << "Closing, goodbye" << endl;
            message_send.add("OK");
            socket.send(message_send);
            return;
        case 's':
            T &solv = incr ? s : new_solver;
            if (!incr) {
                for (auto &a : full_problem) {
                    vec<Lit> vs;
                    for (auto &l : a) {
                        vs.push(l);
                        while (l.x / 2 >= solv.nVars())
                            solv.newVar();
                    }
                    solv.addClause_(vs);
                }
            }
            if (std::is_same<SimpSolver, T>::value) {
                SimpSolver &solv2 = static_cast<SimpSolver &>(solv);
                if (verb)
                    cout << "Eliminate" << endl;
                solv2.eliminate(true);
            } else {
                // solv.simplify();
                // if (verb)
                //    cout<<"Simplify"<<endl;
            }
            // Give me a solution
            vec<Lit> dummy;
            if (solv.solveLimited(dummy) == l_True) {
                // printStats(s);
                // Collect the result
                string out;

                if (cares.empty()) {
                    for (Var i = 0; i < solv.nVars(); i++) {
                        if (solv.modelValue(i) == l_True) {
                            if (!out.empty())
                                out += " ";
                            out += to_string(i + 1);
                        }
                    }
                } else {
                    for (Var i : cares) {
                        if (solv.modelValue(i) == l_True) {
                            if (!out.empty())
                                out += " ";
                            out += to_string(i + 1);
                        }
                        blocking_clause.push_back(mkLit(i, solv.modelValue(i) == l_True));
                    }
                }
                // Return the result
                message_send.add(out);
                if (verb >= 2)
                    print_solution(solv);

                // Add a clause to stop this combination
                // Collect only the placement clauses
                blocking_clause.clear();
                if (places.empty()) {
                    for (Var i = 0; i < solv.nVars(); i++) {
                        blocking_clause.push_back(mkLit(i, solv.modelValue(i) == l_True));
                    }
                } else {
                    for (Var i : places) {
                        blocking_clause.push_back(mkLit(i, solv.modelValue(i) == l_True));
                    }
                }
                // Add counter clause for next time
                if (incr) {
                    vec<Lit> a;
                    for (auto &l : blocking_clause) {
                        a.push(l);
                        full_dimacs += sign(l) ? "-" : "";
                        full_dimacs += to_string(var(l) + 1);
                        full_dimacs += " ";
                    }
                    full_dimacs += "0\n";
                    solv.addClause_(a);
                } else {
                    full_problem.push_back(blocking_clause);
                }
            } else {
                if (verb)
                    cout << "UNSAT" << endl;
                message_send.add("UNSAT");
            }
            break;
        }
        socket.send(message_send);
    }
}

int main(int argc, char **argv) {
    try {
        setUsageHelp("USAGE: %s [options] -ipc-name=<name>\nVERSION: " VERSION "\n");

        // Extra options:
        //
        IntOption verb("MAIN", "verb", "Verbosity level (0=silent, 1=some, 2=more).", 0, IntRange(0, 2));
        StringOption ipc_name("MAIN", "ipc-name", "zmq socket name ipc:/tmp/<ipc-path>", "dminisat");

        parseOptions(argc, argv, true);

        Solver S;
        solver = &S;
        // Use signal handlers that forcibly quit:
        signal(SIGINT, SIGINT_exit);
        signal(SIGXCPU, SIGINT_exit);

        handle_requests(S, true, ipc_name, verb);
        return 0;
    } catch (OutOfMemoryException &) {
        printf("===============================================================================\n");
        printf("INDETERMINATE\n");
        exit(0);
    }
}
