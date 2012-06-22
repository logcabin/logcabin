import sys
import os

opts = Variables('Local.sc')

opts.AddVariables(
    ("CC", "C Compiler"),
    ("CPPPATH", "The list of directories that the C preprocessor "
                "will search for include directories", []),
    ("CXX", "C++ Compiler"),
    ("CXXFLAGS", "Options that are passed to the C++ compiler", []),
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

# This ensures that the LIBPATH variable will be set,
# which some of the SConscript files rely on.
env.SetDefault(LIBPATH = [])

env.Append(CPPFLAGS = [ "-Wall", "-Wformat=2", "-Wextra", "-Wwrite-strings",
                        "-Wno-unused-parameter",
                        "-Wmissing-format-attribute" ])
env.Append(CFLAGS = [ "-Wmissing-prototypes", "-Wmissing-declarations",
                      "-Wshadow", "-Wbad-function-cast", "-Werror" ])
env.Prepend(CXXFLAGS = [ "-Wno-non-template-friend", "-Woverloaded-virtual",
                         "-Wcast-qual", "-Wcast-align", "-Wconversion",
                         "-Weffc++", "-std=c++0x", "-Werror" ])

if env["BUILDTYPE"] == "DEBUG":
    env.Append(CPPFLAGS = [ "-g", "-DDEBUG" ])
elif env["BUILDTYPE"] == "RELEASE":
    env.Append(CPPFLAGS = "-DNDEBUG")
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
    env.Append(LIBPATH = [env["LIBEVENT2PATH"]])

env.Append(CPPPATH = '#')

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
SConscript('test/SConscript', variant_dir='build/test')
SConscript('Client/SConscript', variant_dir='build/Client')
SConscript('Core/SConscript', variant_dir='build/Core')
SConscript('Event/SConscript', variant_dir='build/Event')
SConscript('Examples/SConscript', variant_dir='build/Examples')
SConscript('Protocol/SConscript', variant_dir='build/Protocol')
SConscript('RPC/SConscript', variant_dir='build/RPC')
SConscript('Server/SConscript', variant_dir='build/Server')
SConscript('Storage/SConscript', variant_dir='build/Storage')

# This function is taken from http://www.scons.org/wiki/PhonyTargets
def PhonyTargets(env = None, **kw):
    if not env: env = DefaultEnvironment()
    for target,action in kw.items():
        env.AlwaysBuild(env.Alias(target, [], action))

PhonyTargets(check = "scripts/cpplint.py")
PhonyTargets(lint = "scripts/cpplint.py")
PhonyTargets(doc = "doxygen docs/Doxyfile")
PhonyTargets(docs = "doxygen docs/Doxyfile")

