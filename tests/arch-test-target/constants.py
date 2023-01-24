import os

dutname = 'rv32emu'
refname = 'sail_cSim'

root = os.path.abspath(os.path.dirname(__file__))
cwd = os.getcwd()

misa_C = (1 << 2)
misa_F = (1 << 5)
misa_M = (1 << 12)

config_temp = '''[RISCOF]
ReferencePlugin={0}
ReferencePluginPath={1}
DUTPlugin={2}
DUTPluginPath={3}

[{2}]
pluginpath={3}
ispec={3}/{2}_isa.yaml
pspec={3}/{2}_platform.yaml
path={4}/build
target_run=1

[{0}]
pluginpath={1}
path={1}
'''
