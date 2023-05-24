#!/usr/bin/env bash

source tests/common.sh

num_runs=10

function run_dhrystone()
{
    # Run Dhrystone and extract the DMIPS value
#    output=$($RUN $O/dhrystone.elf)
#    dmips=$(echo "$output" | grep -oE '[0-9]+' | awk 'NR==5{print}')
#    echo "$dmips"
    output=$($RUN $O/dhrystone.elf 2>&1)
    local exit_code=$?
    [ $exit_code -ne 0 ] && fail
    dmips=$(echo "$output" | grep -oE '[0-9]+' | awk 'NR==5{print}')
    echo "$dmips"
}

# Run Dhrystone benchmark and collect DMIPS values
dmips_values=()
for ((i=1; i<=$num_runs; i++))
do
    echo "Running Dhrystone benchmark - Run $i"
    dmips=$(run_dhrystone)
    exit_code=$?
    [ $exit_code -ne 0 ] && fail
    dmips_values+=("$dmips")
done

# Filter out non-numeric values
filtered_dmips=()
for dmips in "${dmips_values[@]}"
do
    if [[ $dmips =~ ^[0-9]+$ ]]; then
        filtered_dmips+=("$dmips")
    fi
done

# Calculate average DMIPS excluding outliers
num_filtered=${#filtered_dmips[@]}
if ((num_filtered > 0)); then
    total_filtered_dmips=0
    for dmips in "${filtered_dmips[@]}"
    do
        total_filtered_dmips=$(echo "$total_filtered_dmips + $dmips" | bc -l)
    done
    average_filtered_dmips=$(echo "scale=2; $total_filtered_dmips / $num_filtered" | bc -l)
    echo "---------------------"
    echo "Average DMIPS (Excluding Outliers): $average_filtered_dmips"
    echo "---------------------"
else
    fail
fi
