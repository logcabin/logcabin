from plot_python3 import PlotWithPython3

import time
import numpy as np

class PlotFailover(PlotWithPython3):
    def __init__(
        self,
        filename,
        fig_name = 'failover/'
    ):
        super(PlotFailover, self).__init__(filename)

        # Set the figure name appending the current time
        curr_time = time.strftime('%Y-%m-%d_%H-%M-%S')
        self.fig_name = '%s%s' % (fig_name, curr_time)

    def _normalize_array(self, array, range=(0, 1)):
        """
        Normalize an array between a range. Used to specify sizes in scatter plots.
        """

        array_min = np.min(array)
        array_max = np.max(array)

        diff = array_max - array_min

        # Normalize between 0 and 1
        normalized_array = (array - array_min) / diff
        # Normalize in the desired range
        normalize_in_range = normalized_array * (range[1] - range[0]) + range[0]

        return normalize_in_range

    def _denormalize_lambda(self, array, range=(0, 1)):
        """
        Returns lambda function to denormalize an array between a range. Used to recover the
        original values from the normalized sizes in scatter plots.
        """

        return lambda x : ((x - range[0]) / (range[1] - range[0])) * (np.max(array) - np.min(array)) + np.min(array)

    def decorate_axis(self, ax, sc, norm_inverse, xlabel = 'Time (s)', ylabel = 'Writes'):
        # Labels
        ax.set_xlabel(xlabel)
        ax.set_ylabel(ylabel)

        # Ticks
        ax.grid(visible=True, which='major', color='#666666', linestyle='--')
        ax.minorticks_on()
        ax.grid(visible=True, which='minor', color='#999999', linestyle='--', alpha=0.2)

        # Sizes legend
        handles, labels = sc.legend_elements(prop="sizes", alpha=0.1, func=norm_inverse)
        legend = ax.legend(handles, labels, title="Kill Interval (s)", loc="lower right")
        ax.add_artist(legend)

    def decorate_figure(self, fig, sc):
        # Color bar
        fig.colorbar(
            sc,
            location = 'right',
            label = 'Launch delay (s)',
        )

        fig.set_size_inches(self.fig_size)

    def plot_stats(self):
        # Grouped data
        grouped_data = self.data.groupby(['writes', 'killinterval', 'launchdelay']).agg({
            "time": 'mean',
        }).reset_index()

        # Parse data
        time = grouped_data['time']
        writes = grouped_data['writes']
        killinterval = grouped_data['killinterval']
        launchdelay = grouped_data['launchdelay']

        # Create axis and figure
        fig, ax = self.plt.subplots(layout="constrained")

        # Scatter plot marker size range
        size_range = (4, 40)

        # Scatter plot
        sc = ax.scatter(
            time,
            writes,
            s = self._normalize_array(killinterval, size_range),
            c = launchdelay,
            alpha=0.8,
            linewidths=0,
        )

        # Decorations
        self.decorate_axis(
            ax=ax,
            sc=sc,
            xlabel='Time (s)',
            ylabel='Writes',
            norm_inverse=self._denormalize_lambda(killinterval, size_range)
        )

        self.decorate_figure(fig, sc)

        fig.savefig('%s%s.pdf' % (self.figures_dir, self.fig_name), backend='pgf')
        

def main():
    plot_object = PlotFailover('failover.csv')
    plot_object.store_data()
    plot_object.plot_stats()

if __name__ == '__main__':
    main()
