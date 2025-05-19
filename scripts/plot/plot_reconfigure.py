from plot_python3 import PlotWithPython3

import time

class PlotReconfigure(PlotWithPython3):
    def __init__(
        self,
        filename,
        fig_name = 'reconfigure/'
    ):
        super(PlotReconfigure, self).__init__(filename)

        # Set the figure name appending the current time
        curr_time = time.strftime('%Y-%m-%d_%H-%M-%S')
        self.fig_name = '%s%s' % (fig_name, curr_time)
    
    def plot_stats(self):
        # Group data
        grouped_data = self.data.groupby(['servers', 'tries'])['time']
        grouped_data = grouped_data.agg(['mean', 'std']).reset_index()

        fig, ax = self.plt.subplots()

        for tries_val in grouped_data['tries'].unique():
            # Filter data
            data = grouped_data[grouped_data['tries'] == tries_val]

            # Parse data
            servers = data['servers']
            time_mean = data['mean']
            time_std = data['std']

            label = '%d tries' % tries_val if tries_val > 1 else '1 try'

            # Plot Sample RTT
            ax.errorbar(
                servers,
                time_mean,
                yerr=time_std,
                label=label,
                capsize=4,
                linewidth=1.5,
                marker='o',
                markersize=4,
            )

        self.decorate_axis(ax, 'Servers', 'Time (s)')
        self.decorate_figure(fig)

        fig.savefig('%s%s.pdf' % (self.figures_dir, self.fig_name), backend='pgf')
        

def main():
    plot_object = PlotReconfigure('reconfigure.csv')
    plot_object.store_data()
    plot_object.plot_stats()

if __name__ == '__main__':
    main()
