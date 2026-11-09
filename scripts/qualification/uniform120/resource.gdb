set pagination off
set confirm off
catch throw
run
python
import gdb, json, re
def record_resource_trace(stage):
    frames = []
    frame = gdb.newest_frame()
    while frame and len(frames) < 80:
        frames.append(frame.name() or '')
        frame = frame.older()
    result = {'stage': stage, 'frames': frames, 'state': gdb.execute('info program', to_string=True)}
    if stage == 'throw':
        pid = gdb.selected_inferior().pid
        limits = open('/proc/%d/limits' % pid).read()
        match = re.search(r'^Max address space\s+(\d+)\s+(\d+)\s+bytes', limits, re.M)
        result['address_space_bytes'] = [int(match.group(1)), int(match.group(2))] if match else []
    print('PRIMO_RESOURCE_TRACE ' + json.dumps(result))
record_resource_trace('throw')
end
disable 1
continue
python
record_resource_trace('abort')
end
quit
