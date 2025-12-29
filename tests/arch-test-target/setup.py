import constants
import argparse
import ruamel.yaml
import os


# setup the ISA config file
def setup_testlist(riscv_device, hw_data_misaligned_support, work_dir=None):
    # ISA config file path - use work_dir if provided for parallel execution
    if work_dir:
        os.makedirs(work_dir, exist_ok=True)
        ispec = os.path.join(work_dir, "rv32emu_isa.yaml")
    else:
        ispec = constants.root + "/rv32emu/rv32emu_isa.yaml"

    # Read from the template in source tree
    ispec_template = constants.root + "/rv32emu/rv32emu_isa.yaml"

    misa = 0x40000000
    ISA = "RV32"

    if not riscv_device:
        raise AssertionError("There is not any ISA.")

    if "E" in riscv_device:
        misa |= constants.misa_E
        ISA += "E"
    else:
        misa |= constants.misa_I
        ISA += "I"
    if "M" in riscv_device:
        misa |= constants.misa_M
        ISA += "M"
    if "A" in riscv_device:
        misa |= constants.misa_A
        ISA += "A"
    if "F" in riscv_device:
        misa |= constants.misa_F
        ISA += "F"
    if "C" in riscv_device:
        misa |= constants.misa_C
        ISA += "C"
    if "Zba" in riscv_device:
        ISA += "_Zba" if "Z" in ISA else "Zba"
    if "Zbb" in riscv_device:
        ISA += "_Zbb" if "Z" in ISA else "Zbb"
    if "Zbc" in riscv_device:
        ISA += "_Zbc" if "Z" in ISA else "Zbc"
    if "Zbs" in riscv_device:
        ISA += "_Zbs" if "Z" in ISA else "Zbs"
    if "Zicsr" in riscv_device:
        ISA += "_Zicsr" if "Z" in ISA else "Zicsr"
    if "Zifencei" in riscv_device:
        ISA += "_Zifencei" if "Z" in ISA else "Zifencei"

    with open(ispec_template, "r") as file:
        try:
            file = ruamel.yaml.YAML(typ="safe", pure=True).load(file)
        except ruamel.yaml.YAMLError as msg:
            print(msg)
            raise SystemExit(1)

    file["hart0"]["ISA"] = ISA
    file["hart0"]["hw_data_misaligned_support"] = (
        True if hw_data_misaligned_support == "1" else False
    )
    file["hart0"]["misa"]["reset-val"] = misa

    with open(ispec, "w+") as outfile:
        ruamel.yaml.YAML(typ="unsafe", pure=True).dump(file, outfile)
        outfile.flush()
        os.fsync(outfile.fileno())

    return ispec


# setup the riscof config file
def setup_config(work_dir=None, ispec_path=None):
    cwd = constants.cwd
    root = constants.root
    refname = constants.refname
    dutname = constants.dutname

    # Use work_dir if provided for parallel execution
    if work_dir:
        config_path = os.path.join(work_dir, "config.ini")
    else:
        config_path = root + "/config.ini"

    # path always points to where rv32emu binary is located (cwd/build)
    # This is used by riscof_rv32emu.py to find the executable
    dut_path = cwd + "/build"

    # create config file with device-specific ISA spec path
    # Use explicit fsync to ensure file is written before riscof reads it
    if ispec_path and work_dir:
        # Use custom config template with explicit ispec path and isolated DUT path
        config_content = constants.config_temp_with_ispec.format(
            refname,
            root + "/" + refname,
            dutname,
            root + "/" + dutname,
            cwd,
            ispec_path,
            dut_path,
        )
    else:
        config_content = constants.config_temp.format(
            refname, root + "/" + refname, dutname, root + "/" + dutname, cwd
        )
    with open(config_path, "w") as configfile:
        configfile.write(config_content)
        configfile.flush()
        os.fsync(configfile.fileno())

    # Verify the config file was written correctly
    if not os.path.exists(config_path):
        raise RuntimeError(f"Config file not created: {config_path}")

    with open(config_path, "r") as verify_file:
        content = verify_file.read()
        if "[RISCOF]" not in content:
            raise RuntimeError(
                f"Config file missing [RISCOF] section after write.\n"
                f"Path: {config_path}\n"
                f"Content ({len(content)} bytes):\n{content[:500]}"
            )

    return config_path


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--riscv_device", help="the ISA will test", default="IMACZicsrZifencei"
    )
    parser.add_argument(
        "--hw_data_misaligned_support",
        help="whether the hardware data misalgnment is implemented or not",
        default="1",
    )
    parser.add_argument(
        "--work_dir",
        help="work directory for device-specific config files (enables parallel execution)",
        default=None,
    )
    args = parser.parse_args()

    ispec_path = setup_testlist(
        args.riscv_device, args.hw_data_misaligned_support, args.work_dir
    )
    setup_config(args.work_dir, ispec_path if args.work_dir else None)
