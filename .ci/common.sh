# Expect host is Linux/x86_64
check_platform()
{
    MACHINE_TYPE=`uname -m`
    if [ ${MACHINE_TYPE} != 'x86_64' ]; then
        exit
    fi

    OS_TYPE=`uname -s`
    if [ ${OS_TYPE} != 'Linux' ]; then
        exit
    fi
}
