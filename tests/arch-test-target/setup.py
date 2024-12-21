import constants
import argparse
import ruamel.yaml

# setup the ISA config file
def setup_testlist(riscv_device):
    # ISA config file path
    ispec = constants.root + '/rv32emu/rv32emu_isa.yaml'
    misa = 0x40000100
    ISA = 'RV32I'

    if not riscv_device:
        raise AssertionError('There is not any ISA.')

    if 'M' in riscv_device:
        misa |= constants.misa_M
        ISA += 'M'
    if 'A' in riscv_device:
        misa |= constants.misa_A
        ISA += 'A'
    if 'F' in riscv_device:
        misa |= constants.misa_F
        ISA += 'F'
    if 'C' in riscv_device:
        misa |= constants.misa_C
        ISA += 'C'
    if 'Zba' in riscv_device:
        ISA += '_Zba' if 'Z' in ISA else 'Zba'
    if 'Zbb' in riscv_device:
        ISA += '_Zbb' if 'Z' in ISA else 'Zbb'
    if 'Zbc' in riscv_device:
        ISA += '_Zbc' if 'Z' in ISA else 'Zbc'
    if 'Zbs' in riscv_device:
        ISA += '_Zbs' if 'Z' in ISA else 'Zbs'
    if 'Zicsr' in riscv_device:
        ISA += '_Zicsr' if 'Z' in ISA else 'Zicsr'
    if 'Zifencei' in riscv_device:
        ISA += '_Zifencei' if 'Z' in ISA else 'Zifencei'
    
    with open(ispec, 'r') as file:
        try:
            file = ruamel.yaml.YAML(typ='safe', pure=True).load(file)
        except ruamel.yaml.YAMLError as msg:
            print(msg)
            raise SystemExit(1)

    file['hart0']['ISA'] = ISA
    file['hart0']['misa']['reset-val'] = misa

    with open(ispec, 'w+') as outfile:
        ruamel.yaml.YAML(typ='unsafe', pure=True).dump(file, outfile)

# setup the riscof config file
def setup_config():
    cwd = constants.cwd
    root = constants.root
    refname = constants.refname
    dutname = constants.dutname

    # create config file
    configfile = open(root + '/config.ini','w')
    configfile.write(constants.config_temp.format(refname, \
            root+'/'+refname, dutname, root+'/'+dutname, cwd))
    configfile.close()

if __name__=="__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--riscv_device', help='the ISA will test',
                        default='IMACZicsrZifencei')
    args = parser.parse_args()

    setup_testlist(args.riscv_device)
    setup_config()
