import matplotlib.font_manager as font_manager
import matplotlib.pyplot as plt
import pandas as pd

class PlotWithPython3(object):
    def __init__(
        self,
        filename
    ):
        # Data Retrieval
        self.csv_dir = 'scripts/plot/csv/'
        self.figures_dir = 'scripts/plot/figures/'
        self.filename = filename
        self.data = None

        # Figure Configuration
        self.text_width = 483.69684 # inches of text width in LaTex document
        self.fig_size = self._set_size(
            width = self.text_width,
            fraction = 0.5 # two columns document
        )

        # Set matplotlib style
        self.plt = plt

        font_path = '/usr/share/fonts/truetype/ebgaramond/EBGaramond12-Regular.ttf'
        font_manager.fontManager.addfont(font_path)
        prop = font_manager.FontProperties(fname=font_path)

        self.plt.rcParams['font.family'] = 'serif'
        self.plt.rcParams['font.serif'] = prop.get_name()
        self.plt.rcParams.update({'font.size': 10})
    
    def store_data(self):
        # Read data from csv file with pandas
        self.data = pd.read_csv('%s/%s' % (self.csv_dir, self.filename), delimiter=';')

    def _set_size(self, width, fraction=1, subplots=(1, 1)):
        # Width of figure (in pts)
        fig_width_pt = width * fraction
        # Convert from pt to inches
        inches_per_pt = 1 / 72.27

        # Golden ratio to set aesthetic figure height
        # https://disq.us/p/2940ij3
        golden_ratio = (5**0.5 -1 ) / 2

        # Figure width in inches
        fig_width_in = fig_width_pt * inches_per_pt
        # Figure height in inches
        fig_height_in = fig_width_in * golden_ratio * (subplots[0] / subplots[1])

        return (fig_width_in, fig_height_in)

    def decorate_axis(
        self,
        ax,
        xlabel = 'X-axis',
        ylabel = 'Y-axis'
    ):
        # Labels
        ax.set_xlabel(xlabel)
        ax.set_ylabel(ylabel)

        # Ticks
        ax.grid(visible=True, which='major', color='#666666', linestyle='--')
        ax.minorticks_on()
        ax.grid(visible=True, which='minor', color='#999999', linestyle='--', alpha=0.2)

        # Legend
        ax.legend()

    def decorate_figure(self, fig):
        fig.set_size_inches(self.fig_size)
        fig.tight_layout()


def main():
    plot_object = PlotWithPython3('timeout_stats.csv')
    plot_object.store_data()

    print(plot_object.data)

if __name__ == '__main__':
    main()
