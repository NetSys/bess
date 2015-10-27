#!/usr/bin/env python2.7
import sys
import os
import os.path
import pprint
import cStringIO

import cli
import commands

try:
    this_dir = os.path.dirname(os.path.realpath(__file__))
    sys.path.insert(1, '%s/../libbess-python' % this_dir)
    from softnic import *
except ImportError:
    print >> sys.stderr, 'Cannot import the API module (libsn-python)'

class BESSCLI(cli.CLI):
    def __init__(self, softnic, cmd_db, fin=sys.stdin, 
                 fout=sys.stdout, history_file=None):
        self.softnic = softnic
        self.cmd_db = cmd_db
        self.this_dir = this_dir

        super(BESSCLI, self).__init__(self.cmd_db.cmdlist, \
                fin=fin, fout=fout, history_file=history_file)
    
    def get_var_attrs(self, var_token):
        return self.cmd_db.get_var_attrs(self, var_token)

    def split_var(self, var_type, line):
        try:
            return self.cmd_db.split_var(self, var_type, line)
        except self.InternalError:
            return super(BESSCLI, self).split_var(var_type, line)

    def bind_var(self, var_type, line):
        try:
            return self.cmd_db.bind_var(self, var_type, line)
        except self.InternalError:
            return super(BESSCLI, self).bind_var(var_type, line)

    def print_banner(self):
        self.fout.write('Type "help" for more information.\n')

    def get_default_args(self):
        return [self]

    def call_func(self, func, args):
        try:
            super(BESSCLI, self).call_func(func, args)
        except self.softnic.APIError as e:
            self.err(e)
        except self.softnic.Error as e:
            self.err(e.errmsg)

            if e.err in errno.errorcode:
                err_code = errno.errorcode[e.err]
            else:
                err_code = '<unknown>'

            self.fout.write('  BESS daemon response - errno=%d (%s: %s)\n' % \
                    (e.err, err_code, os.strerror(e.err)))

            if e.details:
                details = pprint.pformat(e.details)
                initial_indent = '  error details: '
                subsequent_indent = ' ' * len(initial_indent)
                
                for i, line in enumerate(details.splitlines()):
                    if i == 0:
                        self.fout.write('%s%s\n' % (initial_indent, line))
                    else:
                        self.fout.write('%s%s\n' % (subsequent_indent, line))

    def loop(self):
        try:
            super(BESSCLI, self).loop()
        except socket.error as e:
            self.fout.write('\n')

            if e.errno in errno.errorcode:
                err_code = errno.errorcode[e.errno]
            else:
                err_code = '<unknown>'

            self.err('Disconnected from BESS daemon - errno=%d (%s: %s)' % \
                    (e.errno, err_code, os.strerror(e.errno)))
            self.softnic.disconnect()

    def get_prompt(self):
        if self.softnic.is_connected():
            return '%s:%d $ ' % self.softnic.peer
        else:
            return '<disconnected> $ '

def connect_softnic():
    s = SoftNIC()
    try:
        s.connect()
    except s.APIError as e:
        print >> sys.stderr, e.message
    return s

def run_cli():
    try:
        hist_file = os.path.expanduser('~/.bess_history')
        open(hist_file, 'a+').close()
    except:
        print >> sys.stderr, 'Error: Cannot open ~/.bess_history'
        hist_file = None
        raise

    s = connect_softnic()
    cli = BESSCLI(s, commands, history_file=hist_file)
    cli.loop()

def run_cmds(instream):
    s = connect_softnic()
    cli = BESSCLI(s, commands, fin=instream, history_file=None)
    cli.loop()

if __name__ == '__main__':
    if len(sys.argv) == 1:
        run_cli()
    else:
        cmds = ""
        argv = []
        for arg in sys.argv[1:]:
            if arg == '--':
                cmds = "%s%s\n"%(cmds, " ".join(argv))
                argv = []
            else:
                argv.append(arg)
        cmds = "%s%s\n"%(cmds, " ".join(argv))
        bufin = cStringIO.StringIO(cmds)
        run_cmds(bufin)
