-- Run only in a dedicated test REAPER instance. No user projects are opened.
local source = debug.getinfo(1, 'S').source:sub(2)
local root = source:match('^(.*)/tests/integration/')
assert(root, 'Cannot locate TopEnd workspace')
local destination = root .. '/build/host-results'
reaper.RecursiveCreateDirectory(destination, 0)
local log = assert(io.open(destination .. '/reaper-results.txt', 'w'))
log:write('REAPER ' .. reaper.GetAppVersion() .. '\n')
local formats = {'VST3', 'CLAP', 'AU'}
local current = 1
local track, fx, expected, cycles, nextTime
local function record(message) log:write(message .. '\n'); log:flush() end
local function parameterMap(t, f)
  local map = {}
  for i=0,reaper.TrackFX_GetNumParams(t,f)-1 do
    local ok,name = reaper.TrackFX_GetParamName(t,f,i)
    if ok then map[name] = i end
  end
  return map
end
local function start()
  if current > #formats then
    record('PASS: all three formats instantiated, automated, restored; repeated editor cycles complete')
    log:close()
    reaper.Main_OnCommand(40860, 0) -- Close current project tab; test session only.
    return
  end
  reaper.Main_OnCommand(40023,0) -- New project tab.
  reaper.InsertTrackAtIndex(0,true);track=reaper.GetTrack(0,0)
  local ok = reaper.InsertMedia(destination .. '/input.wav',0)
  assert(ok~=0,'Input insert failed')
  local item=reaper.GetTrackMediaItem(track,0)
  assert(item,'Missing test item')
  reaper.SetMediaItemInfo_Value(item,'D_FADEINLEN',0)
  reaper.SetMediaItemInfo_Value(item,'D_FADEOUTLEN',0)
  fx=reaper.TrackFX_AddByName(track,formats[current] .. ': TopEnd',false,-1)
  assert(fx>=0,formats[current] .. ' discovery failed')
  local map=parameterMap(track,fx)
  for name,value in pairs({['Mid Air']=.35,['High Air']=.5,['Output Trim']=.75,['Link']=1,['Bypass']=0}) do
    assert(map[name], 'Missing parameter ' .. name)
    reaper.TrackFX_SetParamNormalized(track,fx,map[name],value)
  end
  expected={}
  for name,index in pairs(map) do expected[name]=reaper.TrackFX_GetParamNormalized(track,fx,index) end
  local second=reaper.TrackFX_AddByName(track,formats[current] .. ': TopEnd',false,-1)
  assert(second>=0,'Second instance failed')
  reaper.GetSetProjectInfo(0,'RENDER_SRATE',48000,true)
  reaper.GetSetProjectInfo(0,'RENDER_CHANNELS',2,true)
  reaper.GetSetProjectInfo(0,'RENDER_SETTINGS',0,true)
  reaper.GetSetProjectInfo(0,'RENDER_BOUNDSFLAG',0,true)
  reaper.GetSetProjectInfo(0,'RENDER_TAILFLAG',0,true)
  reaper.GetSetProjectInfo_String(0,'RENDER_FILE',destination,true)
  reaper.GetSetProjectInfo_String(0,'RENDER_PATTERN','TopEnd-' .. formats[current],true)
  reaper.TrackFX_Show(track,fx,3)
  cycles=0;nextTime=reaper.time_precise()+.1
  reaper.defer(function() _G.TopEndTick() end)
end
function _G.TopEndTick()
  if reaper.time_precise()<nextTime then reaper.defer(TopEndTick);return end
  if cycles<20 then
    reaper.TrackFX_Show(track,fx,cycles%2==0 and 2 or 3)
    cycles=cycles+1;nextTime=reaper.time_precise()+.05
    reaper.defer(TopEndTick);return
  end
  reaper.TrackFX_Show(track,fx,2)
  local project=destination .. '/' .. formats[current] .. '.rpp'
  reaper.Main_SaveProjectEx(0,project,0)
  -- Reload our saved project and assert all parameter values before another save.
  reaper.Main_openProject('noprompt:' .. project)
  track=reaper.GetTrack(0,0);fx=0
  local restored=parameterMap(track,fx)
  for name,value in pairs(expected) do
    assert(restored[name] and math.abs(reaper.TrackFX_GetParamNormalized(track,fx,restored[name])-value)<1e-7,'State mismatch: '..name)
  end
  reaper.Main_SaveProjectEx(0,project,0)
  record('PASS ' .. formats[current] .. ': discovery, two instances, parameters, save/reload, 10 editor open/close cycles')
  current=current+1;start()
end
start()
