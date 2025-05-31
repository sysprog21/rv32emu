function fail()
{
    echo "*** Fail"
    exit 1
}

O=build
RUN=$O/rv32emu

if [ ! -f $RUN ]; then
    echo "No build/rv32emu found!"
    exit 1
fi
