#include "dsp/TopEndProcessor.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Parameter events use TopEnd IDs: 0 Mid %, 1 High %, 3 Output Trim dB, 4 Bypass 0/1.
// Blocks are split at event frames, so changes take effect at exact positions.
struct Event {
  std::size_t frame;
  int id;
  float value;
};

std::vector<Event> readEvents(const char *path) {
  std::ifstream file(path);
  if (!file)
    throw std::runtime_error("Cannot open events");
  std::vector<Event> events;
  Event e{};
  while (file >> e.frame >> e.id >> e.value) {
    if (e.id != 0 && e.id != 1 && e.id != 3 && e.id != 4)
      throw std::runtime_error("Unsupported event parameter");
    events.push_back(e);
  }
  if (!file.eof())
    throw std::runtime_error("Malformed events");
  std::stable_sort(events.begin(), events.end(),
                   [](const Event &a, const Event &b) { return a.frame < b.frame; });
  return events;
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::vector<std::string> args(argv + 1, argv + argc);
    bool bypass = false;
    std::vector<Event> events;
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < args.size(); ++i) {
      if (args[i] == "--bypass")
        bypass = true;
      else if (args[i] == "--events" && i + 1 < args.size())
        events = readEvents(args[++i].c_str());
      else
        positional.push_back(args[i]);
    }
    if (positional.size() != 8)
      throw std::runtime_error("Usage: topend-render in.f32 out.f32 rate channels block mid% "
                               "high% trim-dB [--bypass] [--events file]");
    const auto rate = std::stod(positional[2]);
    const auto channels = std::stoul(positional[3]);
    const auto block = std::stoul(positional[4]);
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000 || channels < 1 || channels > 2 ||
        block < 1 || block > 16384)
      throw std::runtime_error("Invalid configuration");
    std::ifstream in(positional[0], std::ios::binary | std::ios::ate);
    if (!in)
      throw std::runtime_error("Cannot open input");
    const auto bytes = in.tellg();
    if (bytes < 0 || static_cast<std::size_t>(bytes) % (channels * sizeof(float)) != 0)
      throw std::runtime_error("Invalid float audio");
    std::vector<float> x(static_cast<std::size_t>(bytes) / sizeof(float)), y(x.size());
    in.seekg(0);
    in.read(reinterpret_cast<char *>(x.data()), bytes);
    topend::TopEndProcessor dsp;
    topend::Parameters parameters{std::stof(positional[5]), std::stof(positional[6]),
                                  std::stof(positional[7]), bypass};
    dsp.setParameters(parameters);
    dsp.prepare(rate);
    std::vector<float> planarIn(channels * block), planarOut(channels * block);
    const float *inputs[2] = {planarIn.data(), planarIn.data() + block};
    float *outputs[2] = {planarOut.data(), planarOut.data() + block};
    const auto frames = x.size() / channels;
    std::size_t next = 0;
    for (std::size_t offset = 0; offset < frames;) {
      for (; next < events.size() && events[next].frame <= offset; ++next) {
        const auto &e = events[next];
        if (e.id == 0)
          parameters.mid = e.value;
        else if (e.id == 1)
          parameters.high = e.value;
        else if (e.id == 3)
          parameters.trimDb = e.value;
        else
          parameters.bypass = e.value >= 0.5f;
        dsp.setParameters(parameters);
      }
      auto n = std::min(block, frames - offset);
      if (next < events.size())
        n = std::min(n, events[next].frame - offset);
      for (std::size_t ch = 0; ch < channels; ++ch)
        for (std::size_t i = 0; i < n; ++i)
          planarIn[ch * block + i] = x[(offset + i) * channels + ch];
      dsp.process(inputs, outputs, channels, n);
      for (std::size_t ch = 0; ch < channels; ++ch)
        for (std::size_t i = 0; i < n; ++i)
          y[(offset + i) * channels + ch] = planarOut[ch * block + i];
      offset += n;
    }
    std::ofstream out(positional[1], std::ios::binary);
    out.write(reinterpret_cast<const char *>(y.data()),
              static_cast<std::streamsize>(y.size() * sizeof(float)));
    if (!out)
      throw std::runtime_error("Cannot write output");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
