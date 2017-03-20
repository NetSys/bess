import sys
import os

#for pretty printing
class ColorizedOutput(object):
    def __init__(self, orig_out, color):
        self.orig_out = orig_out
        self.color = color

    def __getattr__(self, attr):
        def_color = '\033[0;0m'  # resets all terminal attributes

        if attr == 'write':
            return lambda x: self.orig_out.write(self.color + x + def_color)
        else:
            return getattr(self.orig_out, attr)

class CLI(object):
    # general command errors
    class CommandError(Exception):
        pass

    class HandledError(Exception):
        pass

    class InvalidCommandError(Exception):
        pass

    # variable binding errors
    class BindError(Exception):
        pass

    # some internal logic errors that might be your (or my) fault
    class InternalError(Exception):
        pass

    def __init__(self, cmdlist, fin=sys.stdin, fout=sys.stdout, ferr=None,
                 interactive=None, history_file=None):
        self.cmdlist = cmdlist

        self.fin = fin
        self.fout = fout
        self.last_cmd = ''
        self.rl = None

        if history_file is None:
            try:
                self.history_file = os.path.expanduser('~/.bess_history')
            except:
                self.history_file = None
        else:
            self.history_file = history_file

        # Colorize output to standard error
        if ferr is None:
            if sys.stderr.isatty():
                self.ferr = ColorizedOutput(sys.stderr, '\033[31m')  # dark red
            else:
                self.ferr = sys.stderr
        else:
            self.ferr = ferr

        if interactive is None:
            self.interactive = fin.isatty() and fout.isatty()
        else:
            self.interactive = interactive

        if self.interactive:
            self.go_interactive()

    def err(self, msg):
        self.ferr.write('*** Error: %s\n' % msg)
        if not self.interactive:
            self.stop_loop = True

    # If not a variable, simply return None
    # Otherwise, return (var_type, desc, candidates):
    #    var_type can be: 'int', 'str', 'list'(list of strings), 'map'
    #    candidates is a list of string values.
    def get_var_attrs(self, var_token, partial_word):
        return None

    # Return (head, tail)
    #   head: consumed string portion
    #   tail: the rest of input line
    # You can assume that 'line == head + tail'
    def split_var(self, var_type, line):
        if var_type == 'keyword':
            pos = line.find(' ')
            if pos == -1:
                return line, ''
            else:
                return line[:pos], line[pos:]

        raise self.InternalError('type "%s" is undefined' % var_type)

    # Return (mapped_value, tail)
    #   mapped_value: Python value/object from the consumed token(s)
    #   tail: the rest of input line
    def bind_var(self, var_type, line):
        if var_type == 'keyword':
            return None, self.split_var(var_type, line)[1]

        raise self.InternalError('type "%s" is undefined' % var_type)

    # Compare a command with a user-typed line.
    # It returns (match_type, candidates, syntax_token, score).
    # match_type can be:
    #  - 'full': all tokens in syntax was consumed
    #  - 'partial': prefix matched
    #  - 'nonmatch': not a match
    # candidates is a list of suggested strings to be added as the last token.
    # syntax_token is where the user input is currently on, if any.
    # score is the number of matched keywords
    def match(self, syntax, line):
        candidates = []
        remainder = line
        score = 0

        new_token = (line != '' and line[-1] == ' ')

        syntax_tokens = syntax.split()

        for i, syntax_token in enumerate(syntax_tokens):
            if remainder.split():
                line_word = remainder.split()[0]
            else:
                line_word = ''

            attrs = self.get_var_attrs(syntax_token, line_word)
            if attrs:
                var_type, var_desc, var_candidates = attrs
            else:
                var_type, var_desc, var_candidates = 'keyword', '', []

            if remainder.strip() == '':
                if new_token:
                    if i == 0 or '...' not in syntax_tokens[i - 1]:
                        candidates = []

                    candidates.extend(var_candidates)

                    if var_type == 'keyword':
                        candidates.append(syntax_token)

                    if syntax_token[0] == '[':  # skippable?
                        return 'full', candidates, syntax_token, score
                    return 'partial', candidates, syntax_token, score

                return 'partial', candidates, \
                    syntax_tokens[max(0, i - 1)], score

            token, remainder = self.split_var(var_type, remainder)
            remainder = remainder.lstrip()

            if var_type == 'keyword':
                if syntax_token == token:
                    if new_token:
                        candidates = []
                    else:
                        candidates = [syntax_token]
                else:
                    if not syntax_token.startswith(token):
                        return 'nonmatch', [], '', score
                    candidates = [syntax_token]
                score += 1
            else:
                if new_token:
                    candidates = var_candidates
                else:
                    candidates = []
                    for var in var_candidates:
                        if var.startswith(token.split()[-1]):
                            candidates.append(var)

        if remainder.strip() == '':
            if '...' in syntax_token:
                return 'full', candidates, syntax_token, score
            if new_token:
                return 'full', [], '', score
            return 'full', candidates, '', score
        return 'nonmatch', [], '', score

    # filters is a list of 'full', 'partial', 'nonmatch'
    def list_matched(self, line, filters):
        matched_list = []

        for cmd in self.cmdlist:
            syntax = cmd[0]
            match_type, _, _, score = self.match(syntax, line)

            if match_type in filters:
                matched_list.append((cmd, score))

        if len(matched_list) == 0:
            return [], []

        max_score = max(map(lambda x: x[1], matched_list))

        ret = []
        ret_low = []
        for cmd, score in matched_list:
            if score == max_score:
                ret.append(cmd)
            else:
                ret_low.append(cmd)

        return ret, ret_low

    def _do_complete(self, line, partial_word):
        possible_cmds = []
        candidates = []
        num_full_matches = 0

        # ignore partial_word set by readline
        if len(line) > 0 and line[-1] != ' ':
            partial_word = line.split()[-1]
        else:
            partial_word = ''

        for cmd in self.cmdlist:
            syntax = cmd[0]
            match_type, sub_candidates, \
                syntax_token, score = self.match(syntax, line)

            if match_type in ['full', 'partial']:
                possible_cmds.append((cmd, match_type, syntax_token))

                if match_type == 'full':
                    num_full_matches += 1

                for candidate in sub_candidates:
                    if candidate.startswith(partial_word):
                        if not candidate.endswith('/'):
                            candidate += ' '
                        candidates.append(candidate)

        candidates = sorted(list(set(candidates)))

        if candidates:
            # find the longest common prefix of all candidates
            s_min = candidates[0]
            s_max = candidates[-1]

            for i, c in enumerate(s_min):
                if i >= len(s_max) or c != s_max[i]:
                    common_prefix = s_min[:i]
                    break
            else:
                common_prefix = s_min

            if common_prefix and len(partial_word) < len(common_prefix):
                if partial_word == common_prefix[:len(partial_word)]:
                    ret = []
                    skip = partial_word.rfind('/') + 1
                    for candidate in candidates:
                        candidate = candidate[skip:]
                        ret.append(candidate)

                    return ret

        buf = []

        for cmd, match_type, syntax_token in possible_cmds:
            syntax, desc, _ = cmd

            if match_type == 'full' and num_full_matches == 1:
                buf.append('  %-50s%s\n' % (syntax + ' <enter>', desc))
            else:
                buf.append('  %-50s%s\n' % (syntax, desc))

            if syntax_token:
                attrs = self.get_var_attrs(syntax_token, partial_word)
                if attrs:
                    var_type, var_desc, var_candidates = attrs
                    buf.append('    %s (%s): %s\n' %
                               (syntax_token, var_type, var_desc))

                    eligible = []
                    for var in var_candidates:
                        if var.startswith(partial_word):
                            buf.append('      %s\n' % var)

        if buf:
            self.fout.write('\n')
            self.fout.write(''.join(buf))
            self.fout.write('%s%s' % (self.get_prompt(), line))

        return []

    def complete(self, partial_word, state):
        if state == 0:
            line = self.rl.get_line_buffer()

            # We currently support auto completion only at the EOL
            if len(line) != self.rl.get_endidx():
                return None

            # All exceptions happening here is ignored by the caller,
            # so we add our exception handler for debugging
            try:
                self.candidates = self._do_complete(line, partial_word)
            except BaseException as e:
                import traceback
                traceback.print_exc()
                sys.exit(1)

        try:
            return self.candidates[state]
        except IndexError:
            return None

    def complete_dummy(self, partial_word, state):
        return None

    def get_prompt(self):
        return '> '

    def find_cmd(self, line):
        matched, matched_low = self.list_matched(line, ['full'])

        if len(matched) == 1:
            return matched[0]

        elif len(matched) >= 2:
            self.err('Ambiguous command "%s". Candidates:' % line.strip())
            for cmd, desc, _ in matched + matched_low:
                self.ferr.write('  %-50s%s\n' % (cmd, desc))

        elif len(matched) == 0:
            matched, matched_low = self.list_matched(line, ['partial'])
            if len(matched) > 0:
                self.err('Incomplete command "%s". Candidates:' % line.strip())
                for cmd, desc, _ in matched + matched_low:
                    self.ferr.write('  %-50s%s\n' % (cmd, desc))
            else:
                self.err('Unknown command "%s".' % line.strip())

        raise self.InvalidCommandError()

    def get_default_args(self):
        return []

    def bind_args(self, cmd, line):
        syntax, desc, func = cmd
        remainder = line
        args = []

        for i, syntax_token in enumerate(syntax.split()):
            if remainder.strip() == '':
                if syntax_token[0] == '[':
                    args.append(None)
                    continue

                raise self.InternalError('Partial match on "%s"? line: "%s"' %
                                         (syntax, line))

            attrs = self.get_var_attrs(syntax_token, remainder.split()[0])
            if attrs:
                var_type = attrs[0]
            else:
                var_type = 'keyword'

            val, remainder = self.bind_var(var_type, remainder)

            if var_type != 'keyword':
                args.append(val)

            remainder = remainder.lstrip()

        args = self.get_default_args() + args
        return func, args

    def call_func(self, func, args):
        func(*args)

    def print_banner(self):
        pass

    def process_one_line(self):
        if self.interactive:
            try:
                line = raw_input(self.get_prompt())
            except KeyboardInterrupt:
                self.fout.write('\n')
                return
        else:
            line = self.fin.readline()
            if len(line) == 0:
                raise EOFError()

        line = line.strip()

        if line:
            self.last_cmd = line
            try:
                try:
                    cmd = self.find_cmd(line + ' ')
                    func, args = self.bind_args(cmd, line)
                    self.call_func(func, args)
                except:
                    if not self.interactive:
                        self.stop_loop = True
                    raise

            except self.HandledError:
                pass

            except self.InvalidCommandError:
                pass

            except self.BindError as e:
                self.err(e)

            except self.CommandError as e:
                self.err(e)

    def save_history(self):
        if self.interactive and self.rl and self.history_file:
            try:
                self.rl.write_history_file(self.history_file)
            except:
                self.err('Cannot write to history file "%s"' %
                         self.history_file)

    def disable_echoctl(self):
        try:
            # termios module might not be available. Ignore ImportError if so.
            import termios

            self.old_flags = termios.tcgetattr(sys.stdin)
            new_flags = self.old_flags
            new_flags[3] &= ~termios.ECHOCTL
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, new_flags)
        except:
            pass

    def restore_echoctl(self):
        try:
            cur_flags = termios.tcgetattr(sys.stdin)
            new_flags = cur_flags
            if self.old_flags[3] & termios.ECHOCTL:
                new_flags[3] |= termios.ECHOCTL
            else:
                new_flags[3] &= ~termios.ECHOCTL
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, new_flags)
        except:
            pass

    def go_interactive(self):
        try:
            import readline
            self.rl = readline
        except ImportError:
            self.err('"readline" not available. No auto completion.\n')
            return

        if 'libedit' in self.rl.__doc__:
            self.rl.parse_and_bind('bind -e')
            self.rl.parse_and_bind("bind '\t' rl_complete")
        else:
            self.rl.parse_and_bind('tab: complete')

        self.rl.set_completer(self.complete)

        try:
            if self.history_file and os.path.exists(self.history_file):
                self.rl.read_history_file(self.history_file)
        except:
            self.err('Cannot read from history file "%s"' %
                        self.history_file)

        self.print_banner()
        self.fout.flush()

    def loop(self):
        self.disable_echoctl()

        try:
            self.stop_loop = False

            # the main command loop
            while not self.stop_loop:
                self.process_one_line()
        except EOFError:
            if self.interactive:
                self.fout.write('\n')
        finally:
            self.save_history()
            self.restore_echoctl()
