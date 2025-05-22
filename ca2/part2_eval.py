import pandas as pd
import matplotlib.pyplot as plt
import os

master_df = pd.read_csv('./ipc.csv')
print(master_df)

def main():
    grouped = master_df.groupby(['trace'])
    print(grouped.max()['ipc'])
    for name, group in grouped:
        df = group.loc[group['ipc'] >= (grouped.max()['ipc']*0.95)[name[0]]]
        df['h'] = df['r'] + df['j'] + df['k'] + df['l']
        print(df.loc[df['h'].idxmin()])
    
    for f in [4, 8]:
        plot(f, 'gcc')
        plot(f, 'gobmk')
        plot(f, 'hmmer')
        plot(f, 'mcf')


def plot(f=4, trace='gcc'):
    ipc = master_df.loc[(master_df['f'] == f) & (master_df['trace'] == trace)].groupby(['j', 'k', 'l', 'r']).mean(numeric_only=True)['ipc']
    for name, group in ipc.groupby(['j', 'k', 'l']):
        group.rename(name, inplace=True)
        group.index = group.index.droplevel(['j', 'k', 'l'])
        group.plot(title=f'{trace} with fetch rate {f}', xlabel='Result Buses', ylabel='IPC')
    plt.legend(loc="upper left")
    folder = f'./plots'
    if not os.path.exists(folder): os.makedirs(folder)
    plt.savefig(f'./plots/{trace}{f}.png')
    plt.figure().clear()



if __name__ == '__main__':
    main()