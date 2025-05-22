import os
import subprocess
import itertools
import multiprocessing
import pandas as pd

# Design Space Exploration of procsim
r_options = [1, 2, 3, 4, 5, 6, 7, 8]
j_options = [1, 2]
k_options = [1, 2]
l_options = [1, 2]
f_options = [4, 8]
i_options = ['gcc', 'gobmk', 'hmmer', 'mcf']
traces_folder = './traces'

def main():
    all_option_combo = list(itertools.product(r_options, j_options, k_options, l_options, f_options, i_options))
    # all_option_combo = [(2, 3, 2, 1, 4, 'gcc')]
    with multiprocessing.Pool() as pool:
        results = pool.map(benchmark, all_option_combo)
    
    data = dict(zip(all_option_combo, results))
    data = [[*k, v] for k, v in data.items()]
    df = pd.DataFrame(data, columns=['r', 'j', 'k', 'l', 'f', 'trace', 'ipc'])
    print(df)
    df.to_csv('ipc.csv', index=False)


def benchmark(options):
    return get_procsim_ipc(*options)


def get_procsim_ipc(r=2, j=3, k=2, l=1, f=4, i='gcc'):
    trace_file = os.path.join(traces_folder, f'{i}.100k.trace')
    result = subprocess.run(f'./procsim -r {r} -j {j} -k {k} -l {l} -f {f} -i {trace_file}'.split(), text=True, capture_output=True)
    average_ipc = float(result.stdout.splitlines()[-2].split(':')[1])
    return average_ipc


if __name__ == '__main__':
    main()
