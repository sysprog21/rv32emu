import os

dutname = "rv32emu"
refname = "sail_cSim"

root = os.path.abspath(os.path.dirname(__file__))
cwd = os.getcwd()

misa_A = 1 << 0
misa_C = 1 << 2
misa_E = 1 << 4
misa_F = 1 << 5
misa_I = 1 << 8
misa_M = 1 << 12

config_temp = """[RISCOF]
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
jobs=3
timeout=600

[{0}]
pluginpath={1}
path={1}
jobs=3
timeout=900
"""

# Template with explicit ispec path and isolated DUT path for parallel execution
# {5} = ispec_path, {6} = dut_path (device-specific work directory)
config_temp_with_ispec = """[RISCOF]
ReferencePlugin={0}
ReferencePluginPath={1}
DUTPlugin={2}
DUTPluginPath={3}

[{2}]
pluginpath={3}
ispec={5}
pspec={3}/{2}_platform.yaml
path={6}
target_run=1
jobs=3
timeout=600

[{0}]
pluginpath={1}
path={1}
jobs=3
timeout=900
"""
