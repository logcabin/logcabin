"""
Script to configure election timeout between servers so that it respects the inherent
network properties of the system.
"""

from __future__ import print_function
import re

from TestFramework import TestFramework, run_shell_command

class TimeoutConfiguration(TestFramework):
    def __init__(self):
        super(TimeoutConfiguration, self).__init__()
        self.icmp_header = 8
        self.ip_pattern = r'\b(?:\d{1,3}\.){3}\d{1,3}\b'
        self.ping_separator = r'~*'
        self.time_pattern = r'time=(\d+\.\d+) ms'
        # History of ping sample RTTs (Round Trip Times)
        self.ping_sample_rtts = []
        # History of Exponential Weighted Moving Average (EWMA) parameters
        self.estimations = {
            "average": [],
            "deviation": []
        }
        self.alpha = 0.125
        self.beta = 0.25
        # Window for timeout calculation
        self.timeout_window = 4
        # Path to the csv file for the plot
        self.csv_file = "scripts/plot/csv/timeout_stats.csv"
        self.plot_file = "scripts/plot/plot_timeouts.py"

    def ping_servers(
        self,
        number_of_pings=2,
        number_of_bytes=1024,
    ):
        """
        Ping all servers in the cluster from all servers in the cluster. The ping command is configured to exchange number_of_bytes data, whereas the number_of_pings pings are executed.

        Important: Before executing the pings the debug/ folder is cleared. The output of the ping commands is stored in files with the format: debug/client_command_<command_number>_out.
        """

        # Due to appending to the same files in debug/
        run_shell_command('rm -f debug/*')

        try:
            bytes_sent = number_of_bytes - self.icmp_header

            for from_server_id, from_server_ip in self.server_ids_ips:
                header = "\nPinging from server %s (%s)" % (from_server_id, from_server_ip)
                self._print_string(header)

                # Change stdout and stderr file whenever ssh-ing to different server
                self.client_commands += 1

                for _, to_server_ip in self.server_ids_ips:
                    # Skip pinging yourself
                    if from_server_ip == to_server_ip:
                        continue

                    command = "ping -c %s -s %s %s" % (
                        number_of_pings, number_of_bytes, to_server_ip
                    )

                    print(command)

                    self.sandbox.rsh(
                        '%s' % (from_server_ip),
                        command,
                        bg=False,
                        stdout=open('debug/client_command_%d_out' % self.client_commands, 'a'),
                        stderr=open('debug/client_command_%d' % self.client_commands, 'a')
                    )

                    with open('debug/client_command_%d_out' % self.client_commands, 'a') as f:
                        f.write('~' * 60 + '\n')
        except Exception as e:
            print("Client command error: ", e)
            self.cleanup()

    def _estimations_step(self, ping_sample_rtt):
        # Do not try to access previous entry when the lists are empty
        if (
            len(self.estimations["average"]) == 0 and
            len(self.estimations["deviation"]) == 0
        ):
            self.estimations["average"].append(ping_sample_rtt)
            self.estimations["deviation"].append(0)
            return

        # Estimate RTT
        self.estimations["average"].append(
            (1 - self.alpha) * self.estimations["average"][-1] +
            self.alpha * ping_sample_rtt
        )

        # Estimate Deviation
        self.estimations["deviation"].append(
            (1 - self.beta) * self.estimations["deviation"][-1] +
            self.beta * abs(ping_sample_rtt - self.estimations["average"][-1])
        )

    def parse_ping_stats(self):
        """
        Parse ping stats from ping output files. The ping output files are stored in the debug/
        folder with the format: debug/client_command_<command_number>_out.

        The history of ping rtts is stored in the ping_sample_rtts array.

        The EWMA parameters are incrementally updated for every sample rtt. The history of the
        EWMA parameters are stored in the estimations dictionary, in the form:
        {
            "average": [entry_1, entry_2, ...],
            "deviation": [entry_1, entry_2, ...]
        }
        """

        for i, from_server_id_ip in enumerate(self.server_ids_ips, 1):
            with open('debug/client_command_%d_out' % i, 'r') as f:
                for to_server_id_ip in self.server_ids_ips:
                    for line in f:
                        # Skip to the next ping command
                        m = re.search(self.ping_separator, line)
                        if len(m.group(0)) != 0:
                            break

                        # Extract the time from the ping command
                        m = re.search(self.time_pattern, line)
                        if  (m is not None and len(m.group(0)) != 0):
                            ping_sample_rtt = float(m.group(1))
                            # Ping Stats
                            self.ping_sample_rtts.append(ping_sample_rtt)
                            # estimations History
                            self._estimations_step(ping_sample_rtt)

    def caclulate_timeout(self):
        return (
            self.estimations["average"][-1] +
            self.timeout_window * self.estimations["deviation"][-1]
        )

    def plot_stats(self):
        # Generate time axis
        time_axis = []

        time_count = 0
        for ping_sample_rtt in self.ping_sample_rtts:
            time_count += ping_sample_rtt
            time_axis.append(time_count)

        # Write to csv file for plotting with python3 matplotlib
        with open('%s' % self.csv_file, 'w') as f:

            f.write('time;sample_rtt;estimated_rtt;deviation\n')

            for i in range(len(time_axis)):
                f.write('%s;' % time_axis[i])
                f.write('%s;' % self.ping_sample_rtts[i])
                f.write('%s;' % self.estimations["average"][i])
                f.write('%s\n' % self.estimations["deviation"][i])

        self._print_string("\nPlotting timeout stats")
        run_shell_command('python3 %s' % self.plot_file)

def main():
    test = TimeoutConfiguration()
    test._print_attr()

    test.create_configs()
    test.create_folders()

    test.initialize_cluster()

    test.ping_servers(
        number_of_pings=10,
        number_of_bytes=1024,
    )

    test.parse_ping_stats()
    timeout = test.caclulate_timeout()

    test._print_string("\nElection Timeout: %s ms" % timeout)

    test.plot_stats()

    test.cleanup(debug=True)

if __name__ == "__main__":
    main()
