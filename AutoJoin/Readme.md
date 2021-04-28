

# Prereqs avahi-daemon, dbus

1. Enable all services that are not currently running.
```bash
# Check if either service is running
sudo service dbus status
sudo service avahi-daemon status

# If either service is offline or not running, run the following command(s)
sudo service dbus start
sudo service avai-daemon start
```

2. Install Avahi library/files
```bash
# Clone the avahi libraries to the desired location/user space
cd ~ && git clone git@github.com:lathiat/avahi.git
```

2. Compile the cluster management application
```bash
# Compile and link the 2 programs
gcc manage.cpp -l~/avahi

gcc zeroconf/browse-services.c -o manage.out -L~/avahi/ -lavahi-client -lavahi-common

```

3. From 2 different terminals with all of the above already completed run the following
host
```bash
# arg 1 is the name of the service to broadcast the
# arg 2 is the mode to start the socket with
# arg 3 is the communication type to use ipv4/6
sudo ./manage.out host host 4
```

client
```bash
# arg 1 is the name of the service to look for
# arg 2 is the mode to start the socket with
# arg 3 is the communication type to use ipv4/6
sudo ./manage.out host auto 4
```