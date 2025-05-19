"""
Utilities to run tests and clean environment afterwards. An example of use is provided in the
main function.
"""

from __future__ import print_function

import subprocess
import random
import time
import sys
import re

from localconfig import hosts
from common import Sandbox, sh

def run_shell_command(command):
    """
    Run a shell command and print a warning if it fails.
    """

    try:
        subprocess.check_call(command, shell=True)
    except subprocess.CalledProcessError as e:
        print("Warning: Command failed:", e)

class TestFramework(object):
    """
    Contains the functionality to run tests and clean up the environment afterwards.
    """
    
    def __init__(
        self, 
        snapshotMinLogSize = 67108864,
        snapshotRatio = 4,
        snapshotWatchdogMilliseconds = 10000,
        electionTimeoutMilliseconds = 500
    ):
        """
        ### General ###

        List of tuples (server_id, server_ip) for each server in the cluster, generated from
        common.hosts with 1-1 mapping.

        ### Snapshotting ###

        Each server takes a snapshot once the following conditions are met:
        log size > snapshotMinLogSize, AND
        log size > snapshotRatio * last snapshot size
    
        Size in bytes of smallest log to snapshot. Default: 64 MB.
    
        - snapshotMinLogSize = 67108864 (2^26)
        
        Maximum log size as multiple of last snapshot size until server should
        snapshot.
        
        - snapshotRatio = 4
        
        Snapshotting is done in a separate child process, and if there was a bug in
        LogCabin or its libraries, this child might be prone to deadlock (see
        https://github.com/logcabin/logcabin/issues/121). To detect this deadlock,
        the parent process includes a watchdog thread that makes sure the child
        writes something into the snapshot file during each interval; the length of
        the interval is given by this setting. If the interval elapses with no
        progress made, the child is killed, and another one is started shortly
        thereafter. A value of 0 disables this functionality altogether.

        - snapshotWatchdogMilliseconds = 10000

        ### Raft ###

        The number of milliseconds that a follower waits without hearing from a
        current leader or granting its vote, before it becomes a candidate and starts
        a new election. Until we understand how Raft would behave, it's strongly
        recommended that you use the same election timeout setting on every server.

        - electionTimeoutMilliseconds = 500

        A leader sends RPCs at least this often, even if there is no data to
        send. Default and sane value: electionTimeoutMilliseconds / 2.

        - heartbeatPeriodMilliseconds = 250

        A candidate or leader waits this long after an RPC fails before sending
        another one, so as to not overwhelm the network with retries.
        Default value: electionTimeoutMilliseconds / 2.

        - rpcFailureBackoffMilliseconds = 250

        If true and compiled with BUILDTYPE=DEBUG mode, runs through some additional
        checks inside the Raft module. These are very costly, especially if you have
        a large number of entries.

        - raftDebug = no
        """

        self.server_ids_ips = [(server_id, server_ip) for server_ip, _, server_id in hosts]

        alphabet = [chr(ord('a') + i) for i in range(26)]
        self.cluster_uuid = ''.join([random.choice(alphabet) for i in range(8)]) 
        
        self.snapshotInfos = {
            "snapshotMinLogSize" : snapshotMinLogSize,
            "snapshotRatio" : snapshotRatio,
            "snapshotWatchdogMilliseconds" : snapshotWatchdogMilliseconds,
            "electionTimeoutMilliseconds" : electionTimeoutMilliseconds
        }

        self.filename = None

        self.sandbox = Sandbox()
        self.client_commands = 0

        # The running processes of the servers in the cluster. If a server is killed, the process
        # is removed from the dictionary. Otherwise, the process is kept in the dictionary from the
        # start of the server until the end of the test.
        self.server_processes = {}
    
    def _print_attr(self):
        print("server_ids_ips: ", self.server_ids_ips)
        print("cluster_uuid: ", self.cluster_uuid)
        print("snapshotInfos: ", self.snapshotInfos)
        print("filename: ", self.filename)
        print("sandbox: ", self.sandbox)
        print("client_commands: ", self.client_commands)
        print("server_processes: ", self.server_processes)

    def _print_string(self, string):
        """
        Print string with dashes below it.
        """
        str_len = len(string)

        print(string)
        print('-' * str_len)

    def create_configs(self, filename="logcabin"):
        """ 
        Create configuration files for each server. 
        """

        self.filename = filename

        # Write snapshotting configuration to smoketest.conf that is appended to each server's
        # configuration file.
        with open("smoketest.conf", 'w') as f:
            for key, value in self.snapshotInfos.items():
                f.write("%s = %d\n" % (key, value))

        # Write the configuration files for each server.
        for server_id, server_ip in self.server_ids_ips:
            with open('%s-%d.conf' % (self.filename, server_id), 'w') as f:
                f.write('serverId = %d\n' % server_id)
                f.write('listenAddresses = %s\n' % server_ip)
                f.write('clusterUUID = %s\n' % self.cluster_uuid)
                f.write('\n\n')
                try:
                    f.write(open('smoketest.conf').read())
                except:
                    pass

    def create_folders(self):
        """ 
        Create necessary folders for the metadata of the test.
        """

        run_shell_command('mkdir -p debug')

    def _initialize_first_server(self, server_command):
        """ 
        Bootstrap the first server in the cluster. The bootrstap server is the first server in
        the cluster, so it acts as its leader until a new configuration is set.
        """

        self._print_string('\nInitializing first server\'s log')
        
        server_id, server_ip = self.server_ids_ips[0]
        command = ('%s --bootstrap --config %s-%d.conf' %
                    (server_command, self.filename, server_id))

        self._print_string('Executing: %s on %s' % (command, server_ip))

        self.sandbox.rsh(
            server_ip,
            command,
            stderr=open('debug/bootstrap_server', 'w')
        ) 

    def _start_server(self, server_command, server_id_ip):
        """ 
        Start a server in the background process.
        """

        server_id, server_ip = server_id_ip
        command = ('%s --config %s-%d.conf' %
                    (server_command, self.filename, server_id))

        self._print_string('Executing: %s on %s' % (command, server_ip))

        self.server_processes[server_id_ip] = self.sandbox.rsh(
            server_ip,
            command,
            bg=True,
            stderr=open('debug/server_%d' % server_id, 'w')
        )
        self.sandbox.checkFailures()
        
    def _kill_server(self, server_id_ip):
        """ 
        Kill a server in the cluster.
        """

        self._print_string('Killing server %d at %s' % (server_id_ip[0], server_id_ip[1]))

        server_process = self.server_processes[server_id_ip]

        del self.server_processes[server_id_ip]
        self.sandbox.kill(server_process)
    
    def _start_servers(self, server_command):
        """
        Starts the servers in the cluster in background processes. 
        """

        self._print_string('\nStarting servers')

        for server_id_ip in self.server_ids_ips:
            self._start_server(server_command, server_id_ip)
    
    def _reconfigure_cluster(self, reconf_opts):
        """
        Execute the reconfigure command to grow the cluster. 
        """

        self._print_string('\nGrowing cluster')

        sh('build/Examples/Reconfigure %s %s set %s' %
           (
               "--cluster=%s" % ','.join([server_ip for _, server_ip in self.server_ids_ips]),
               reconf_opts,
               ' '.join([server_ip for _, server_ip in self.server_ids_ips])
            )
        )
    
    def initialize_cluster(self, server_command="build/LogCabin", reconf_opts=""):
        """ 
        Initialize the cluster by bootstrapping the first server, starting the servers in the
        cluster and reconfiguring the cluster to contain all servers. 
        """

        try:
            self._initialize_first_server(server_command)
            self._start_servers(server_command)
            self._reconfigure_cluster(reconf_opts)
        except:
            self.cleanup()
    
    def _client_command_to_cluster(
        self, 
        client_executable, 
        conf
    ):
        options = conf["options"]
        command = conf["command"]

        cluster = "--cluster=%s" % ','.join([server_ip for _, server_ip in self.server_ids_ips])
        options_formatted = "%s %s" % (options, cluster)
        client_command = "%s %s %s" % (client_executable, options_formatted, command)

        return client_command

    def _client_command_to_server(
        self, 
        client_executable, 
        conf
    ):
        options = conf["options"]
        command = conf["command"]
        server_ip = conf["server_ip"]

        server = "--server=%s" % (server_ip)
        options_formatted = "%s %s" % (options, server)
        client_command = "%s %s %s" % (client_executable, options_formatted, command)

        return client_command

    def execute_client_command(
        self, 
        client_executable, 
        conf = {
            "options": "",
            "command": "",
            "server_ip": "localhost"
        },
        onCluster=True,
        bg=False
    ):
        """ 
        Executes a client command by providing the client executable, options and command. The
        client command can be executed on the cluster (onCluser=True) or a single server of it 
        (onCluster=False). Also, the command can be executed in the background. The latter is 
        useful for time_client_command. 

        - For cluster commands the conf dictionary has the following keys:
            - options: options for the client command
            - command: the command to execute
        - For server commands the conf dictionary has the following keys
            - options: options for the client command
            - command: the command to execute
            - server_ip: the server to connect to
        """

        if onCluster:
            client_command = self._client_command_to_cluster(client_executable, conf)
        else:
            client_command = self._client_command_to_server(client_executable, conf)

        self._print_string('\nStarting %s on localhost' % (client_command))

        try:
            self.client_commands += 1

            return self.sandbox.rsh(
                'localhost',
                '%s' % (client_command),
                bg=bg,
                stderr=open('debug/client_command_%d' % self.client_commands, 'w')
            )
        except Exception as e:
            print("Client command error: ", e)
            self.cleanup()
    
    def time_client_command(self, client_process, timeout_sec=10):
        """ 
        Time the execution of a client command. If the command takes longer that the timeout, an
        exception is raised.
        """

        start = time.time()

        while client_process.proc.returncode is None:
            self.sandbox.checkFailures()

            time.sleep(.1)

            if time.time() - start > timeout_sec:
                raise Exception('Warning: timeout exceeded!')
    
    def cleanup(self, debug=False):
        """
        Clean up the environment: configuration files, debug files and storage folders. Also, 
        release the resources of the sandbox, concerning the remote processes.
        """

        self._print_string('\nCleaning up')

        # Generated from TestFramework.create_config
        run_shell_command('rm "smoketest.conf"')
        run_shell_command('rm "%s-"*".conf"' % self.filename)
        if not debug:
            run_shell_command('rm -f debug/*')

        # Generated from LogCabin
        run_shell_command('rm -rf "Storage/server"*"/"')
        run_shell_command('rm -rf "Server/server"*"/"')

        # Release Ssandbox resources
        self.sandbox.__exit__(None, None, None)

        
if __name__ == '__main__':
    test = TestFramework()
    test._print_attr()

    test.create_configs()
    test.create_folders()

    test.initialize_cluster()

    test.execute_client_command("build/Examples/TreeOps mkdir /dir1")
    test.execute_client_command("build/Examples/TreeOps mkdir /dir2")
    test.execute_client_command("build/Examples/TreeOps write /dir2/file1")
    test.execute_client_command("build/Examples/TreeOps dump")

    test.cleanup()
    