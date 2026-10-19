// Minimal offline CLAP host for deterministic adapter/core comparisons.
#include <array>
#include <clap/clap.h>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct InputEvents {
  std::array<clap_event_param_value_t, 5> values{};
  clap_input_events_t api{};
  InputEvents(float mid, float high, float trim) {
    const std::array<double, 5> normalized{mid / 100., high / 100., 0., (trim + 12.) / 12., 0.};
    for (std::uint32_t i = 0; i < values.size(); ++i) {
      auto &event = values[i];
      event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
      event.param_id = i;
      event.note_id = -1;
      event.port_index = -1;
      event.channel = -1;
      event.key = -1;
      event.value = normalized[i];
    }
    api.ctx = this;
    api.size = [](const clap_input_events_t *) -> std::uint32_t { return 5; };
    api.get = [](const clap_input_events_t *list,
                 std::uint32_t index) -> const clap_event_header_t * {
      const auto &self = *static_cast<InputEvents *>(list->ctx);
      return index < self.values.size() ? &self.values[index].header : nullptr;
    };
  }
};
struct Plugin {
  void *library = nullptr;
  const clap_plugin_entry_t *entry = nullptr;
  const clap_plugin_t *instance = nullptr;
  bool initialized = false, active = false, processing = false;
  ~Plugin() {
    if (processing)
      instance->stop_processing(instance);
    if (active)
      instance->deactivate(instance);
    if (instance)
      instance->destroy(instance);
    if (initialized)
      entry->deinit();
    if (library)
      dlclose(library);
  }
};
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 10, "Usage: topend-clap-host plugin-executable in.f32 out.f32 rate channels "
                        "block mid% high% trim-dB");
    const auto rate = std::stod(argv[4]);
    const auto channels = std::stoul(argv[5]);
    const auto block = std::stoul(argv[6]);
    require(channels >= 1 && channels <= 2 && block >= 1 && block <= 16384,
            "Invalid channel/block configuration");
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.name = "TopEnd analysis host";
    host.vendor = "TopEnd";
    host.url = "";
    host.version = "1";
    host.get_extension = [](const clap_host_t *, const char *) -> const void * { return nullptr; };
    host.request_restart = [](const clap_host_t *) {};
    host.request_process = [](const clap_host_t *) {};
    host.request_callback = [](const clap_host_t *) {};
    Plugin plugin;
    plugin.library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    require(plugin.library, "Cannot load CLAP executable");
    plugin.entry = static_cast<const clap_plugin_entry_t *>(dlsym(plugin.library, "clap_entry"));
    require(plugin.entry && clap_version_is_compatible(plugin.entry->clap_version),
            "Missing/incompatible CLAP entry");
    require(plugin.entry->init(argv[1]), "CLAP entry initialization failed");
    plugin.initialized = true;
    const auto *factory = static_cast<const clap_plugin_factory_t *>(
        plugin.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    require(factory && factory->get_plugin_count(factory) == 1, "Expected one plugin");
    const auto *descriptor = factory->get_plugin_descriptor(factory, 0);
    plugin.instance = factory->create_plugin(factory, &host, descriptor->id);
    require(plugin.instance && plugin.instance->init(plugin.instance),
            "Plugin initialization failed");
    const auto *ports = static_cast<const clap_plugin_audio_ports_config_t *>(
        plugin.instance->get_extension(plugin.instance, CLAP_EXT_AUDIO_PORTS_CONFIG));
    require(ports, "Missing audio port configurations");
    bool selected = false;
    for (std::uint32_t i = 0; i < ports->count(plugin.instance); ++i) {
      clap_audio_ports_config_t config{};
      if (ports->get(plugin.instance, i, &config) && config.main_input_channel_count == channels &&
          config.main_output_channel_count == channels) {
        selected = ports->select(plugin.instance, config.id);
        break;
      }
    }
    require(selected, "Requested channel configuration unavailable");
    const auto *parameters = static_cast<const clap_plugin_params_t *>(
        plugin.instance->get_extension(plugin.instance, CLAP_EXT_PARAMS));
    require(parameters && parameters->count(plugin.instance) == 5, "Unexpected parameters");
    InputEvents changes(std::stof(argv[7]), std::stof(argv[8]), std::stof(argv[9]));
    clap_output_events_t outputEvents{
        nullptr, [](const clap_output_events_t *, const clap_event_header_t *) { return true; }};
    parameters->flush(plugin.instance, &changes.api, &outputEvents);
    require(plugin.instance->activate(plugin.instance, rate, 1, static_cast<std::uint32_t>(block)),
            "Activate failed");
    plugin.active = true;
    require(plugin.instance->start_processing(plugin.instance), "Start processing failed");
    plugin.processing = true;
    std::ifstream input(argv[2], std::ios::binary | std::ios::ate);
    require(bool(input), "Cannot read input");
    const auto bytes = input.tellg();
    require(bytes >= 0 && static_cast<std::size_t>(bytes) % (sizeof(float) * channels) == 0,
            "Invalid raw frames");
    std::vector<float> x(static_cast<std::size_t>(bytes) / sizeof(float)), y(x.size());
    input.seekg(0);
    input.read(reinterpret_cast<char *>(x.data()), bytes);
    require(bool(input), "Input read failed");
    std::array<std::vector<float>, 2> planarIn{std::vector<float>(block),
                                               std::vector<float>(block)},
        planarOut = planarIn;
    float *ins[] = {planarIn[0].data(), planarIn[1].data()};
    float *outs[] = {planarOut[0].data(), planarOut[1].data()};
    clap_audio_buffer_t inBuffer{ins, nullptr, static_cast<std::uint32_t>(channels), 0, 0};
    clap_audio_buffer_t outBuffer{outs, nullptr, static_cast<std::uint32_t>(channels), 0, 0};
    clap_input_events_t empty{nullptr,
                              [](const clap_input_events_t *) -> std::uint32_t { return 0; },
                              [](const clap_input_events_t *,
                                 std::uint32_t) -> const clap_event_header_t * { return nullptr; }};
    const auto frames = x.size() / channels;
    for (std::size_t offset = 0; offset < frames; offset += block) {
      const auto n = std::min(block, frames - offset);
      for (std::size_t ch = 0; ch < channels; ++ch)
        for (std::size_t i = 0; i < n; ++i)
          planarIn[ch][i] = x[(offset + i) * channels + ch];
      clap_process_t process{};
      process.steady_time = static_cast<std::int64_t>(offset);
      process.frames_count = static_cast<std::uint32_t>(n);
      process.audio_inputs = &inBuffer;
      process.audio_outputs = &outBuffer;
      process.audio_inputs_count = process.audio_outputs_count = 1;
      process.in_events = &empty;
      process.out_events = &outputEvents;
      require(plugin.instance->process(plugin.instance, &process) != CLAP_PROCESS_ERROR,
              "Processing failed");
      for (std::size_t ch = 0; ch < channels; ++ch)
        for (std::size_t i = 0; i < n; ++i)
          y[(offset + i) * channels + ch] = planarOut[ch][i];
    }
    std::ofstream output(argv[3], std::ios::binary);
    output.write(reinterpret_cast<const char *>(y.data()),
                 static_cast<std::streamsize>(y.size() * sizeof(float)));
    require(bool(output), "Cannot write output");
    std::cout << descriptor->name << " " << descriptor->version << ": " << frames
              << " frames rendered\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
