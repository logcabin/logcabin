#!/usr/bin/env python

"""
This runs ReconfigureTest that constantly changes the configuration to a random
subset of all the servers participating in the initial one. This action is performed
for a specified number of tries in an array.

Usage:
  reconfiguretest.py [options]
  reconfiguretest.py (-h | --help)

Options:
  -h --help            Show this help message and exit
  --binary=<cmd>       Server binary to execute [default: build/LogCabin]
  --reconf=<opts>      Additional options to pass through to the Reconfigure
                       binary. [default: '']
"""

import random
import time
from docopt import docopt
from common import sh

from TestFramework import TestFramework, run_shell_command

class ReconfigureTest(TestFramework):
    def __init__(self):
        TestFramework.__init__(self)
        # Infos from localconfig.py
        self.parent_server_ids_ips = self.server_ids_ips
        # Path to the csv file for the plot
        self.csv_file = "scripts/plot/csv/reconfigure.csv"
        self.plot_file = "scripts/plot/plot_reconfigure.py"
    
    def set_servers_num(
        self,
        servers_num,
        server_command
    ):
        # Number of servers in the cluster
        self.server_ids_ips = self.parent_server_ids_ips[:servers_num]

    def create_folders(self):
        TestFramework.create_folders(self)
        # Create the csv file for the plot
        with open("%s" % (self.csv_file), 'w') as f:
            # write the columns
            f.write('servers;time;tries;runs\n')

    def cleanup(self, debug=False):
        TestFramework.cleanup(self, debug=debug)
        if not debug:
            # Remove the csv file
            run_shell_command("rm %s" % (self.csv_file))

    def membership_changes(self, tries):
        self.execute_client_command(
            client_executable = "build/Examples/ReconfigureTest",
            conf = {
                "options": "--tries=%d" % (tries),
                "command": ""
            }
        )

    def _reconfigure_cluster(self, reconf_opts):
        """
        Execute the reconfigure command to grow the cluster.
        """

        self._print_string('\nGrowing cluster')

        # Important: The --cluster option must contain all the server (old and new)
        sh('build/Examples/Reconfigure %s %s set %s' %
            (
                "--cluster=%s" % ','.join(
                    [server_ip for _, server_ip in self.parent_server_ids_ips]),
                    reconf_opts,
                ' '.join([server_ip for _, server_ip in self.server_ids_ips])
            )
        )

    def reconfigure_test(self, tries, run):
        start_time = time.time()
        self.membership_changes(tries)
        end_time = time.time()

        with open("%s" % (self.csv_file), 'a') as f:
            f.write('%d;%f;%d;%d\n' % (
                len(self.server_ids_ips),
                end_time - start_time,
                tries,
                run)
            )

    def plot(self):
        self._print_string("\nPlotting reconfigure results")
        run_shell_command('python3 %s' % self.plot_file)

def run_test(
        server_command,
        reconf_opts,
        tries_range,
        debug = False,
        runs=5
    ):
    test = ReconfigureTest()

    test.create_configs()
    test.create_folders()

    test._initialize_first_server(server_command)
    test._start_servers(server_command)

    for run in range(runs):
        print("\n=======================================")
        print("run: %d" % run)
        print("=======================================")
        for tries in tries_range:
            print("\n=======================================")
            print("Running ReconfigureTest with tries: %d" % tries)
            print("=======================================")

            for servers_num in range(len(test.parent_server_ids_ips), 1, -1):
                print("\n=======================================")
                print("Running ReconfigureTest with servers: %d" % servers_num)
                print("=======================================")

                test.set_servers_num(servers_num, server_command)
                test._reconfigure_cluster(reconf_opts)
                test.reconfigure_test(tries, run)

    test.plot()
    test.cleanup(debug=debug)


def main():
    # Parse command line arguments
    arguments = docopt(__doc__)

    server_command = arguments['--binary']

    reconf_opts = arguments['--reconf']
    if reconf_opts == "''":
        reconf_opts = ""

    # Run the test
    run_test(
        server_command = server_command,
        reconf_opts = reconf_opts,
        tries_range = [10, 100, 250, 500],
        debug = True
    )

if __name__ == '__main__':
    main()
