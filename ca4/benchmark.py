import os
import subprocess
import itertools
import multiprocessing
import pandas as pd

# Benchmark maxwell-config1 across all traces
option_1 = ('comm1', 'comm2')
option_2 = ('black', 'freq')

def main():
    all_option_combo = [option_1, option_2]
    with multiprocessing.Pool() as pool:
        results = pool.map(benchmark, all_option_combo)

    data = dict(zip(all_option_combo, results))
    data = [[*k, *v] for k, v in data.items()]
    df = pd.DataFrame(data, columns=['input1', 'input2', 'edp'])
    print(df)
    
    average_edp = df['edp'].mean()
    print(f'average EDP = {average_edp}')


def benchmark(options):
    result = run_program(*options)
    edp = extract_edp(result.stdout)
    
    return edp


def run_program(input_1='comm1', input_2='comm2'):
    command = f'bin/usimm input/1channel.cfg input/{input_1} input/{input_2}'
    print(f'Run {command=}')
    result = subprocess.run(command.split(), text=True, capture_output=True)
    
    return result


def extract_edp(stdout):
    stats = stdout.splitlines()[-1].split()
    edp = float(stats[-2])

    return (edp,)


if __name__ == '__main__':
    main()
