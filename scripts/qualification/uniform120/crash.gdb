set pagination off
set confirm off
set exec-wrapper prlimit --as=8589934592:8589934592
catch throw
run
bt 30
python
import gdb
pid = gdb.selected_inferior().pid
print(open('/proc/%d/status' % pid).read())
print(open('/proc/%d/limits' % pid).read())
end
disable 1
continue
bt 30
quit
