// Headless AUv2 test host: renders float files through a bundle via the public AU API.
#import <AppKit/AppKit.h>
#import <AudioToolbox/AUCocoaUIView.h>
#import <AudioToolbox/AudioToolbox.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

static void check(OSStatus s, const char* operation) {
  if(s != noErr) throw std::runtime_error(std::string(operation) + ": OSStatus " + std::to_string(s));
}
struct Unit {
  AudioUnit value = nullptr;
  ~Unit() { if(value) { AudioUnitUninitialize(value); AudioComponentInstanceDispose(value); } }
};
struct Source {
  std::vector<float> samples;
  unsigned channels = 2;
  size_t offset = 0;
  static OSStatus pull(void* ctx, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32,
                       UInt32 frames, AudioBufferList* buffers) {
    auto& s = *static_cast<Source*>(ctx);
    for(unsigned c=0; c<buffers->mNumberBuffers; ++c) {
      auto* dest = static_cast<float*>(buffers->mBuffers[c].mData);
      if(!dest) return kAudio_ParamError;
      for(unsigned i=0; i<frames; ++i) {
        const size_t index = (s.offset+i)*s.channels+c;
        dest[i] = index < s.samples.size() ? s.samples[index] : 0.f;
      }
      buffers->mBuffers[c].mDataByteSize = frames*sizeof(float);
    }
    return noErr;
  }
};
static OSType code(NSString* s) {
  const char* c = [s UTF8String];
  if(!c || strlen(c)!=4) throw std::runtime_error("Invalid AU identifier");
  return (UInt32(UInt8(c[0]))<<24) | (UInt32(UInt8(c[1]))<<16) | (UInt32(UInt8(c[2]))<<8) | UInt8(c[3]);
}
int main(int argc, char** argv) {
 @autoreleasepool {
  try {
    std::cerr << "host: AppKit init\n";
    [NSApplication sharedApplication];
    std::cerr << "host: read bundle metadata\n";
    int arg=1;
    if(arg+1>=argc || std::string(argv[arg])!="--bundle")
      throw std::runtime_error("Usage: --bundle path --list | --render ...");
    NSString* path=[NSString stringWithUTF8String:argv[arg+1]]; arg+=2;
    NSBundle* bundle=[NSBundle bundleWithPath:path];
    if(!bundle) throw std::runtime_error("AU bundle missing");
    NSDictionary* meta=[bundle infoDictionary];
    NSArray* components=meta[@"AudioComponents"];
    if([components count]==0) throw std::runtime_error("No public AudioComponents descriptor");
    NSDictionary* item=components[0];
    // Direct registration avoids scanning unrelated installed plugins.
    CFBundleRef cf=CFBundleCreate(kCFAllocatorDefault, (__bridge CFURLRef)[NSURL fileURLWithPath:path]);
    std::cerr << "host: load exported AU factory\n";
    auto factory=reinterpret_cast<AudioComponentFactoryFunction>(CFBundleGetFunctionPointerForName(cf, (__bridge CFStringRef)item[@"factoryFunction"]));
    if(!factory) throw std::runtime_error("Cannot load AU factory");
    AudioComponentDescription desc={code(item[@"type"]),code(item[@"subtype"]),code(item[@"manufacturer"]),0,0};
    auto component=AudioComponentRegister(&desc, (__bridge CFStringRef)item[@"name"], [item[@"version"] unsignedIntValue], factory);
    if(!component) throw std::runtime_error("AU registration failed");
    Unit unit;
    std::cerr << "host: instantiate AU\n";
    check(AudioComponentInstanceNew(component,&unit.value),"instantiate");
    std::cerr << "host: enumerate parameters\n";
    UInt32 size=0;
    check(AudioUnitGetPropertyInfo(unit.value,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,&size,nullptr),"parameter list size");
    std::vector<AudioUnitParameterID> ids(size/sizeof(AudioUnitParameterID));
    check(AudioUnitGetProperty(unit.value,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,0,ids.data(),&size),"parameter list");
    NSMutableArray* parameters=[NSMutableArray array];
    for(auto id:ids) {
      AudioUnitParameterInfo info={}; size=sizeof(info);
      check(AudioUnitGetProperty(unit.value,kAudioUnitProperty_ParameterInfo,kAudioUnitScope_Global,id,&info,&size),"parameter info");
      Float32 value=0; check(AudioUnitGetParameter(unit.value,id,kAudioUnitScope_Global,0,&value),"parameter value");
      NSString* name=(info.flags & kAudioUnitParameterFlag_HasCFNameString) ? (__bridge NSString*)info.cfNameString : [NSString stringWithUTF8String:info.name];
      [parameters addObject:@{@"id":@(id),@"name":name ? name : @"",@"minimum":@(info.minValue),@"maximum":@(info.maxValue),@"default":@(info.defaultValue),@"value":@(value),@"unit":@(info.unit),@"flags":@(info.flags)}];
      if(info.flags & kAudioUnitParameterFlag_CFNameRelease) CFRelease(info.cfNameString);
    }
    if(arg==argc || std::string(argv[arg])=="--list") {
      NSData* json=[NSJSONSerialization dataWithJSONObject:@{@"name":item[@"name"],@"version":meta[@"CFBundleShortVersionString"] ? meta[@"CFBundleShortVersionString"] : @"unknown",@"parameters":parameters} options:NSJSONWritingPrettyPrinted error:nil];
      std::cout.write(static_cast<const char*>(json.bytes),json.length); std::cout<<'\n'; return 0;
    }
    if(argc-arg<6 || std::string(argv[arg])!="--render")
      throw std::runtime_error("Usage: [--bundle path] --list | --render in.f32 out.f32 sample-rate channels block-size [id=value ...] [--events file; id 4294967295 sets host BypassEffect] [--editor seconds]");
    Source source; source.channels=std::stoul(argv[arg+4]);
    double rate=std::stod(argv[arg+3]); UInt32 block=std::stoul(argv[arg+5]);
    if(source.channels<1 || source.channels>2 || rate<8000 || rate>384000 || block<1 || block>16384) throw std::runtime_error("Invalid render configuration");
    std::ifstream input(argv[arg+1],std::ios::binary|std::ios::ate);
    if(!input) throw std::runtime_error("Input open failed");
    auto bytes=input.tellg();
    if(bytes<0 || static_cast<size_t>(bytes)%(sizeof(float)*source.channels)!=0) throw std::runtime_error("Invalid interleaved float input");
    source.samples.resize(static_cast<size_t>(bytes)/sizeof(float)); input.seekg(0); input.read(reinterpret_cast<char*>(source.samples.data()),bytes);
    AudioStreamBasicDescription format={};
    format.mSampleRate=rate; format.mFormatID=kAudioFormatLinearPCM;
    format.mFormatFlags=kAudioFormatFlagIsFloat|kAudioFormatFlagIsPacked|kAudioFormatFlagIsNonInterleaved|kAudioFormatFlagsNativeEndian;
    format.mBytesPerPacket=4; format.mFramesPerPacket=1; format.mBytesPerFrame=4; format.mChannelsPerFrame=source.channels; format.mBitsPerChannel=32;
    check(AudioUnitSetProperty(unit.value,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof(format)),"input format");
    check(AudioUnitSetProperty(unit.value,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Output,0,&format,sizeof(format)),"output format");
    check(AudioUnitSetProperty(unit.value,kAudioUnitProperty_MaximumFramesPerSlice,kAudioUnitScope_Global,0,&block,sizeof(block)),"maximum block");
    AURenderCallbackStruct cb={Source::pull,&source};
    check(AudioUnitSetProperty(unit.value,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&cb,sizeof(cb)),"input callback");
    struct Event { size_t frame; AudioUnitParameterID id; float value; };
    std::vector<Event> events;
    double editorSeconds=0;
    for(int i=arg+6; i<argc; ++i) {
      if(std::string(argv[i])=="--editor") {
        if(++i>=argc) throw std::runtime_error("--editor requires seconds");
        editorSeconds=std::stod(argv[i]);
        continue;
      }
      if(std::string(argv[i])=="--events") {
        if(++i>=argc) throw std::runtime_error("--events requires file");
        std::ifstream eventFile(argv[i]); if(!eventFile) throw std::runtime_error("Events open failed");
        Event event;
        while(eventFile>>event.frame>>event.id>>event.value) events.push_back(event);
        if(!eventFile.eof()) throw std::runtime_error("Invalid event file");
        continue;
      }
      std::string p=argv[i]; auto equal=p.find('=');
      if(equal==p.npos) throw std::runtime_error("Expected id=value");
      check(AudioUnitSetParameter(unit.value,std::stoul(p.substr(0,equal)),kAudioUnitScope_Global,0,std::stof(p.substr(equal+1)),0),"set parameter");
    }
    check(AudioUnitInitialize(unit.value),"initialize");
    Float64 latency=0; size=sizeof(latency);
    auto latencyStatus=AudioUnitGetProperty(unit.value,kAudioUnitProperty_Latency,kAudioUnitScope_Global,0,&latency,&size);
    std::cerr<<"latency_seconds="<<latency<<" status="<<latencyStatus<<'\n';
    if(editorSeconds>0) {
      // Show the plugin's own editor while the input loops in real time.
      [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
      [NSApp finishLaunching];
      UInt32 infoSize=0; Boolean writable=false;
      check(AudioUnitGetPropertyInfo(unit.value,kAudioUnitProperty_CocoaUI,kAudioUnitScope_Global,0,&infoSize,&writable),"editor info size");
      std::vector<std::byte> infoStorage(infoSize);
      auto* info=reinterpret_cast<AudioUnitCocoaViewInfo*>(infoStorage.data());
      check(AudioUnitGetProperty(unit.value,kAudioUnitProperty_CocoaUI,kAudioUnitScope_Global,0,info,&infoSize),"editor info");
      NSBundle* viewBundle=[NSBundle bundleWithURL:(__bridge NSURL*)info->mCocoaAUViewBundleLocation];
      Class viewClass=[viewBundle classNamed:(__bridge NSString*)info->mCocoaAUViewClass[0]];
      id<AUCocoaUIBase> viewFactory=[[viewClass alloc] init];
      NSView* view=[viewFactory uiViewForAudioUnit:unit.value withSize:NSZeroSize];
      if(!view) throw std::runtime_error("Editor view unavailable");
      NSWindow* window=[[NSWindow alloc] initWithContentRect:NSMakeRect(80,80,view.frame.size.width,view.frame.size.height)
                                                   styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
      [window setContentView:view];
      [window orderFrontRegardless];
      std::atomic<bool> stop{false};
      const size_t frames=source.samples.size()/source.channels;
      std::thread audio([&] {
        std::vector<float> scratch(source.channels*block);
        std::vector<std::byte> listStorage(offsetof(AudioBufferList,mBuffers)+sizeof(AudioBuffer)*source.channels);
        auto* list=reinterpret_cast<AudioBufferList*>(listStorage.data()); list->mNumberBuffers=source.channels;
        Float64 time=0;
        auto next=std::chrono::steady_clock::now();
        while(!stop) {
          if(source.offset+block>frames) source.offset=0;
          for(unsigned c=0;c<source.channels;++c) list->mBuffers[c]={1,block*UInt32(sizeof(float)),scratch.data()+c*block};
          AudioTimeStamp stamp={}; stamp.mSampleTime=time; stamp.mFlags=kAudioTimeStampSampleTimeValid;
          AudioUnitRenderActionFlags flags=0;
          AudioUnitRender(unit.value,&flags,&stamp,0,block,list);
          source.offset+=block; time+=block;
          next+=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(block/rate));
          std::this_thread::sleep_until(next);
        }
      });
      [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:editorSeconds]];
      stop=true;
      audio.join();
      [window orderOut:nil];
      CFRelease(cf);
      return 0;
    }
    std::vector<float> planar(source.channels*block),interleaved(source.channels*block);
    std::vector<std::byte> storage(offsetof(AudioBufferList,mBuffers)+sizeof(AudioBuffer)*source.channels);
    auto* buffers=reinterpret_cast<AudioBufferList*>(storage.data()); buffers->mNumberBuffers=source.channels;
    std::ofstream output(argv[arg+2],std::ios::binary); if(!output) throw std::runtime_error("Output open failed");
    size_t frames=source.samples.size()/source.channels;
    std::stable_sort(events.begin(),events.end(),[](const Event& a,const Event& b) {return a.frame<b.frame;});
    size_t eventIndex=0;
    while(source.offset<frames) {
      while(eventIndex<events.size() && events[eventIndex].frame==source.offset) {
        const auto& event=events[eventIndex++];
        // Reserved event ID 4294967295 toggles the host-level BypassEffect property.
        if(event.id==0xFFFFFFFFu) {
          const UInt32 bypass=event.value>=0.5f;
          check(AudioUnitSetProperty(unit.value,kAudioUnitProperty_BypassEffect,kAudioUnitScope_Global,0,&bypass,sizeof(bypass)),"host bypass event");
        } else {
          check(AudioUnitSetParameter(unit.value,event.id,kAudioUnitScope_Global,0,event.value,0),"automation event");
        }
      }
      UInt32 n=static_cast<UInt32>(std::min<size_t>(block,frames-source.offset));
      if(eventIndex<events.size()) n=static_cast<UInt32>(std::min<size_t>(n,events[eventIndex].frame-source.offset));
      for(unsigned c=0;c<source.channels;++c) buffers->mBuffers[c]={1,n*UInt32(sizeof(float)),planar.data()+c*block};
      AudioTimeStamp stamp={}; stamp.mSampleTime=source.offset; stamp.mFlags=kAudioTimeStampSampleTimeValid;
      AudioUnitRenderActionFlags flags=0;
      check(AudioUnitRender(unit.value,&flags,&stamp,0,n,buffers),"render");
      for(unsigned i=0;i<n;++i) for(unsigned c=0;c<source.channels;++c)
        interleaved[i*source.channels+c]=static_cast<float*>(buffers->mBuffers[c].mData)[i];
      output.write(reinterpret_cast<const char*>(interleaved.data()),n*source.channels*sizeof(float));
      source.offset+=n;
    }
    if(!output) throw std::runtime_error("Output write failed");
    CFRelease(cf);
    return 0;
  } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
 }
}
