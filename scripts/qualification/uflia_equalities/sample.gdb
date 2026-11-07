set pagination off
set confirm off
set startup-with-shell off
handle SIGINT stop print nopass
python
import gdb, json, os, shlex, subprocess, time
binary = os.environ['PRIMO_PROFILE_BINARY']
case = os.environ['PRIMO_PROFILE_CASE']
flags = shlex.split(os.environ['PRIMO_PROFILE_FLAGS'])
output = os.environ['PRIMO_PROFILE_JSON']
samples = []
sample_count = int(os.environ.get('PRIMO_PROFILE_SAMPLES', '30'))
interval = float(os.environ.get('PRIMO_PROFILE_INTERVAL', '0.5'))
assert sample_count > 0 and interval > 0
gdb.execute('file ' + shlex.quote(binary))
gdb.execute('set args ' + ' '.join(shlex.quote(arg) for arg in flags + [case]))
gdb.execute('start')
pid = gdb.selected_inferior().pid
try:
    for index in range(sample_count):
        stop_code = 'import os,signal,sys,time; time.sleep(float(sys.argv[2])); os.kill(int(sys.argv[1]),signal.SIGINT)'
        helper = subprocess.Popen(['python3', '-c', stop_code, str(pid), str(interval)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        started = time.time()
        try:
            gdb.execute('continue')
        except KeyboardInterrupt:
            pass
        finally:
            if helper.poll() is None:
                helper.terminate()
            helper.wait()
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
        json.dump({'binary': binary, 'case': case, 'flags': flags, 'interval_seconds': interval,
                   'requested_samples': sample_count,
                   'samples': samples, 'limitation': 'bounded wall-time stack sampling, not CPU attribution'}, stream, indent=2)
end
quit
