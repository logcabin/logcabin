import sys
import os

opts = Variables('Local.sc')

opts.AddVariables(
    ("CC", "C Compiler"),
    ("CXX", "C++ Compiler"),
    ("AS", "Assembler"),
    ("LINK", "Linker"),
    ("LIBEVENT2PATH", "libevent-2.0 library path (if necessary).", ""),
    ("BUILDTYPE", "Build type (RELEASE or DEBUG)", "RELEASE"),
    ("VERBOSE", "Show full build information (0 or 1)", "0"),
    ("NUMCPUS", "Number of CPUs to use for build (0 means auto).", "0"),
)

env = Environment(options = opts,
                  tools = ['default', 'protoc'],
                  ENV = os.environ)
Help(opts.GenerateHelpText(env))

env.Append(CPPFLAGS = [ "-Wall", "-Wformat=2", "-Wextra", "-Wwrite-strings",
                        "-Wno-unused-parameter", "-Wmissing-format-attribute" ])
env.Append(CFLAGS = [ "-Wmissing-prototypes", "-Wmissing-declarations",
                      "-Wshadow", "-Wbad-function-cast" ])
env.Append(CXXFLAGS = [ "-Wno-non-template-friend", "-Woverloaded-virtual",
                        "-Wcast-qual", "-Wcast-align", "-Wconversion",
                        "-Weffc++", "-std=c++0x" ])

if env["BUILDTYPE"] == "DEBUG":
    env.Append(CXXFLAGS = [ "-g", "-DDEBUG" ])
elif env["BUILDTYPE"] == "RELEASE":
    env.Append(CXXFLAGS = "-DNDEBUG")
else:
    print "Error BUILDTYPE must be RELEASE or DEBUG"
    sys.exit(-1)

if env["VERBOSE"] == "0":
    env["CCCOMSTR"] = "Compiling $SOURCE"
    env["CXXCOMSTR"] = "Compiling $SOURCE"
    env["SHCCCOMSTR"] = "Compiling $SOURCE"
    env["SHCXXCOMSTR"] = "Compiling $SOURCE"
    env["ARCOMSTR"] = "Creating library $TARGET"
    env["LINKCOMSTR"] = "Linking $TARGET"

if env["LIBEVENT2PATH"] != "":
    env.Append(LIBPATH = env["LIBEVENT2PATH"])

def GetNumCPUs():
    if env["NUMCPUS"] != "0":
        return int(env["NUMCPUS"])
    if os.sysconf_names.has_key("SC_NPROCESSORS_ONLN"):
        cpus = os.sysconf("SC_NPROCESSORS_ONLN")
        if isinstance(cpus, int) and cpus > 0:
            return 2*cpus
        else:
            return 2
    return 2*int(os.popen2("sysctl -n hw.ncpu")[1].read())

env.SetOption('num_jobs', GetNumCPUs())

Export('env')
SConscript('libDLogClient/SConscript', variant_dir='build/libDLogClient')
SConscript('libDLogRPC/SConscript', variant_dir='build/libDLogRPC')
SConscript('libDLogStorage/SConscript', variant_dir='build/libDLogStorage')
SConscript('dlogd/SConscript', variant_dir='build/dlogd')
SConscript('dlogperf/SConscript', variant_dir='build/dlogperf')
SConscript('proto/SConscript', variant_dir='build/proto')
SConscript('test/SConscript', variant_dir='build/test')

# This function is taken from http://www.scons.org/wiki/PhonyTargets
def PhonyTargets(env = None, **kw):
    if not env: env = DefaultEnvironment()
    for target,action in kw.items():
        env.AlwaysBuild(env.Alias(target, [], action))

PhonyTargets(check = "scripts/cpplint.py")
PhonyTargets(lint = "scripts/cpplint.py")
PhonyTargets(doc = "doxygen docs/Doxyfile")
PhonyTargets(docs = "doxygen docs/Doxyfile")

