#include "deferred_allocator.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <vector>

template <typename AllocatorArgument, typename Allocator, typename Container, typename Generator>
auto benchmark(unsigned N, Generator g)
{
    AllocatorArgument h;
    Allocator         a{h};
    Container         c{a};

    auto start = std::chrono::high_resolution_clock::now();
    for (unsigned i = 0; i != N; ++i)
    {
        c.push_back(g(h, a, i));
    }
    auto stop = std::chrono::high_resolution_clock::now();

    return std::chrono::duration<double, std::milli>(stop - start).count() / 1000.0;
}

void write_linechart(
    std::ostream &                          o,
    std::string                             label,
    std::string                             title,
    std::string                             x_title,
    std::string                             y_title,
    std::vector<std::string>                legend,
    std::map<unsigned, std::vector<double>> series);

unsigned const max_iterations = 5000;
unsigned const step = 20;

void all_benchmarks(std::string html_filename)
{
    std::map<unsigned, std::vector<double>> all_series;
    std::map<unsigned, std::vector<double>> deferred_only_series;
    std::vector<std::string>                all_legend{"X", "vector<unique_ptr>", "vector<shared_ptr>", "deferred_vector<deferred_ptr>"};
    std::vector<std::string>                deferred_only_legend{"X", "deferred_vector<deferred_ptr>"};

    for (unsigned i = 0; i < max_iterations; i += step)
    {
        auto d = benchmark<std::allocator<int>, std::allocator<int>, std::vector<std::unique_ptr<unsigned>>>(
            i, [](auto &, auto &, auto & i) { return std::make_unique<unsigned>(i); });

        // std::cout << " " << i << "\t" << i / d << std::endl;
        all_series[i].push_back(d ? i / d : 0);
    }

    for (unsigned i = 0; i < max_iterations; i += step)
    {
        auto d = benchmark<std::allocator<int>, std::allocator<int>, std::vector<std::shared_ptr<unsigned>>>(
            i, [](auto &, auto &, auto & i) { return std::make_shared<unsigned>(i); });

        // std::cout << " " << i << "\t" << i / d << std::endl;
        all_series[i].push_back(d ? i / d : 0);
    }

    for (unsigned i = 0; i < max_iterations; i += step)
    {
        auto d = benchmark<gcpp::deferred_heap, gcpp::deferred_allocator<int>, gcpp::deferred_vector<gcpp::deferred_ptr<unsigned>>>(
            i, [](auto & h, auto &, auto & i) { return h.template make<unsigned>(i); });

        // std::cout << " " << i << "\t" << i / d << std::endl;
        all_series[i].push_back(d ? i / d : 0);
        deferred_only_series[i].push_back(d ? i / d : 0);
    }

    {
        std::ofstream o(html_filename);
        write_linechart(o, "just_deferred", "Deferred only", "Number of insertions", "Insertions per second", deferred_only_legend, deferred_only_series);
        write_linechart(o, "all_benchmarks", "Deferred compared to std", "Number of insertions", "Insertions per second", all_legend, all_series);
    }
}

void one_benchmark(unsigned how_many = max_iterations)
{
    auto unique_ptr_time = benchmark<std::allocator<int>, std::allocator<int>, std::vector<std::unique_ptr<unsigned>>>(
        how_many, [](auto &, auto &, auto & i) { return std::make_unique<unsigned>(i); });

    auto deferred_ptr_time = benchmark<gcpp::deferred_heap, gcpp::deferred_allocator<int>, gcpp::deferred_vector<gcpp::deferred_ptr<unsigned>>>(
        how_many, [](auto & h, auto &, auto & i) { return h.template make<unsigned>(i); });

    std::cout << "Inserting " << how_many << " elements into deferred_vector<deferred_ptr> is " << std::setprecision(0) << (deferred_ptr_time / unique_ptr_time)
              << " times slower than into vector<unique_ptr>" << std::endl;
}

int main(int argc, char * argv[])
{
    std::cout << std::fixed;

    if (argc == 2)
    {
        all_benchmarks(argv[1]);
    }
    else
    {
        one_benchmark();
    }
}

void write_linechart(
    std::ostream &                          o,
    std::string                             label,
    std::string                             title,
    std::string                             x_title,
    std::string                             y_title,
    std::vector<std::string>                h,
    std::map<unsigned, std::vector<double>> all_series)
{
    std::string header = R"html(
    <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
    <script type="text/javascript">
    google.charts.load('current', {packages: ['corechart', 'line']});
    google.charts.setOnLoadCallback( )html" +
                         label + "_f" + R"html(   );

    function )html" + label +
                         "_f" + R"html(() {
      var data = new google.visualization.DataTable();

    )html";
    std::string footer = R"html(
      ]);

      var options = {
        title: ')html" + title +
                         R"html(',
        width: 1400,
        height: 600,
        hAxis: {title: ')html" +
                         x_title + R"html('},
        vAxis: {title: ')html" +
                         y_title + R"html('}
      };

      var chart = new google.visualization.LineChart(document.getElementById(')html" +
                         label + "_d" + R"html('));
      chart.draw(data, options);
    }
    </script>

    <div id=")html" + label +
                         "_d" + R"html(" style="width: 1400px; height: 600px;"></div>

    )html";

    o << header;
    {
        for (auto v : h)
        {
            o << "data.addColumn('number', '" << v << "');";
        }
    }

    o << "data.addRows([\n";

    for (auto [k, v] : all_series)
    {
        o << "[" << k << ", ";
        bool first = true;
        for (auto z : v)
        {
            o << (first ? "" : ", ") << z;
            first = false;
        }
        o << "],\n";
    }
    o << footer << std::endl;
}
