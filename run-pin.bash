#!/bin/bash -e

[[ $# == 3 ]] ||
	{ echo "Usage: $0 EVENT-LIST COMMAND" 2>&1; exit 1; }

declare tool=$1
declare command=$2
declare size=$3
declare outfile="run_deal.out"

export PIN_ROOT=/opt/intel/pin
export PATH=$PIN_ROOT:$PATH

touch pin.log

date

pin -t "$tool" -outfile run_ls.out -size "$size" -CTsize 10 -CTbits 2 -- /bin/ls
pin -t "$tool" -outfile run_libQ.out -size "$size" -CTsize 10 -CTbits 2 -- /usr/local/benchmarks/libquantum_O3 400 25
pin -t "$tool" -outfile run_hmmer.out -size "$size" -CTsize 10 -CTbits 2 -- /usr/local/benchmarks/hmmer_O3 /usr/local/benchmarks/inputs/nph3.hmm /usr/local/benchmarks/inputs/swiss41
pin -t "$tool" -outfile run_deal.out -size "$size" -CTsize 10 -CTbits 2 -- /usr/local/benchmarks/dealII_O3 10

date
