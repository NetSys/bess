import os
import os.path
import sys
import fnmatch
import socket
import fcntl
import errno
import glob
import re
import subprocess
import pprint
import copy
import time
import inspect
import traceback
import tempfile
import signal

import sugar
from port import *
from module import *

# extention for configuration files.
CONF_EXT = 'bess'

# errors in configuration file
class ConfError(Exception):
    pass

def __bess_env__(key, default=None):
    try:
        return os.environ[key]
    except KeyError:
        if default is None:
            raise ConfError('Environment variable "%s" must be set.')

        print 'Environment variable "%s" is not set. Using default value "%s"' \
                % (key, default)
        return default

def __bess_module__(module_names, mclass_name, *args, **kwargs):
    caller_globals = inspect.stack()[1][0].f_globals 
    
    if mclass_name not in caller_globals:
        raise ConfError("Module class %s does not exist" % mclass_name)
    mclass_obj = caller_globals[mclass_name]

    if isinstance(module_names, str):
        if module_names in caller_globals:
            raise ConfError("Module name %s already exists" % module_names)
        obj = mclass_obj(module_names, *args, **kwargs)
        caller_globals[module_names] = obj
        return obj

    # a,b,c::SomeMod()
    elif isinstance(module_names, tuple):
        obj_list = []

        for module in module_names:
            if module in caller_globals:
                raise ConfError("Module name %s already exists" % module)

        for module in module_names:
            obj = mclass_obj(module, *args, **kwargs)
            caller_globals[module] = obj
            obj_list.append(obj)
        return obj_list

    else:
        assert False, 'Invalid argument %s' % type(module_names)


def is_allowed_filename(basename):
    # do not allow whitespaces
    for c in basename:
        if c.isspace():
            return False

    return True

def complete_filename(partial_word, start_dir='', suffix=''):
    try:
        sub_dir, partial_basename = os.path.split(partial_word)
        pattern = '%s*%s' % (partial_basename, suffix)

        target_dir = os.path.join(start_dir, os.path.expanduser(sub_dir))
        if target_dir:
            basenames = os.listdir(target_dir)
        else:
            basenames = os.listdir(os.curdir)

        candidates = []
        for basename in basenames + ['.', '..']:
            if basename.startswith('.'):
                if not partial_basename.startswith('.'):
                    continue

            if not is_allowed_filename(basename):
                continue

            #print '%s-%s-%s' % (target_dir, basename, os.path.join(target_dir, basename))
            if os.path.isdir(os.path.join(target_dir, basename)):
                candidates.append(basename + '/')
            else:
                if fnmatch.fnmatch(basename, pattern):
                    if suffix:
                        basename = basename[:-len(suffix)]
                    candidates.append(basename)
      
        ret = []
        for candidate in candidates:
            ret.append(os.path.join(sub_dir, candidate))
        return ret
    
    except OSError:
        # ignore failure of os.listdir()
        return []

def get_var_attrs(cli, var_token, partial_word):
    var_type = None
    var_desc = ''
    var_candidates = []

    try:
        if var_token == 'WORKER_ID...':
            var_type = 'wid+'
            var_desc = 'one or more worker IDs'
            var_candidates = [str(m.wid) for m in cli.bess.list_workers()]

        elif var_token == 'DRIVER':
            var_type = 'name'
            var_desc = 'name of a port driver'
            var_candidates = cli.bess.list_drivers()

        elif var_token == 'MCLASS':
            var_type = 'name'
            var_desc = 'name of a module class'
            var_candidates = cli.bess.list_mclasses()

        elif var_token == '[NEW_MODULE]':
            var_type = 'name'
            var_desc = 'specify a name of the new module instance'

        elif var_token == 'MODULE':
            var_type = 'name'
            var_desc = 'name of an existing module instance'
            var_candidates = [m.name for m in cli.bess.list_modules()]

        elif var_token == 'MODULE...':
            var_type = 'name+'
            var_desc = 'one or more module names'
            var_candidates = [m.name for m in cli.bess.list_modules()]

        elif var_token == '[NEW_PORT]':
            var_type = 'name'
            var_desc = 'specify a name of the new port'
        
        elif var_token == 'PORT':
            var_type = 'name'
            var_desc = 'name of a port'
            var_candidates = [p.name for p in cli.bess.list_ports()]

        elif var_token == 'PORT...':
            var_type = 'name+'
            var_desc = 'one or more port names'
            var_candidates = [p.name for p in cli.bess.list_ports()]

        elif var_token == 'TC...':
            var_type = 'name+'
            var_desc = 'one or more traffic class names'
            var_candidates = [c.name for c in cli.bess.list_tcs()]

        elif var_token == 'CONF':
            var_type = 'confname'
            var_desc = 'configuration name in "conf/" directory'
            var_candidates = complete_filename(partial_word,
                    '%s/conf' % cli.this_dir, '.' + CONF_EXT)

        elif var_token == 'CONF_FILE':
            var_type = 'filename'
            var_desc = 'configuration filename'
            var_candidates = complete_filename(partial_word)

        elif var_token == '[OGATE]':
            var_type = 'gate'
            var_desc = 'output gate of a module (default 0)'

        elif var_token == '[IGATE]':
            var_type = 'gate'
            var_desc = 'input gate of a module (default 0)'

        elif var_token == '[ENV_VARS...]':
            var_type = 'map'
            var_desc = 'Environmental variables for configuration'

        elif var_token == '[PORT_ARGS...]':
            var_type = 'map'
            var_desc = 'initial configuration for port'

        elif var_token == '[MODULE_ARGS...]':
            var_type = 'pyobj'
            var_desc = 'initial configuration for module'

        elif var_token == '[TCPDUMP_OPTS...]':
            var_type = 'opts'
            var_desc = 'tcpdump(1) options (e.g., "-ne tcp port 22")'

        elif var_token == '[BESSD_OPTS...]':
            var_type = 'opts'
            var_desc = 'bess daemon options (see "bessd -h")'

    except socket.error as e:
        if e.errno in [errno.ECONNRESET, errno.EPIPE]:
            cli.bess.disconnect()
        else:
            raise

    except cli.bess.APIError:
        pass

    if var_type == None:
        return None
    else:
        return var_type, var_desc, var_candidates

# Return (head, tail)
#   head: consumed string portion
#   tail: the rest of input line
# You can assume that 'line == head + tail'
def split_var(cli, var_type, line):
    if var_type in ['name', 'gate', 'confname', 'filename']:
        pos = line.find(' ')
        if pos == -1:
            head = line
            tail = ''
        else:
            head = line[:pos]
            tail = line[pos:]

    elif var_type in ['wid+', 'name+', 'map', 'pyobj', 'opts']:
        head = line
        tail = ''

    else:
        raise cli.InternalError('type "%s" is undefined', var_type)

    return head, tail

def _parse_map(**kwargs):
    return kwargs

# Return (mapped_value, tail)
#   mapped_value: Python value/object from the consumed token(s)
#   tail: the rest of input line
def bind_var(cli, var_type, line):
    head, remainder = split_var(cli, var_type, line)

    # default behavior
    val = head

    if var_type == 'wid+':
        val = []
        for wid_str in head.split():
            if wid_str.isdigit():
                val.append(int(wid_str))
            else:
                raise cli.BindError('"wid" must be a positive number')
        val = sorted(list(set(val)))

    elif var_type == 'name':
        if re.match(r'^[_a-zA-Z][\w]*$', val) is None:
            raise cli.BindError('"name" must be [_a-zA-Z][_a-zA-Z0-9]*')

    elif var_type == 'gate':
        if head.isdigit():
            val = int(head)
        else:
            raise cli.BindError('"gate" must be a positive number')

    elif var_type == 'name+':
        val = sorted(list(set(head.split()))) # collect unique items
        for name in val:
            if re.match(r'^[_a-zA-Z][\w]*$', name) is None:
                raise cli.BindError('"name" must be [_a-zA-Z][_a-zA-Z0-9]*')
   
    elif var_type == 'confname':
        if val.find('\0') >= 0:
            raise cli.BindError('Invalid configuration name')

    elif var_type == 'filename':
        if val.find('\0') >= 0:
            raise cli.BindError('Invalid filename')

    elif var_type == 'map':
        try:
            val = eval('_parse_map(%s)' % head)
        except:
            raise cli.BindError('"map" should be "key=val, key=val, ..."')

    elif var_type == 'pyobj':
        try:
            if head.strip() == '':
                val = None
            else:
                val = eval(head)
        except:
            raise cli.BindError('"pyobj" should be an object in python syntax' \
                    ' (e.g., 42, "foo", ["hello", "world"], {"bar": "baz"})')

    elif var_type == 'opts':
        val = val.split()

    return val, remainder

cmdlist = []

def cmd(syntax, desc=''):
    def cmd_decorator(func):
        cmdlist.append((syntax, desc, func))
    return cmd_decorator

@cmd('help', 'List available commands')
def help(cli):
    for syntax, desc, _ in cmdlist:
        cli.fout.write('  %-50s%s\n' % (syntax, desc))
    cli.fout.flush()

@cmd('quit', 'Quit CLI')
def help(cli):
    raise EOFError()

@cmd('history', 'Show command history')
def history(cli):
    if cli.rl:
        len_history = cli.rl.get_current_history_length()
        begin_index = max(1, len_history - 100)     # max 100 items
        for i in range(begin_index, len_history):   # skip the last one
            cli.fout.write('%5d  %s\n' % (i, cli.rl.get_history_item(i)))
    else:
        cli.err('"readline" not available')

@cmd('daemon connect [HOST] [PORT]', 'Connect to BESS daemon')
def daemon_connect(cli, host, port):
    kwargs = {}

    if host:
        kwargs['host'] = host

    if port:
        kwargs['port'] = int(port)

    cli.bess.connect(**kwargs)

@cmd('daemon disconnect', 'Disconnect from BESS daemon')
def daemon_disconnect(cli):
    cli.bess.disconnect()

# return False iff canceled.
def warn(cli, msg, func, *args):
    if cli.interactive:
        if cli.rl:
            cli.rl.set_completer(cli.complete_dummy)

        try:
            resp = raw_input('WARNING: %s Are you sure? (type "yes") ' % msg)

            if resp.strip() == 'yes':
                func(cli, *args)
            else:
                cli.fout.write('Canceled.\n')
                return False

        except KeyboardInterrupt:
            cli.fout.write('Canceled.\n')
            return False
        finally:
            if cli.rl:
                cli.rl.set_completer(cli.complete)

                # don't leave response in the history
                hist_len = cli.rl.get_current_history_length()
                cli.rl.remove_history_item(hist_len - 1)

    else:
        func(cli, *args)

    return True

def _do_start(cli, opts):
    if opts is None:
        opts = []

    cmd = 'sudo %s/core/bessd -k %s' % \
            (os.path.dirname(cli.this_dir), ' '.join(opts))

    cli.bess.disconnect()

    try:
        ret = os.system('sudo -n echo -n 2> /dev/null')
        if os.WEXITSTATUS(ret) != 0:
            cli.fout.write('You need root privilege to launch BESS daemon, '
                    'but "sudo" requires a password for this account.\n')
        subprocess.check_call(cmd, shell='True')
    except subprocess.CalledProcessError:
        try:
            cli.bess.connect()  # reconnect to the old instance, if any
        except:
            pass
        raise cli.CommandError('Cannot start BESS daemon')
    else:
        cli.bess.connect()

@cmd('daemon start [BESSD_OPTS...]', 'Start BESS daemon in the local machine')
def daemon_start(cli, opts):
    daemon_exists = False

    try:
        with open('/var/run/bessd.pid', 'r') as f:
            try:
                fcntl.flock(f.fileno(), fcntl.LOCK_SH | fcntl.LOCK_NB)
            except IOError as e:
                if e.errno in [errno.EAGAIN, errno.EACCES]:
                    daemon_exists = True
                else:
                    raise
    except IOError as e:
        if e.errno != errno.ENOENT:
            raise

    if daemon_exists:
        warn(cli, 'Existing BESS daemon will be killed.', _do_start, opts)
    else:
        _do_start(cli, opts)

def is_pipeline_empty(cli):
    workers = cli.bess.list_workers()
    return len(workers) == 0

def _do_reset(cli):
    cli.bess.pause_all()
    cli.bess.reset_all()
    cli.bess.resume_all()
    if cli.interactive:
        cli.fout.write('Done.\n')

@cmd('daemon reset', 'Remove all ports and modules in the pipeline')
def daemon_reset(cli):
    if is_pipeline_empty(cli):
        _do_reset(cli)
    else:
        warn(cli, 'The entire pipeline will be cleared.', _do_reset)

def _do_stop(cli):
    cli.bess.pause_all()
    cli.bess.kill()
    if cli.interactive:
        cli.fout.write('Done.\n')

@cmd('daemon stop', 'Stop BESS daemon')
def daemon_stop(cli):
    if is_pipeline_empty(cli):
        _do_stop(cli)
    else:
        warn(cli, 'BESS daemon will be killed.', _do_stop)

@staticmethod
def _choose_arg(arg, kwargs):
    if kwargs:
        for key in kwargs:
            if isinstance(kwargs[key], (Module, Port)):
                kwargs[key] = kwargs[key].name

        if arg:
            raise bess.Error('You cannot specify both arg and keyword args')
        else:
            return kwargs

    if isinstance(arg, (Module, Port)):
        return arg.name
    else:
        return arg

def _clear_pipeline(cli):
    cli.bess.pause_all()
    cli.bess.reset_all()

# NOTE: the name of this function is used below
def _do_run_file(cli, conf_file):
    if not os.path.exists(conf_file):
        cli.err('Cannot open file "%s"' % conf_file)
        return

    xformed = sugar.xform_file(conf_file)

    new_globals = {
            '__builtins__': __builtins__,
            'bess': cli.bess,
            'ConfError': ConfError,
            '__bess_env__': __bess_env__,
            '__bess_module__': __bess_module__,
        }

    class_names = cli.bess.list_mclasses()

    # Add the special Port class. TODO: per-driver classes
    new_globals['Port'] = type('Port', (Port,), 
            {'bess': cli.bess, 'choose_arg': _choose_arg})

    # Add BESS module classes
    for name in class_names:
        if name in new_globals:
            raise cli.InternalError('Invalid module class name: %s' % name)

        new_globals[name] = type(name, (Module,),
                {'bess': cli.bess, 'choose_arg': _choose_arg})

    code = compile(xformed, conf_file, 'exec')

    if is_pipeline_empty(cli):
        cli.bess.pause_all()
    else:
        ret = warn(cli, 'The current pipeline will be reset.', _clear_pipeline)
        if ret is False:
            return

    try:
        exec(code, new_globals)
        if cli.interactive:
            cli.fout.write('Done.\n')
    except cli.bess.Error:
        raise

    except cli.bess.APIError:
        raise

    except ConfError as e:
        cli.err(e.message)

    except:
        cur_frame = inspect.currentframe()
        cur_func = inspect.getframeinfo(cur_frame).function
        t, v, tb = sys.exc_info()
        stack = traceback.extract_tb(tb)

        while len(stack) > 0 and stack.pop(0)[2] != cur_func:
            pass

        cli.err('Unhandled exception in the script (most recent call last)')
        cli.fout.write(''.join(traceback.format_list(stack)))
        cli.fout.write(''.join(traceback.format_exception_only(t, v)))

    finally:
        cli.bess.resume_all()

def _run_file(cli, conf_file, env_map):
    if env_map:
        try:
            original_env = copy.copy(os.environ)

            for k, v in env_map.iteritems():
                os.environ[k] = str(v)

            _do_run_file(cli, conf_file)
        finally:
            os.environ.clear()
            for k, v in original_env.iteritems():
                os.environ[k] = v
    else:
            _do_run_file(cli, conf_file)

@cmd('run CONF [ENV_VARS...]', 'Run a *.bess configuration in "conf/"')
def run_conf(cli, conf, env_map):
    target_dir = '%s/conf' % cli.this_dir
    basename = os.path.expanduser('%s.%s' % (conf, CONF_EXT))
    conf_file = os.path.join(target_dir, basename)
    _run_file(cli, conf_file, env_map)

@cmd('run file CONF_FILE [ENV_VARS...]', 'Run a configuration file')
def run_file(cli, conf_file, env_map):
    _run_file(cli, os.path.expanduser(conf_file), env_map)

@cmd('add port DRIVER [NEW_PORT] [PORT_ARGS...]', 'Add a new port')
def add_port(cli, driver, port, args):
    ret = cli.bess.create_port(driver, port, args)

    if port is None:
        cli.fout.write('  The new port "%s" has been created\n' % ret.name)

@cmd('add module MCLASS [NEW_MODULE] [MODULE_ARGS...]', 'Add a new module')
def add_module(cli, mclass, module, args):
    cli.bess.pause_all()
    try:
        ret = cli.bess.create_module(mclass, module, args)
    finally:
        cli.bess.resume_all()

    if module is None:
        cli.fout.write('  The new module "%s" has been created\n' % ret.name)

@cmd('add connection MODULE MODULE [OGATE] [IGATE]', 
        'Add a connection between two modules')
def add_connection(cli, m1, m2, ogate, igate):
    if ogate is None:
        ogate = 0

    if igate is None:
        igate = 0

    cli.bess.pause_all()
    try:
        cli.bess.connect_modules(m1, m2, ogate, igate)
    finally:
        cli.bess.resume_all()

@cmd('delete port PORT', 'Delete a port')
def delete_port(cli, port):
    cli.bess.destroy_port(port)

@cmd('delete module MODULE', 'Delete a module')
def delete_module(cli, module):
    cli.bess.pause_all()
    try:
        cli.bess.destroy_module(module)
    finally:
        cli.bess.resume_all()

@cmd('delete connection MODULE ogate [OGATE]', 
    'Delete a connection between two modules')
def delete_connection(cli, module, ogate):
    if ogate is None:
        ogate = 0

    cli.bess.pause_all()
    try:
        cli.bess.disconnect_modules(module, ogate)
    finally:
        cli.bess.resume_all()

def _show_worker_header(cli):
    cli.fout.write('  %10s%10s%10s%10s%16s\n' % \
            ('Worker ID', 
             'Status', 
             'CPU core', 
             '# of TCs',
             'Deadend pkts'))

def _show_worker(cli, w):
    cli.fout.write('  %10d%10s%10d%10d%16d\n' % \
            (w.wid, 
             'RUNNING' if w.running else 'PAUSED', 
             w.core, 
             w.num_tcs,
             w.silent_drops))

@cmd('show worker', 'Show the status of all worker threads')
def show_worker_all(cli):
    workers = cli.bess.list_workers()

    if not workers:
        raise cli.CommandError('There is no active worker thread to show.')
       
    _show_worker_header(cli)
    for worker in workers:
        _show_worker(cli, worker)

@cmd('show worker WORKER_ID...', 'Show the status of specified worker threads')
def show_worker_list(cli, worker_ids):
    workers = cli.bess.list_workers()

    for wid in worker_ids:
        for worker in workers:
            if worker.wid == wid:
                break;
        else:
            raise cli.CommandError('Worker ID %d does not exist' % wid)

    _show_worker_header(cli)
    for worker in workers:
        if worker.wid in worker_ids:
            _show_worker(cli, worker)

def _limit_to_str(limit):
    buf = []
    
    if limit.schedules:
        buf.append('%d times' % limit.schedules)

    if limit.cycles:
        buf.append('%.3f Mhz' % (limit.cycles / 1e6))

    if limit.packets:
        if limit.packets < 1e3:
            buf.append('%.d pps' % limit.packets)
        elif limit.packets < 1e6:
            buf.append('%.3f kpps' % (limit.packets / 1e3))
        else:
            buf.append('%.3f Mpps' % (limit.packets / 1e6))

    if limit.bits:
        if limit.bits < 1e3:
            buf.append('%.d bps' % limit.bits)
        elif limit.bits < 1e6:
            buf.append('%.3f kbps' % (limit.bits / 1e3))
        else:
            buf.append('%.3f Mbps' % (limit.bits / 1e6))

    if buf:
        return 'limits: ' + ', '.join(buf)
    else:
        return 'unlimited'

def _show_tc_list(cli, tcs):
    wids = sorted(list(set(map(lambda tc: tc.wid, tcs))))

    for wid in wids:
        matched= filter(lambda tc: tc.wid == wid, tcs)

        if wid == -1:
            cli.fout.write('  Unattached (%d classes)\n' % len(matched))
        else:
            cli.fout.write('  worker %d (%d classes)\n' % (wid, len(matched)))

        for tc in matched:
            cli.fout.write('    %-16s  ' \
                           'parent %-10s  priority %-3d  tasks %-3d ' \
                           '%s\n' % \
                    (tc.name, 
                     tc.parent if tc.parent else 'none', 
                     tc.priority,
                     tc.tasks,
                     _limit_to_str(tc.limit)))

            
@cmd('show tc', 'Show the list of traffic classes')
def show_tc_all(cli):
    _show_tc_list(cli, cli.bess.list_tcs())

@cmd('show tc worker WORKER_ID...', 'Show the list of traffic classes')
def show_tc_workers(cli, wids):
    wids = sorted(list(set(wids)))
    for wid in wids:
        _show_tc_list(cli, cli.bess.list_tcs(wid))

@cmd('show status', 'Show the overall status')
def show_status(cli):
    workers = sorted(cli.bess.list_workers())
    drivers = sorted(cli.bess.list_drivers())
    mclasses = sorted(cli.bess.list_mclasses())
    modules = sorted(cli.bess.list_modules())
    ports = sorted(cli.bess.list_ports())

    cli.fout.write('  Active worker threads: ')
    if workers:
        worker_list = ['worker%d (cpu %d)' % \
                (worker.wid, worker.core) for worker in workers]
        cli.fout.write('%s\n' % ', '.join(worker_list))
    else:
        cli.fout.write('(none)\n')

    cli.fout.write('  Available drivers: ')
    if drivers:
        cli.fout.write('%s\n' % ', '.join(drivers))
    else:
        cli.fout.write('(none)\n')

    cli.fout.write('  Available module classes: ')
    if mclasses:
        cli.fout.write('%s\n' % ', '.join(mclasses))
    else:
        cli.fout.write('(none)\n')
    
    cli.fout.write('  Active ports: ')
    if ports:
        port_list = ['%s/%s' % (p.name, p.driver) for p in ports]
        cli.fout.write('%s\n' % ', '.join(port_list))
    else:
        cli.fout.write('(none)\n')

    cli.fout.write('  Active modules: ')
    if modules:
        module_list = []
        for m in modules:
            if 'desc' in m:
                module_list.append('%s::%s(%s)' % \
                        (m.name, m.mclass, m.desc))
            else:
                module_list.append('%s::%s' % (m.name, m.mclass))

        cli.fout.write('%s\n' % ', '.join(module_list))
    else:
        cli.fout.write('(none)\n')

# last_stats: a map of (node name, gateid) -> (timestamp, counter value)
def _draw_pipeline(cli, field, last_stats = None):
    modules = sorted(cli.bess.list_modules())
    names = []
    node_labels = {}

    for m in modules:
        name = m.name
        mclass = m.mclass
        names.append(name)
        node_labels[name] = '%s\\n%s' % (name, mclass)
        if 'desc' in m:
            node_labels[name] += '\\n%s' % m.desc
        else:
            node_labels[name] += '\\n-'

    port_inc_list = []

    try:
        f = subprocess.Popen('graph-easy', shell=True,
                stdin=subprocess.PIPE, 
                stdout=subprocess.PIPE, 
                stderr=subprocess.PIPE)

        for m in modules:
            print >> f.stdin, '[%s]' % node_labels[m.name]

        for name in names:
            gates = cli.bess.get_module_info(name).ogates

            for gate in gates:
                if last_stats is not None:
                    last_time, last_val = last_stats[(name, gate.ogate)]
                    new_time, new_val = gate.timestamp, gate[field]
                    last_stats[(name, gate.ogate)] = (new_time, new_val)

                    val = int((new_val - last_val) / (new_time - last_time))
                else:
                    val = gate[field]

                edge_attr = '{label::%d  %d  %d:;}' % \
                        (gate.ogate, val, gate.igate)

                print >> f.stdin, '[%s] ->%s [%s]' % (
                        node_labels[name],
                        edge_attr,
                        node_labels[gate.name],
                    )
        output, error = f.communicate()
        f.wait()
        return output

    except IOError as e:
        if e.errno == errno.EPIPE:
            raise cli.CommandError('"graph-easy" program is not availabe? ' \
                    'Check if the package "libgraph-easy-perl" is installed.')
        else:
            raise

@cmd('show pipeline', 'Show the current datapath pipeline')
def show_pipeline(cli):
    cli.fout.write(_draw_pipeline(cli, 'pkts'))

@cmd('show pipeline batch', 
        'Show the current datapath pipeline with batch counters')
def show_pipeline_batch(cli):
    cli.fout.write(_draw_pipeline(cli, 'cnt'))

def _group(number):
    s = str(number)
    groups = []
    while s and s[-1].isdigit():
        groups.append(s[-3:])
        s = s[:-3]
    return s + ','.join(reversed(groups))

def _show_port(cli, port):
    cli.fout.write('  %s/%s\n' % (port.name, port.driver))
    
    port_stats = cli.bess.get_port_stats(port.name)

    cli.fout.write('    Incoming (external -> BESS):\n')
    cli.fout.write('      packets: %s\n' % _group(port_stats.inc.packets))
    cli.fout.write('      dropped: %s\n' % _group(port_stats.inc.dropped))
    cli.fout.write('      bytes  : %s\n' % _group(port_stats.inc.bytes))

    cli.fout.write('    Outgoing (BESS -> external):\n')
    cli.fout.write('      packets: %s\n' % _group(port_stats.out.packets))
    cli.fout.write('      dropped: %s\n' % _group(port_stats.out.dropped))
    cli.fout.write('      bytes  : %s\n' % _group(port_stats.out.bytes))

@cmd('show port', 'Show the status of all ports')
def show_port_all(cli):
    ports = cli.bess.list_ports()

    if not ports:
        raise cli.CommandError('There is no active port to show.')
    else:
        for port in ports:
            _show_port(cli, port)

@cmd('show port PORT...', 'Show the status of spcified ports')
def show_port_list(cli, port_names):
    ports = cli.bess.list_ports()

    port_names = list(set(port_names))
    for port_name in port_names:
        for port in ports:
            if port_name == port.name:
                _show_port(cli, port)
                break
        else:
            raise cli.CommandError('Port "%s" doest not exist' % port_name)

def _show_module(cli, module):
    info = cli.bess.get_module_info(module.name)

    cli.fout.write('  %s::%s' % (info.name, info.mclass))

    if 'desc' in info:
        cli.fout.write(' (%s)\n' % info.desc)
    else:
        cli.fout.write('\n')

    if info.igates:
        cli.fout.write('    Input gates:\n')
        for gate in info.igates:
            cli.fout.write('      %5d: %s\n' % \
                    (gate.igate, 
                     ', '.join('%s:%d ->' % (g.name, g.ogate) \
                             for g in gate.ogates)))
    else:
        cli.fout.write('    No input gate\n')

    if info.ogates:
        cli.fout.write('    Output gates:\n')
        for gate in info.ogates:
            cli.fout.write(
                    '      %5d: batches %-16d packets %-16d -> %d:%s\n' % \
                    (gate.ogate, gate.cnt, gate.pkts, 
                     gate.igate, gate.name))
    else:
        cli.fout.write('    No output gate\n')

    if 'dump' in info:
        cli.fout.write('    Dump:\n')
        cli.fout.write('      %s' % info.dump)

@cmd('show module', 'Show the status of all modules')
def show_module_all(cli):
    modules = cli.bess.list_modules()

    if not modules:
        raise cli.CommandError('There is no active module to show.')

    for module in modules:
        _show_module(cli, module)

@cmd('show module MODULE...', 'Show the status of specified modules')
def show_module_list(cli, module_names):
    modules = cli.bess.list_modules()

    for module_name in module_names:
        for module in modules:
            if module_name == module.name:
                _show_module(cli, module)
                break
        else:
            raise cli.CommandError('Module "%s" doest not exist' % module_name)

def _monitor_pipeline(cli, field):
    modules = sorted(cli.bess.list_modules())
   
    last_stats = {}
    for module in modules:
        gates = cli.bess.get_module_info(module.name).ogates

        for gate in gates:
            last_stats[(module.name, gate.ogate)] = \
                    (gate.timestamp, gate[field])

    try:
        while True:
            time.sleep(1)
            cli.fout.write(_draw_pipeline(cli, field, last_stats))
            cli.fout.write('\n')
    except KeyboardInterrupt:
        pass

@cmd('monitor pipeline', 'Monitor packet counters in the datapath pipeline')
def monitor_pipeline(cli):
    _monitor_pipeline(cli, 'pkts')

@cmd('monitor pipeline batch', 'Monitor batch counters in the datapath pipeline')
def monitor_pipeline_batch(cli):
    _monitor_pipeline(cli, 'cnt')

def _monitor_ports(cli, *ports):

    def get_delta(old, new):
        sec_diff = new.timestamp - old.timestamp
        return (new - old) / sec_diff

    def print_header(timestamp):
        print
        print '%-20s%14s%10s%10s        %14s%10s%10s' % \
                (time.strftime('%X') + str(timestamp % 1)[1:8], \
                 'INC     Mbps', 'Mpps', 'dropped', \
                 'OUT     Mbps', 'Mpps', 'dropped')

        print '-' * 96

    def print_footer():
        print '-' * 96

    def print_delta(port, delta):
        print '%-20s%14.1f%10.3f%10d        %14.1f%10.3f%10d' % (port, 
                (delta.inc.bytes + delta.inc.packets * 24) * 8 / 1e6,
                delta.inc.packets / 1e6,
                delta.inc.dropped,
                (delta.out.bytes + delta.out.packets * 24) * 8 / 1e6,
                delta.out.packets / 1e6,
                delta.out.dropped,
            )

    def get_total(arr):
        total = copy.deepcopy(arr[0])
        for stat in arr[1:]:
            for pdir in ('inc', 'out'):
                for key in total[pdir]:
                    total[pdir][key] += stat[pdir][key]
        return total

    all_ports = sorted(cli.bess.list_ports())
    drivers = {}
    for port in all_ports:
        drivers[port.name] = port.driver

    if not ports:
        ports = [port.name for port in all_ports]
        if not ports:
            raise cli.CommandError('No port to monitor')

    cli.fout.write('Monitoring ports: %s\n' % ', '.join(ports))

    last = {}
    now = {}

    for port in ports:
        last[port] = cli.bess.get_port_stats(port)
    
    try:
        while True:
            time.sleep(1)

            for port in ports:
                now[port] = cli.bess.get_port_stats(port)

            print_header(now[port].timestamp)

            for port in ports:
                print_delta('%s/%s' % (port, drivers[port]), 
                        get_delta(last[port], now[port]))

            print_footer()

            if len(ports) > 1:
                print_delta('Total', get_delta(
                        get_total(last.values()),
                        get_total(now.values())))

            for port in ports:
                last[port] = now[port]
    except KeyboardInterrupt:
        pass

@cmd('monitor port', 'Monitor the current traffic of all ports')
def monitor_port_all(cli):
    _monitor_ports(cli)

@cmd('monitor port PORT...', 'Monitor the current traffic of specified ports')
def monitor_port_all(cli, ports):
    _monitor_ports(cli, *ports)

def _monitor_tcs(cli, *tcs):
    def get_delta(old, new):
        sec_diff = new.timestamp - old.timestamp
        return (new - old) / sec_diff

    def print_header(timestamp):
        print
        print '%-20s%12s%12s%12s%12s%12s%12s' % \
                (time.strftime('%X') + str(timestamp % 1)[1:8], \
                 'CPU MHz', 'scheduled', 'Mpps', 'Mbps', 
                 'pkts/batch', 'cycles/p')

        print '-' * 92

    def print_footer():
        print '-' * 92

    def print_delta(tc, delta):
        if delta.count >= 1:
            ppb = delta.packets / delta.count
        else:
            ppb = 0

        if delta.packets >= 1:
            cpp = delta.cycles / delta.packets
        else:
            cpp = 0

        print '%-20s%12.3f%12d%12.3f%12.3f%12.3f%12.3f' % (tc, 
                delta.cycles / 1e6, 
                delta.count, 
                delta.packets / 1e6, 
                delta.bits / 1e6,
                ppb,
                cpp)


    def get_total(arr):
        total = copy.deepcopy(arr[0])

        for stat in arr[1:]:
            for key in total._keys():
                if key != 'timestamp':
                    total[key] += stat[key]

        return total

    all_tcs = cli.bess.list_tcs()
    wids = {}
    for tc in all_tcs:
        wids[tc.name] = tc.wid

    if not tcs:
        tcs = [tc.name for tc in all_tcs]
        if not tcs:
            raise cli.CommandError('No traffic class to monitor')

    cli.fout.write('Monitoring traffic classes: %s\n' % ', '.join(tcs))

    last = {}
    now = {}

    for tc in tcs:
        last[tc] = cli.bess.get_tc_stats(tc)

    try:
        while True:
            time.sleep(1)

            for tc in tcs:
                now[tc] = cli.bess.get_tc_stats(tc)

            print_header(now[tc].timestamp)

            for tc in tcs:
                print_delta('W%d %s' % (wids[tc], tc), 
                        get_delta(last[tc], now[tc]))

            print_footer()

            if len(tcs) > 1:
                print_delta('Total', get_delta(
                        get_total(last.values()),
                        get_total(now.values())))

            for tc in tcs:
                last[tc] = now[tc]
    except KeyboardInterrupt:
        pass

@cmd('monitor tc', 'Monitor the statistics of all traffic classes')
def monitor_tc_all(cli):
    _monitor_tcs(cli)

@cmd('monitor tc TC...', 'Monitor the statistics of specified traffic classes')
def monitor_tc_all(cli, tcs):
    _monitor_tcs(cli, *tcs)

# tcpdump can write pcap files, so we don't need to support it separately
@cmd('tcpdump MODULE [OGATE] [TCPDUMP_OPTS...]', 'Capture packets on a gate')
def tcpdump_module(cli, module_name, ogate, opts):
    if ogate is None:
        ogate = 0

    if opts is None:
        opts = []

    fifo = tempfile.mktemp()
    os.mkfifo(fifo, 0600)   # random people should not see packets...

    fd = os.open(fifo, os.O_RDWR)

    tcpdump_cmd = ['tcpdump']
    tcpdump_cmd.extend(['-r', fifo])
    tcpdump_cmd.extend(opts)
    tcpdump_cmd = ' '.join(tcpdump_cmd)

    cli.fout.write('  Running: %s\n' % tcpdump_cmd)
    proc = subprocess.Popen(tcpdump_cmd, shell=True, preexec_fn = os.setsid)

    cli.bess.pause_all()
    try:
        cli.bess.enable_tcpdump(fifo, module_name, ogate)
    finally:
        cli.bess.resume_all()

    try:
        proc.wait()
    except KeyboardInterrupt:
        # kill all descendants in the process group
        os.killpg(proc.pid, signal.SIGTERM)
    finally:
        cli.bess.pause_all()
        try:
            cli.bess.disable_tcpdump(module_name, ogate)
        finally:
            cli.bess.resume_all()

        try:
            os.close(fd)
            os.unlink(fifo)
            os.system('stty sane')  # more/less may have screwed the terminal
        except:
            pass
