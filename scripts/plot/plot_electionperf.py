from plot_python3 import PlotWithPython3

import time
import numpy as np

class PlotElectionPerf(PlotWithPython3):
    def __init__(
        self,
        filename,
        fig1_name = 'electionperf/cummulative_fraction/',
        fig2_name = 'electionperf/terms/'
    ):
        super(PlotElectionPerf, self).__init__(filename)

        # Set the figure name appending the current time
        curr_time = time.strftime('%Y-%m-%d_%H-%M-%S')
        self.fig1_name = '%s%s' % (fig1_name, curr_time)
        self.fig2_name = '%s%s' % (fig2_name, curr_time)

    def plot_figure1(self):
        # Parse data
        electionTimeout = self.data['electionTimeout']

        # Create axis and figure
        fig, ax = self.plt.subplots()

        for electionTimeout in electionTimeout.unique():
            # Filter data
            data = self.data[self.data['electionTimeout'] == electionTimeout]

            # Parse data
            elections = data['elections']
            time = data['time'].sort_values().reset_index(drop=True)

            # Scatter plot
            ax.plot(
                time * 1000,
                time.index/(elections-1),
                label="%d ms" % electionTimeout,
            )

        # Decorations
        self.decorate_axis(ax, 'Duration (ms)', 'Cummulative Fraction')
        self.decorate_figure(fig)

        return fig

    def plot_figure2(self):
        # Parse data
        electionTimeout = self.data['electionTimeout']
        
        hist_data = {
            "electionTimeout": [],
            "terms": [],
        }

        # Create axis and figure
        fig, ax = self.plt.subplots()

        for electionTimeout in electionTimeout.unique():
            # Filter data
            data = self.data[self.data['electionTimeout'] == electionTimeout]

            # Parse data
            terms = data['terms']

            # Store data
            hist_data["electionTimeout"].append(electionTimeout)
            hist_data["terms"].append(terms)

        # Histogram
        ax.hist(
            hist_data["terms"],
            bins='sturges',
            label=["%d ms" % electionTimeout for electionTimeout in hist_data["electionTimeout"]],
            histtype='bar',
        )

        # Decorations
        self.decorate_axis(ax, 'Terms', 'Trials')
        self.decorate_figure(fig)

        return fig

    def plot_stats(self):
        fig1 = self.plot_figure1()
        fig2 = self.plot_figure2()

        fig1.savefig('%s%s.pdf' % (self.figures_dir, self.fig1_name), backend='pgf')
        fig2.savefig('%s%s.pdf' % (self.figures_dir, self.fig2_name), backend='pgf')

def main():
    plot_object = PlotElectionPerf('electionperf.csv')
    plot_object.store_data()
    plot_object.plot_stats()

if __name__ == '__main__':
    main()
