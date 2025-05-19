import time

from plot_python3 import PlotWithPython3

class PlotSnapshotting(PlotWithPython3):
    def __init__(
        self,
        filename,
        fig_name = 'snapshotting/'
    ):
        super(PlotSnapshotting, self).__init__(filename)

        # Set the figure name appending the current time
        curr_time = time.strftime('%Y-%m-%d_%H-%M-%S')
        self.fig_name = '%s%s' % (fig_name, curr_time)
    
    def plot_stats(self):
        # Group data
        grouped_data = self.data.groupby(['writes', 'size', 'snapshotting'])['time']

        # Calculate mean and standard deviation of the time
        grouped_data = grouped_data.agg(['mean', 'std']).reset_index()

        fig, ax = self.plt.subplots()

        for snapshotting_value in grouped_data['snapshotting'].unique():
            # Filter data
            data = grouped_data[grouped_data['snapshotting'] == snapshotting_value]

            # Parse data
            writes = data['writes']
            size = data['size']
            time_mean = data['mean']
            time_std = data['std']

            # Caclulate the total size of written data to the log
            kilo_byte = 1024
            total_size = size * writes / kilo_byte

            # Label
            label = "Snapshotting" if snapshotting_value else "Bare-Bones"

            # Scatter plot
            ax.errorbar(
                time_mean,
                writes,
                xerr=time_std,
                capsize=4,
                label=label,
                linewidth=1.5,
                marker='o',
                markersize=4,
            )

        self.decorate_axis(ax, 'Time (s)', 'Total Size (KB)')
        self.decorate_figure(fig)

        fig.savefig('%s%s.pdf' % (self.figures_dir, self.fig_name), backend='pgf')

def main():
    plot_object = PlotSnapshotting('snapshotting.csv')
    plot_object.store_data()
    plot_object.plot_stats()

if __name__ == '__main__':
    main()
