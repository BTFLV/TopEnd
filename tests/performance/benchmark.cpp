#include "dsp/TopEndProcessor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
  std::cout << "rate,block,instances,ns_per_stereo_frame,realtime_percent_one_core,min_ns,max_ns\n";
  for (double rate : {44100., 48000., 96000.})
    for (std::size_t block : {32, 128, 512, 1024})
      for (int count : {1, 8, 32}) {
        std::vector<topend::TopEndProcessor> dsps(static_cast<std::size_t>(count));
        for (auto &dsp : dsps) {
          dsp.setParameters({50, 50, -3, false});
          dsp.prepare(rate);
        }
        std::array<std::vector<float>, 2> input{std::vector<float>(block),
                                                std::vector<float>(block)},
            output = input;
        for (std::size_t i = 0; i < block; ++i)
          input[0][i] = input[1][i] = .2f * std::sin(static_cast<float>(i) * .61f);
        const float *in[] = {input[0].data(), input[1].data()};
        float *out[] = {output[0].data(), output[1].data()};
        for (auto &dsp : dsps)
          dsp.process(in, out, 2, block);
        const auto iterations = static_cast<std::size_t>(rate * 2 / static_cast<double>(block));
        std::array<double, 5> runs{};
        for (auto &run : runs) {
          const auto start = std::chrono::steady_clock::now();
          for (std::size_t i = 0; i < iterations; ++i)
            for (auto &dsp : dsps)
              dsp.process(in, out, 2, block);
          const double ns =
              std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start)
                  .count();
          run = ns / (static_cast<double>(iterations * block) * count);
        }
        std::sort(runs.begin(), runs.end());
        const double perFrame = runs[2];
        std::cout << rate << ',' << block << ',' << count << ',' << perFrame << ','
                  << perFrame * rate * count / 1e7 << ',' << runs.front() << ',' << runs.back()
                  << '\n';
      }
}
