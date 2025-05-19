"""
This tests the snapshotting capabilities and performance of LogCabin.

Usage:
  snapshotting.py [--client=<cmd>]... [options]
  snapshotting.py (-h | --help)

Options:
  -h --help                         Show this help message and exit
  --binary=<cmd>                    Server binary to execute [default: build/LogCabin]
  --reconf=<opts>                   Additional options to pass through to the Reconfigure
                                    binary. [default: '']
"""

import re
import time

from docopt import docopt
from TestFramework import TestFramework, run_shell_command

class SnapshotTest(TestFramework):
    # Experiment metadata
    experiment_number = 0
    stats = {}

    # Path to the csv file for the plot
    csv_file = "scripts/plot/csv/snapshotting.csv"
    plot_file = "scripts/plot/plot_snapshotting.py"

    def __init__(
        self, 
        snapshotMinLogSize = 67108864,
        snapshotRatio = 4,
        snapshotWatchdogMilliseconds = 10000
    ):
        TestFramework.__init__(
            self,
            snapshotMinLogSize,
            snapshotRatio,
            snapshotWatchdogMilliseconds,
        )

        # Test stats
        SnapshotTest.experiment_number += 1
        SnapshotTest.stats[SnapshotTest.experiment_number] = {}

    def allowSnapshotting(self):
        SnapshotTest.stats[SnapshotTest.experiment_number]["snapshotting"] = 1

        for _, server_ip in self.server_ids_ips:
            self.execute_client_command(
                client_executable = "build/Client/ServerControl",
                conf = {
                    "options": "--timeout=10",
                    "command": "snapshot inhibit clear",
                    "server_ip": server_ip,
                },
                onCluster = False,
            )

    def disallowSnapshotting(self):
        SnapshotTest.stats[SnapshotTest.experiment_number]["snapshotting"] = 0

        for _, server_ip in self.server_ids_ips:
            self.execute_client_command(
                client_executable = "build/Client/ServerControl",
                conf = {
                    "options": "--timeout=10",
                    "command": "snapshot inhibit set",
                    "server_ip": server_ip,
                },
                onCluster = False,
            )

    def executeBenchmark(self, size, writes, run):
        SnapshotTest.stats[SnapshotTest.experiment_number]["size"] = size
        SnapshotTest.stats[SnapshotTest.experiment_number]["writes"] = writes
        SnapshotTest.stats[SnapshotTest.experiment_number]["run"] = run

        start_time = time.time()

        self.execute_client_command(
            client_executable = "build/Examples/Benchmark",
            conf = {
                "options": "--size=%s --writes=%s" % (size, writes),
                "command": "",
            }
        )

        end_time = time.time()

        SnapshotTest.stats[SnapshotTest.experiment_number]["time"] = end_time - start_time

    def dumpStats(self):
        for _, server_ip in self.server_ids_ips:
            self.execute_client_command(
                client_executable = "build/Client/ServerControl",
                conf = {
                    "options": "--timeout=10",
                    "command": "stats dump",
                    "server_ip": server_ip,
                },
                onCluster = False,
            )

    def printStats(self):
        for server_id, _ in self.server_ids_ips:
            self._print_string("\nServer %d stats" % server_id)

            for line in open('debug/server_%d' % server_id):
                m = re.search('num_snapshots_attempted: (\d+)', line)
                if m is not None:
                    print "Snapshots attempted: %s" % m.group(1)

                m = re.search('num_snapshots_failed: (\d+)', line)
                if m is not None:
                    print "Snapshots failed: %s" % m.group(1)
                
                m = re.search('num_write_attempted: (\d+)', line)
                if m is not None:
                    print "Write attempted: %s" % m.group(1)

                m = re.search('num_write_success: (\d+)', line)
                if m is not None:
                    print "Write succeded: %s" % m.group(1)
    
def run_test(
    server_command,
    reconf_opts,
    snapshotting,
    size,
    writes,
    run
):
    snapshotTest = SnapshotTest(
        snapshotMinLogSize = 1024,
        snapshotRatio = 4,
        snapshotWatchdogMilliseconds = 1000
    )

    snapshotTest._print_attr()

    snapshotTest.create_configs()
    snapshotTest.create_folders()

    snapshotTest.initialize_cluster(server_command, reconf_opts)

    if snapshotting:
        snapshotTest.allowSnapshotting()
    else:
        snapshotTest.disallowSnapshotting()

    snapshotTest.executeBenchmark(size, writes, run)

    snapshotTest.dumpStats()
    snapshotTest.printStats()

    snapshotTest.cleanup()

def write_csv(
    file,
    stats,
):
    """
    Write experiment metadata to a csv file. The stats must have the following form:
    {
        experiment_number_1 : {metric_1: value_1, metric_2: value_2, ...},
        experiment_number_2 : {metric_1: value_1, metric_2: value_2, ...},
        . . .
        experiment_number_n : {metric_1: value_1, metric_2: value_2, ...},
    }
    """
    with open(file, 'w') as f:

        for i, (_, value) in enumerate(SnapshotTest.stats.items()):
            # Write columns
            if i == 0:
                for key in value.keys():
                    f.write('%s;' % key)
            
                f.write('\n')

            # Write experiment metadata
            for _, value in value.items():
                f.write('%s;' % value)

            f.write('\n')

def plot_stats(
    file
):
    run_shell_command('python3 %s' % file)

def execute_experiment(
    server_command,
    reconf_opts,
    size_array,
    writes_array,
    runs=5
):

    for run in range(runs):
        print("\n\n=============================================")
        print("Run %d" % run)
        print("=============================================\n\n")
        for size in size_array:
            for writes in writes_array:
                for snapshotting in [True, False]:
                    print("\n\n=============================================")
                    print("size: %d, writes: %d, snapshotting: %s" % (size, writes, snapshotting))
                    print("=============================================\n\n")

                    # Run the test
                    run_test(
                        server_command,
                        reconf_opts,
                        snapshotting,
                        size,
                        writes,
                        run
                    )
    
    write_csv(
        file = SnapshotTest.csv_file,
        stats = SnapshotTest.stats,
    )

    plot_stats(
        file = SnapshotTest.plot_file,
    )

def main():
    # Parse command line arguments
    arguments = docopt(__doc__)

    server_command = arguments['--binary']

    reconf_opts = arguments['--reconf']
    if reconf_opts == "''":
        reconf_opts = ""

    # Run the test
    size_array = [1024]
    writes_array = [20, 80, 150, 200]

    execute_experiment(
        server_command = server_command,
        reconf_opts = reconf_opts,
        size_array = size_array,
        writes_array = writes_array,
    )

if __name__ == "__main__":
    main()