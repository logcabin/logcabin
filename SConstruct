import os

opts = Variables('Local.sc')

opts.AddVariables(
    ("CC", "C Compiler"),
    ("AS", "Assembler"),
    ("LINK", "Linker"),
)

env = Environment(options = opts, tools = ['default'], ENV = os.environ)
Help(opts.GenerateHelpText(env))

SConscript('libDLogClient/SConstruct', variant_dir='build/libDLogClient')
SConscript('libDLogRPC/SConstruct', variant_dir='build/libDLogRPC')
SConscript('dlogd/SConstruct', variant_dir='build/dlogd')

