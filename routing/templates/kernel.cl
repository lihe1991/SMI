{% macro cks(program, channel, channel_count, target_index) -%}
__kernel void CK_S_{{ channel.index }}(__global volatile char *restrict rt, const int num_ranks)
{
    char external_routing_table[MAX_RANKS];
    for (int i = 0; i < MAX_RANKS; i++)
    {
        if (i < num_ranks)
        {
            external_routing_table[i] = rt[i];
        }
    }

{% set allocations = program.get_channel_allocations(channel.index)["cks"] %}
    // number of CK_S - 1 + CK_R + {{ allocations|length }} CKS hardware ports
    const char num_sender = {{ channel_count + allocations|length }};
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;

    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            {% for ck_s in channel.neighbours() %}
            case {{ loop.index0 }}:
                // receive from CK_S_{{ ck_s }}
                message = read_channel_nb_intel(channels_interconnect_ck_s[{{ (channel_count - 1) * channel.index + loop.index0 }}], &valid);
                break;
            {% endfor %}
            case {{ channel_count - 1 }}:
                // receive from CK_R_{{ channel.index }}
                message = read_channel_nb_intel(channels_interconnect_ck_r_to_ck_s[{{ channel.index }}], &valid);
                break;
            {% for (method, logical_port, hw_port) in allocations %}
            case {{ channel_count + loop.index0 }}:
                // receive from app channel with logical port {{ logical_port }}, hardware port {{ hw_port }}, method {{ method }}
            {% if method == "data" %}
                message = read_channel_nb_intel(channels_cks_data[{{ hw_port }}], &valid);
            {% else %}
                message = read_channel_nb_intel(channels_cks_control[{{ hw_port }}], &valid);
            {% endif %}
                break;
            {% endfor %}
        }

        if (valid)
        {
            contiguous_reads++;
            char idx = external_routing_table[GET_HEADER_DST(message.header)];
            switch (idx)
            {
                case 0:
                    // send to QSFP
                    write_channel_intel(io_out_{{ channel.index }}, message);
                    break;
                case 1:
                    // send to CK_R_{{ channel.index }}
                    write_channel_intel(channels_interconnect_ck_s_to_ck_r[{{ channel.index }}], message);
                    break;
                {% for ck_s in channel.neighbours() %}
                case {{ 2 + loop.index0 }}:
                    // send to CK_S_{{ ck_s }}
                    write_channel_intel(channels_interconnect_ck_s[{{ (channel_count - 1) * ck_s + target_index(ck_s, channel.index) }}], message);
                    break;
                {% endfor %}
            }
        }
        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
{%- endmacro %}

{% macro ckr(program, channel, channel_count, target_index) -%}
__kernel void CK_R_{{ channel.index }}(__global volatile char *restrict rt, const char rank)
{
    // rt contains intertwined (dp0, cp0, dp1, cp1, ...)
{% set logical_ports = program.logical_port_count %}
    char external_routing_table[{{ logical_ports }} /* logical port count */][2];
    for (int i = 0; i < {{ logical_ports }}; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            external_routing_table[i][j] = rt[i * 2 + j];
        }
    }

    // QSFP + number of CK_Rs - 1 + CK_S
    const char num_sender = {{ channel_count + 1 }};
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;
    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // QSFP
                message = read_channel_nb_intel(io_in_{{ channel.index }}, &valid);
                break;
            {% for ck_r in channel.neighbours() %}
            case {{ loop.index0 + 1 }}:
                // receive from CK_R_{{ ck_r }}
                message = read_channel_nb_intel(channels_interconnect_ck_r[{{ (channel_count - 1) * channel.index + loop.index0 }}], &valid);
                break;
            {% endfor %}
            case {{ channel_count }}:
                // receive from CK_S_{{ channel.index }}
                message = read_channel_nb_intel(channels_interconnect_ck_s_to_ck_r[{{ channel.index }}], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char dest;
            if (GET_HEADER_DST(message.header) != rank)
            {
                dest = 0;
            }
            else dest = external_routing_table[GET_HEADER_PORT(message.header)][GET_HEADER_OP(message.header) == SMI_SYNCH];

            switch (dest)
            {
                case 0:
                    // send to CK_S_{{ channel.index }}
                    write_channel_intel(channels_interconnect_ck_r_to_ck_s[{{ channel.index }}], message);
                    break;
                {% for ck_r in channel.neighbours() %}
                case {{ loop.index0 + 1 }}:
                    // send to CK_R_{{ ck_r }}
                    write_channel_intel(channels_interconnect_ck_r[{{ (channel_count - 1) * ck_r + target_index(ck_r, channel.index) }}], message);
                    break;
                {% endfor %}
                {% set allocations = program.get_channel_allocations(channel.index)["ckr"] %}
                {% for (method, logical_port, hw_port) in allocations %}
                case {{ channel_count + loop.index0 }}:
                    // send to app channel with logical port {{ logical_port }}, hardware port {{ hw_port }}, method {{ method }}
                {% if method == "data" %}
                    write_channel_intel(channels_ckr_data[{{ hw_port }}], message);
                {% else %}
                    write_channel_intel(channels_ckr_control[{{ hw_port }}], message);
                {% endif %}
                    break;
                {% endfor %}
            }
        }

        if (!valid || contiguous_reads == READS_LIMIT)
        {
            contiguous_reads = 0;
            sender_id++;
            if (sender_id == num_sender)
            {
                sender_id = 0;
            }
        }
    }
}
{%- endmacro %}
