#!/usr/bin/env bash

source tests/common.sh

# Set the number of runs for the Dhrystone benchmark
N_RUNS=10

function run_dhrystone()
{
    # Run Dhrystone and extract the DMIPS value
    output=$($RUN $O/dhrystone.elf 2>&1)
    local exit_code=$?
    [ $exit_code -ne 0 ] && fail
    dmips=$(echo "$output" | grep -oE '[0-9]+' | awk 'NR==5{print}')
    echo "$dmips"
}

# Run Dhrystone benchmark and collect DMIPS values
dmips_values=()
for ((i=1; i<=$N_RUNS; i++))
do
    echo "Running Dhrystone benchmark - Run #$i"
    dmips=$(run_dhrystone)
    exit_code=$?
    [ $exit_code -ne 0 ] && fail
    dmips_values+=("$dmips")
done

# Sort DMIPS values
sorted_dmips=($(printf "%s\n" "${dmips_values[@]}" | sort -n))

# Calculate Median Absolute Deviation (MAD)
num_dmips=${#sorted_dmips[@]}
median_index=$((num_dmips / 2))
if ((num_dmips % 2 == 0)); then
    median=$(echo "scale=2; (${sorted_dmips[median_index - 1]} + ${sorted_dmips[median_index]}) / 2" | bc -l)
else
    median=${sorted_dmips[median_index]}
fi

deviation=0
for dmips in "${sorted_dmips[@]}"; do
    if (( $(echo "$dmips > $median" | bc -l) )); then
        diff=$(echo "$dmips - $median" | bc -l)
    else
        diff=$(echo "$median - $dmips" | bc -l)
    fi
    deviation=$(echo "scale=2; $deviation + $diff" | bc -l)
done

mad=$(echo "scale=2; $deviation / $num_dmips" | bc -l)

# Filter outliers based on MAD
filtered_dmips=()
for dmips in "${sorted_dmips[@]}"
do
    if (( $(echo "$dmips > 0" | bc -l) )); then
        if (( $(echo "$dmips > $median" | bc -l) )); then
            diff=$(echo "$dmips - $median" | bc -l)
        else
            diff=$(echo "$median - $dmips" | bc -l)
        fi
        if (( $(echo "$diff <= $mad * 2" | bc -l) )); then
            filtered_dmips+=("$dmips")
        fi
    fi
done

#dhrystone benchmark output file
benchmark_output=dhrystone_output.json
# empty the file
echo -n "" > $benchmark_output

# Calculate average DMIPS excluding outliers
num_filtered=${#filtered_dmips[@]}
if ((num_filtered > 0)); then
    total_dmips=0
    for dmips in "${filtered_dmips[@]}"
    do
        total_dmips=$(echo "scale=2; $total_dmips + $dmips" | bc -l)
    done

    average_dmips=$(echo "scale=2; $total_dmips / $num_filtered" | bc -l)
    echo "--------------------------"
    echo "Average DMIPS : $average_dmips"
    echo "--------------------------"

    #save Average DMIPS in JSON format for benchmark action workflow
    echo -n '{' >> $benchmark_output
    echo -n '"name": "Dhrystone",' >> $benchmark_output
    echo -n '"unit": "Average DMIPS over 10 runs",' >> $benchmark_output
    echo -n '"value": ' >> $benchmark_output
    echo -n $average_dmips >> $benchmark_output
    echo -n '}' >> $benchmark_output
else
    fail
fi
