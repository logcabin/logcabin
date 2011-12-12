import sys
import os

opts = Variables('Local.sc')

opts.AddVariables(
    ("CC", "C Compiler"),
    ("CXX", "C++ Compiler"),
    ("AS", "Assembler"),
    ("LINK", "Linker"),
    ("BUILDTYPE", "Build type (RELEASE or DEBUG)", "RELEASE"),
    ("VERBOSE", "Show full build information (0 or 1)", "0"),
)

env = Environment(options = opts, tools = ['default'], ENV = os.environ)
Help(opts.GenerateHelpText(env))

env.Append(CXXFLAGS = [ "-Wall", "-Wformat=2", "-Wextra", "-Wwrite-strings",
                        "-Wno-unused-parameter", "-Wmissing-format-attribute" ])
env.Append(CFLAGS = [ "-Wmissing-prototypes", "-Wmissing-declarations",
                      "-Wshadow", "-Wbad-function-cast" ])
env.Append(CPPFLAGS = [ "-Wno-non-template-friend", "-Woverloaded-virtual",
                        "-Wcast-qual", "-Wcast-align", "-Wconversion",
                        "-std=c++0x" ])

if env["BUILDTYPE"] == "DEBUG":
    env.Append(CXXFLAGS = [ "-g", "-DDEBUG" ])
elif env["BUILDTYPE"] == "RELEASE":
    env.Append(CXXFLAGS = "-DNDEBUG")
else:
    print "Error BUILDTYPE must be RELEASE or DEBUG"
    sys.exit(-1)

if env["VERBOSE"] == "0":
    env["CXXCOMSTR"] = "Compiling $SOURCE"
    env["ARCOMSTR"] = "Creating library $TARGET"
    env["LINKCOMSTR"] = "Linking $TARGET"

Export('env')
SConscript('libDLogClient/SConscript', variant_dir='build/libDLogClient')
SConscript('libDLogRPC/SConscript', variant_dir='build/libDLogRPC')
SConscript('dlogd/SConscript', variant_dir='build/dlogd')

# This function is taken from http://www.scons.org/wiki/PhonyTargets
def PhonyTargets(env = None, **kw):
    if not env: env = DefaultEnvironment()
    for target,action in kw.items():
        env.AlwaysBuild(env.Alias(target, [], action))

PhonyTargets(check = "./cpplint.py")
PhonyTargets(lint = "./cpplint.py")
PhonyTargets(doc = "doxygen")
PhonyTargets(docs = "doxygen")

env.StaticObject("build/gtest",
                 ["gtest/src/gtest-all.cc"],
                 CPPPATH = [ "#gtest", "#gtest/include" ],
                 CXXFLAGS = [ "-g", "-DDEBUG" ],
                 CPPFLAGS = "-std=c++0x")
env.StaticLibrary("build/gtest",
                  ["build/gtest.o"])
env.Program("build/test",
            [
              "TestRunner.cc",
              "RefTest.cc",
            ],
            LIBPATH = [ "#build" ],
            LIBS = [ "gtest", "pthread" ],
            CPPPATH = ["#include", "#gtest/include"],
            CXXFLAGS = [ "-g", "-DDEBUG" ],
            CPPFLAGS = "-std=c++0x")
