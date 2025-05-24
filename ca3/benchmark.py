import os
import subprocess
import itertools
import multiprocessing
import pandas as pd

# Benchmark maxwell-config1 across all traces
w = [1000000]
s = [10000000]
t = ['bzip2', 'graph_analytics', 'libquantum', 'mcf']
# t = ['bzip2']
traces_folder = './trace'

def main():
    all_option_combo = list(itertools.product(w, s, t))
    with multiprocessing.Pool() as pool:
        results = pool.map(benchmark, all_option_combo)

    data = dict(zip(all_option_combo, results))
    data = [[*k, *v] for k, v in data.items()]
    df = pd.DataFrame(data, columns=['warmup', 'sim', 'trace', 'access', 'hit', 'miss'])
    df['miss_rate'] = df['miss']/df['access']
    print(df)
    
    average_miss_rate = df['miss_rate'].mean()
    print(f'average miss rate = {average_miss_rate}')


def benchmark(options):
    result = run_program(*options)
    access, hit, miss = extract_llc_total_stat(result.stdout)
    
    return access, hit, miss


def run_program(warmup_instructions=1000000, simulation_instructions=10000000, trace='bzip2'):
    trace_file = os.path.join(traces_folder, f'{trace}_10M.trace.gz')
    command = f'./maxwell-config1 -warmup_instructions {warmup_instructions} -simulation_instructions {simulation_instructions} -traces {trace_file}'
    print(f'Run {command=}')
    result = subprocess.run(command.split(), text=True, capture_output=True)
    
    return result


def extract_llc_total_stat(stdout):
    stats = stdout.splitlines()[-5].split()
    access = int(stats[3])
    hit = int(stats[5])
    miss = int(stats[7])
    
    return access, hit, miss


if __name__ == '__main__':
    main()
