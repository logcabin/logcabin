import os

opts = Variables('Local.sc')

opts.AddVariables(
    ("CC", "C Compiler"),
    ("CXX", "C++ Compiler"),
    ("AS", "Assembler"),
    ("LINK", "Linker"),
)

env = Environment(options = opts, tools = ['default'], ENV = os.environ)
Help(opts.GenerateHelpText(env))

SConscript('libDLogClient/SConstruct', variant_dir='build/libDLogClient')
SConscript('libDLogRPC/SConstruct', variant_dir='build/libDLogRPC')
SConscript('dlogd/SConstruct', variant_dir='build/dlogd')

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
                 CPPFLAGS = [ "-g", "-std=c++0x" ])
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
            CPPFLAGS = [ "-g", "-std=c++0x" ])
