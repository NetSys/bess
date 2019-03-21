// colorscheme:
// #01295f cool black
// #437f97 queen blue
// #849324 olive drab
// #ffb30f dark tangerine
// #fd151b vivid red

// (node name, gate type, gate ID) -> [(timestamp, pkts, bits, cnt), ...]
var stats = {};

var opt_field;
var opt_mode;
var opt_humanreadable;

function gates_to_str(gates, gate_type) {
    var ret = '';

    for (var i = 0; i < gates.length; i++) {
        var gate_num = gates[i][gate_type]
        if (gate_type == 'igate') {
            color = '#437f97'
        } else {
            color = '#01295f'
        }
        ret += `<td port="${gate_type}${gate_num}" border="0" cellpadding="0" bgcolor="${color}"><font color="#ffffff" point-size="6">${gate_num}</font></td>
`;
    }

    return `
      <tr>
        <td border="0" cellspacing="0" cellpadding="0">
          <table border="0" cellborder="0" cellspacing="0" cellpadding="0">
            <tr>
              ${ret}
            </tr>
          </table>
        </td>
      </tr>`;
}

function add_datapoints(stats, module_name, gates, gate_type) {
    for (var i = 0; i < gates.length; i++) {
        var gate = gates[i];
        var key = [module_name, gate_type, gate[gate_type]];
        var value = {timestamp: gate.timestamp,
                 bits: Number(gate.bytes * 8),
                 pkts: Number(gate.pkts),
                 cnt: Number(gate.cnt),
                 batchsize: gate.cnt ? gate.pkts / gate.cnt : 0};
        if (!(key in stats)) {
            stats[key] = [value];
        } else {
            stats[key].push(value);
        }
    }
}

function get_edge_label(stats) {
    var num_stats = stats.length;
    var value = stats[num_stats - 1];
    var label = '?'

    if (value.timestamp > 0) {
        switch (opt_mode) {
            case 'total':
                label = value[opt_field];
                break;
            case 'rate':
                if (num_stats >= 2) {
                    var last = stats[num_stats - 2];
                    var time_diff = value.timestamp - last.timestamp;
                    if (opt_field == 'batchsize') {
                        var packets = value.pkts - last.pkts;
                        var batches = value.cnt - last.cnt;
                        label = batches ? packets / batches : 'N/A';
                    } else {
                        var value_diff = value[opt_field] - last[opt_field];
                        label = Math.round(value_diff / time_diff);
                    }
                }
                break;
            case 'none':
                return '';
            default:
                throw new Error('Unknown mode ' + opt_mode);
        }
    }

    if ((typeof label == 'number') && opt_humanreadable) {
        var unit = ' ';
        if (opt_mode == 'rate') {
            if (label > 1000000000) {
                label /= 1000000000;
                unit += 'G';
            } else if (label > 1000000) {
                label /= 1000000;
                unit += 'M';
            } else if (label > 1000) {
                label /= 1000;
                unit += 'k';
            }
            if (opt_field == 'pkts') {
                unit += 'pps';
            } else if (opt_field == 'bits') {
                unit += 'bps';
            }
        }
        label = label.toLocaleString('en-US', {maximumFractionDigits: 2}) + unit;
    }

    // We need this HTML hack to give background color to edge labels
    return `<<table border="0" cellpadding="0"><tr><td bgcolor="white">${label}</td></tr></table>>`;
}

function graph_to_dot(modules) {
    opt_field = document.querySelector('input[name="metric"]:checked').value;
    opt_mode = document.querySelector('input[name="mode"]:checked').value;
    opt_humanreadable = document.querySelector('input[name="humanreadable"]').checked;

    var nodes = '';
    for (var module_name in modules) {
	var module = modules[module_name];
	// no need to collect igate data since we don't show them yet.
        // add_datapoints(stats, module_name, module.igates, 'igate')
        add_datapoints(stats, module_name, module.ogates, 'ogate')

        module.show_igates = module.igates.length > 1 ||
            (module.igates.length == 1 && module.igates[0].igate != 0);
        module.show_ogates = module.ogates.length > 1 ||
            (module.ogates.length == 1 && module.ogates[0].ogate != 0);

        var desc = module.desc ? `<font point-size="9">${module.desc}</font>` : '';
        var igates = module.show_igates ? gates_to_str(module.igates, 'igate') : '';
        var ogates = module.show_ogates ? gates_to_str(module.ogates, 'ogate') : '';

        nodes += `
  ${module_name} [shape=plaintext label=
    <<table port="mod" border="1" cellborder="0" cellspacing="0" cellpadding="1">
      ${igates}<tr>
        <td width="60">${module_name}</td>
      </tr>
      <tr>
        <td><font color="#888888" point-size="9"><i>${module.mclass}</i></font></td>
      </tr>
      <tr>
        <td>${desc}</td>
      </tr>
      ${ogates}</table>>];
`;
    }

    var edges = '';
    for (module_name in modules) {
        var module = modules[module_name];
        for (var i = 0; i < module.ogates.length; i++) {
            var gate = module.ogates[i];
            var dst_module = modules[gate.name];
            var out_port = module.show_ogates ? `ogate${gate.ogate}:s` : 'mod';
            var in_port = dst_module.show_igates ? `igate${gate.igate}:n` : 'mod';

            var label = get_edge_label(stats[[module_name, 'ogate', gate.ogate]]);
            if (label != '') {
                label = ` [label=${label}]`;
            }

            edges += `  "${module_name}":${out_port} -> "${gate.name}":${in_port}${label}\n`;
        }
    }

    return `digraph G {
  graph [ rankdir=TB ];
  node [ fontsize=12 ];
  edge [ fontsize=9, color="#ffb30f", arrowsize=0.5, labeldistance=1.2 ];
${nodes}
${edges}
}
`
}
