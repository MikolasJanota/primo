set debuginfod enabled off
set pagination off
set confirm off
set startup-with-shell off
handle SIGINT stop noprint nopass
python
import gdb, json, os, shlex, signal, threading, time
binary = os.environ['PRIMO_PROFILE_BINARY']
case = os.environ['PRIMO_PROFILE_CASE']
flags = shlex.split(os.environ['PRIMO_PROFILE_FLAGS'])
output = os.environ['PRIMO_PROFILE_JSON']
samples = []
gdb.execute('file ' + shlex.quote(binary))
gdb.execute('set args ' + ' '.join(shlex.quote(arg) for arg in flags + [case]))
gdb.execute('start')
pid = gdb.selected_inferior().pid
try:
    for index in range(30):
        def stop():
            try:
                os.kill(pid, signal.SIGINT)
            except OSError:
                pass
        timer = threading.Timer(0.5, stop)
        timer.start()
        started = time.time()
        try:
            gdb.execute('continue')
        finally:
            timer.cancel()
            timer.join()
        if not gdb.selected_inferior().pid:
            break
        names = []
        frame = gdb.newest_frame()
        for depth in range(20):
            if frame is None:
                break
            names.append(frame.name() or 'unknown')
            frame = frame.older()
        samples.append({'running_seconds': time.time() - started, 'frames': names})
        gdb.execute('bt 20')
finally:
    if gdb.selected_inferior().pid:
        gdb.execute('kill')
    with open(output, 'w') as stream:
        json.dump({'binary': binary, 'case': case, 'flags': flags, 'interval_seconds': 0.5,
                   'samples': samples, 'limitation': 'bounded wall-time stack sampling, not CPU attribution'}, stream, indent=2)
end
quit
