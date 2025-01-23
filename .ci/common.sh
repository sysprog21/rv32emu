# Expect host is Linux/x86_64
check_platform()
{
    MACHINE_TYPE=`uname -m`
    OS_TYPE=`uname -s`

    case "${MACHINE_TYPE}/${OS_TYPE}" in
        x86_64/Linux | aarch64/Linux)
            ;;
        Arm64/Darwin)
            echo "Apple Silicon is not supported yet"
            exit 1
            ;;
        *)
            echo "Unsupported platform: ${MACHINE_TYPE}/${OS_TYPE}"
            exit 1
            ;;
    esac

}
