#define BUFFER_SIZE 512

#include "smi/channel_helpers.h"

// the maximum number of consecutive reads that each CKs/CKr can do from the same channel
#define READS_LIMIT 8
// maximum number of ranks in the cluster
#define MAX_RANKS 8

// QSFP channelsf
#ifndef SMI_EMULATION_RANK
channel SMI_Network_message io_out_0 __attribute__((depth(16))) __attribute__((io("kernel_output_ch0")));
channel SMI_Network_message io_in_0 __attribute__((depth(16))) __attribute__((io("kernel_input_ch0")));
channel SMI_Network_message io_out_1 __attribute__((depth(16))) __attribute__((io("kernel_output_ch1")));
channel SMI_Network_message io_in_1 __attribute__((depth(16))) __attribute__((io("kernel_input_ch1")));
channel SMI_Network_message io_out_2 __attribute__((depth(16))) __attribute__((io("kernel_output_ch2")));
channel SMI_Network_message io_in_2 __attribute__((depth(16))) __attribute__((io("kernel_input_ch2")));
channel SMI_Network_message io_out_3 __attribute__((depth(16))) __attribute__((io("kernel_output_ch3")));
channel SMI_Network_message io_in_3 __attribute__((depth(16))) __attribute__((io("kernel_input_ch3")));
#else
#if SMI_EMULATION_RANK == 0
channel SMI_Network_message io_out_0 __attribute__((depth(16))) __attribute__((io("emulated_channel_r0c0_r6c1")));
channel SMI_Network_message io_in_0 __attribute__((depth(16))) __attribute__((io("emulated_channel_r6c1_r0c0")));
channel SMI_Network_message io_out_1 __attribute__((depth(16))) __attribute__((io("emulated_channel_r0c1_r2c0")));
channel SMI_Network_message io_in_1 __attribute__((depth(16))) __attribute__((io("emulated_channel_r2c0_r0c1")));
channel SMI_Network_message io_out_2 __attribute__((depth(16))) __attribute__((io("emulated_channel_r0c2_r1c3")));
channel SMI_Network_message io_in_2 __attribute__((depth(16))) __attribute__((io("emulated_channel_r1c3_r0c2")));
channel SMI_Network_message io_out_3 __attribute__((depth(16))) __attribute__((io("emulated_channel_r0c3_r1c2")));
channel SMI_Network_message io_in_3 __attribute__((depth(16))) __attribute__((io("emulated_channel_r1c2_r0c3")));
#endif
#endif

/**
  These tables, defined at compile time, maps application endpoints (Port) to channels and are
  used by the compiler to lay down the circuitry. The data routing table is used by push (and collectives)
  to send the actual communication data, while the control is used by push (and collective) to receive
  control information (e.g. rendezvous data) from the pairs. There are also otehr channels for collective operations.
*/

// cks_data: logical port -> index in cks_data_table -> index in cks_data_channels
__constant char cks_data_table[2] = { 0, 1 };
channel SMI_Network_message cks_data_channels[2] __attribute__((depth(16)));

// cks_control: logical port -> index in cks_control_table -> index in cks_control_channels
__constant char cks_control_table[2] = { -1, -1 };
channel SMI_Network_message cks_control_channels[0] __attribute__((depth(16)));

// ckr_data: logical port -> index in ckr_data_table -> index in ckr_data_channels
__constant char ckr_data_table[2] = { -1, -1 };
channel SMI_Network_message ckr_data_channels[0] __attribute__((depth(BUFFER_SIZE)));

// ckr_control: logical port -> index in ckr_control_table -> index in ckr_control_channels
__constant char ckr_control_table[2] = { 0, 1 };
channel SMI_Network_message ckr_control_channels[2] __attribute__((depth(BUFFER_SIZE)));


// broadcast: logical port -> index in broadcast_table -> index in broadcast_channels
__constant char broadcast_table[2] = { -1, -1 };
channel SMI_Network_message broadcast_channels[0] __attribute__((depth(2)));


// reduce_send: logical port -> index in reduce_send_table -> index in reduce_send_channels
__constant char reduce_send_table[2] = { -1, -1 };
channel SMI_Network_message reduce_send_channels[0] __attribute__((depth(1)));

// reduce_recv: logical port -> index in reduce_recv_table -> index in reduce_recv_channels
__constant char reduce_recv_table[2] = { -1, -1 };
channel SMI_Network_message reduce_recv_channels[0] __attribute__((depth(1)));


// scatter: logical port -> index in scatter_table -> index in scatter_channels
__constant char scatter_table[2] = { -1, -1 };
channel SMI_Network_message scatter_channels[0] __attribute__((depth(2)));

// gather: logical port -> index in gather_table -> index in gather_channels
__constant char gather_table[2] = { -1, -1 };
channel SMI_Network_message gather_channels[0] __attribute__((depth(2)));


__constant char QSFP_COUNT = 4;

// connect all CK_S together
channel SMI_Network_message channels_interconnect_ck_s[QSFP_COUNT*(QSFP_COUNT-1)] __attribute__((depth(16)));

// connect all CK_R together
channel SMI_Network_message channels_interconnect_ck_r[QSFP_COUNT*(QSFP_COUNT-1)] __attribute__((depth(16)));

// connect corresponding CK_S/CK_R pairs
channel SMI_Network_message channels_interconnect_ck_s_to_ck_r[QSFP_COUNT] __attribute__((depth(16)));

// connect corresponding CK_R/CK_S pairs
channel SMI_Network_message channels_interconnect_ck_r_to_ck_s[QSFP_COUNT] __attribute__((depth(16)));

#include "smi/pop.h"
#include "smi/push.h"
#include "smi/bcast.h"
#include "smi/reduce.h"
#include "smi/scatter.h"
#include "smi/gather.h"

__kernel void smi_kernel_cks_0(__global volatile char *restrict rt, const char num_ranks)
{
    char external_routing_table[MAX_RANKS];
    for (int i = 0; i < MAX_RANKS; i++)
    {
        if (i < num_ranks)
        {
            external_routing_table[i] = rt[i];
        }
    }

    // number of CK_S - 1 + CK_R + 1 CKS hardware ports
    const char num_sender = 5;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;

    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // receive from CK_S_1
                message = read_channel_nb_intel(channels_interconnect_ck_s[0], &valid);
                break;
            case 1:
                // receive from CK_S_2
                message = read_channel_nb_intel(channels_interconnect_ck_s[1], &valid);
                break;
            case 2:
                // receive from CK_S_3
                message = read_channel_nb_intel(channels_interconnect_ck_s[2], &valid);
                break;
            case 3:
                // receive from CK_R_0
                message = read_channel_nb_intel(channels_interconnect_ck_r_to_ck_s[0], &valid);
                break;
            case 4:
                // receive from app channel with logical port 0, hardware port 0, method data
                message = read_channel_nb_intel(cks_data_channels[0], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char idx = external_routing_table[GET_HEADER_DST(message.header)];
            switch (idx)
            {
                case 0:
                    // send to QSFP
                    write_channel_intel(io_out_0, message);
                    break;
                case 1:
                    // send to CK_R_0
                    write_channel_intel(channels_interconnect_ck_s_to_ck_r[0], message);
                    break;
                case 2:
                    // send to CK_S_1
                    write_channel_intel(channels_interconnect_ck_s[3], message);
                    break;
                case 3:
                    // send to CK_S_2
                    write_channel_intel(channels_interconnect_ck_s[6], message);
                    break;
                case 4:
                    // send to CK_S_3
                    write_channel_intel(channels_interconnect_ck_s[9], message);
                    break;
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
__kernel void smi_kernel_ckr_0(__global volatile char *restrict rt, const char rank)
{
    // rt contains intertwined (dp0, cp0, dp1, cp1, ...)
    char external_routing_table[2 /* logical port count */][2];
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            external_routing_table[i][j] = rt[i * 2 + j];
        }
    }

    // QSFP + number of CK_Rs - 1 + CK_S
    const char num_sender = 5;
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
                message = read_channel_nb_intel(io_in_0, &valid);
                break;
            case 1:
                // receive from CK_R_1
                message = read_channel_nb_intel(channels_interconnect_ck_r[0], &valid);
                break;
            case 2:
                // receive from CK_R_2
                message = read_channel_nb_intel(channels_interconnect_ck_r[1], &valid);
                break;
            case 3:
                // receive from CK_R_3
                message = read_channel_nb_intel(channels_interconnect_ck_r[2], &valid);
                break;
            case 4:
                // receive from CK_S_0
                message = read_channel_nb_intel(channels_interconnect_ck_s_to_ck_r[0], &valid);
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
                    // send to CK_S_0
                    write_channel_intel(channels_interconnect_ck_r_to_ck_s[0], message);
                    break;
                case 1:
                    // send to CK_R_1
                    write_channel_intel(channels_interconnect_ck_r[3], message);
                    break;
                case 2:
                    // send to CK_R_2
                    write_channel_intel(channels_interconnect_ck_r[6], message);
                    break;
                case 3:
                    // send to CK_R_3
                    write_channel_intel(channels_interconnect_ck_r[9], message);
                    break;
                case 4:
                    // send to app channel with logical port 0, hardware port 0, method control
                    write_channel_intel(ckr_control_channels[0], message);
                    break;
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
__kernel void smi_kernel_cks_1(__global volatile char *restrict rt, const char num_ranks)
{
    char external_routing_table[MAX_RANKS];
    for (int i = 0; i < MAX_RANKS; i++)
    {
        if (i < num_ranks)
        {
            external_routing_table[i] = rt[i];
        }
    }

    // number of CK_S - 1 + CK_R + 1 CKS hardware ports
    const char num_sender = 5;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;

    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // receive from CK_S_0
                message = read_channel_nb_intel(channels_interconnect_ck_s[3], &valid);
                break;
            case 1:
                // receive from CK_S_2
                message = read_channel_nb_intel(channels_interconnect_ck_s[4], &valid);
                break;
            case 2:
                // receive from CK_S_3
                message = read_channel_nb_intel(channels_interconnect_ck_s[5], &valid);
                break;
            case 3:
                // receive from CK_R_1
                message = read_channel_nb_intel(channels_interconnect_ck_r_to_ck_s[1], &valid);
                break;
            case 4:
                // receive from app channel with logical port 1, hardware port 1, method data
                message = read_channel_nb_intel(cks_data_channels[1], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char idx = external_routing_table[GET_HEADER_DST(message.header)];
            switch (idx)
            {
                case 0:
                    // send to QSFP
                    write_channel_intel(io_out_1, message);
                    break;
                case 1:
                    // send to CK_R_1
                    write_channel_intel(channels_interconnect_ck_s_to_ck_r[1], message);
                    break;
                case 2:
                    // send to CK_S_0
                    write_channel_intel(channels_interconnect_ck_s[0], message);
                    break;
                case 3:
                    // send to CK_S_2
                    write_channel_intel(channels_interconnect_ck_s[7], message);
                    break;
                case 4:
                    // send to CK_S_3
                    write_channel_intel(channels_interconnect_ck_s[10], message);
                    break;
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
__kernel void smi_kernel_ckr_1(__global volatile char *restrict rt, const char rank)
{
    // rt contains intertwined (dp0, cp0, dp1, cp1, ...)
    char external_routing_table[2 /* logical port count */][2];
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            external_routing_table[i][j] = rt[i * 2 + j];
        }
    }

    // QSFP + number of CK_Rs - 1 + CK_S
    const char num_sender = 5;
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
                message = read_channel_nb_intel(io_in_1, &valid);
                break;
            case 1:
                // receive from CK_R_0
                message = read_channel_nb_intel(channels_interconnect_ck_r[3], &valid);
                break;
            case 2:
                // receive from CK_R_2
                message = read_channel_nb_intel(channels_interconnect_ck_r[4], &valid);
                break;
            case 3:
                // receive from CK_R_3
                message = read_channel_nb_intel(channels_interconnect_ck_r[5], &valid);
                break;
            case 4:
                // receive from CK_S_1
                message = read_channel_nb_intel(channels_interconnect_ck_s_to_ck_r[1], &valid);
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
                    // send to CK_S_1
                    write_channel_intel(channels_interconnect_ck_r_to_ck_s[1], message);
                    break;
                case 1:
                    // send to CK_R_0
                    write_channel_intel(channels_interconnect_ck_r[0], message);
                    break;
                case 2:
                    // send to CK_R_2
                    write_channel_intel(channels_interconnect_ck_r[7], message);
                    break;
                case 3:
                    // send to CK_R_3
                    write_channel_intel(channels_interconnect_ck_r[10], message);
                    break;
                case 4:
                    // send to app channel with logical port 1, hardware port 1, method control
                    write_channel_intel(ckr_control_channels[1], message);
                    break;
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
__kernel void smi_kernel_cks_2(__global volatile char *restrict rt, const char num_ranks)
{
    char external_routing_table[MAX_RANKS];
    for (int i = 0; i < MAX_RANKS; i++)
    {
        if (i < num_ranks)
        {
            external_routing_table[i] = rt[i];
        }
    }

    // number of CK_S - 1 + CK_R + 0 CKS hardware ports
    const char num_sender = 4;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;

    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // receive from CK_S_0
                message = read_channel_nb_intel(channels_interconnect_ck_s[6], &valid);
                break;
            case 1:
                // receive from CK_S_1
                message = read_channel_nb_intel(channels_interconnect_ck_s[7], &valid);
                break;
            case 2:
                // receive from CK_S_3
                message = read_channel_nb_intel(channels_interconnect_ck_s[8], &valid);
                break;
            case 3:
                // receive from CK_R_2
                message = read_channel_nb_intel(channels_interconnect_ck_r_to_ck_s[2], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char idx = external_routing_table[GET_HEADER_DST(message.header)];
            switch (idx)
            {
                case 0:
                    // send to QSFP
                    write_channel_intel(io_out_2, message);
                    break;
                case 1:
                    // send to CK_R_2
                    write_channel_intel(channels_interconnect_ck_s_to_ck_r[2], message);
                    break;
                case 2:
                    // send to CK_S_0
                    write_channel_intel(channels_interconnect_ck_s[1], message);
                    break;
                case 3:
                    // send to CK_S_1
                    write_channel_intel(channels_interconnect_ck_s[4], message);
                    break;
                case 4:
                    // send to CK_S_3
                    write_channel_intel(channels_interconnect_ck_s[11], message);
                    break;
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
__kernel void smi_kernel_ckr_2(__global volatile char *restrict rt, const char rank)
{
    // rt contains intertwined (dp0, cp0, dp1, cp1, ...)
    char external_routing_table[2 /* logical port count */][2];
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            external_routing_table[i][j] = rt[i * 2 + j];
        }
    }

    // QSFP + number of CK_Rs - 1 + CK_S
    const char num_sender = 5;
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
                message = read_channel_nb_intel(io_in_2, &valid);
                break;
            case 1:
                // receive from CK_R_0
                message = read_channel_nb_intel(channels_interconnect_ck_r[6], &valid);
                break;
            case 2:
                // receive from CK_R_1
                message = read_channel_nb_intel(channels_interconnect_ck_r[7], &valid);
                break;
            case 3:
                // receive from CK_R_3
                message = read_channel_nb_intel(channels_interconnect_ck_r[8], &valid);
                break;
            case 4:
                // receive from CK_S_2
                message = read_channel_nb_intel(channels_interconnect_ck_s_to_ck_r[2], &valid);
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
                    // send to CK_S_2
                    write_channel_intel(channels_interconnect_ck_r_to_ck_s[2], message);
                    break;
                case 1:
                    // send to CK_R_0
                    write_channel_intel(channels_interconnect_ck_r[1], message);
                    break;
                case 2:
                    // send to CK_R_1
                    write_channel_intel(channels_interconnect_ck_r[4], message);
                    break;
                case 3:
                    // send to CK_R_3
                    write_channel_intel(channels_interconnect_ck_r[11], message);
                    break;
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
__kernel void smi_kernel_cks_3(__global volatile char *restrict rt, const char num_ranks)
{
    char external_routing_table[MAX_RANKS];
    for (int i = 0; i < MAX_RANKS; i++)
    {
        if (i < num_ranks)
        {
            external_routing_table[i] = rt[i];
        }
    }

    // number of CK_S - 1 + CK_R + 0 CKS hardware ports
    const char num_sender = 4;
    char sender_id = 0;
    SMI_Network_message message;

    char contiguous_reads = 0;

    while (1)
    {
        bool valid = false;
        switch (sender_id)
        {
            case 0:
                // receive from CK_S_0
                message = read_channel_nb_intel(channels_interconnect_ck_s[9], &valid);
                break;
            case 1:
                // receive from CK_S_1
                message = read_channel_nb_intel(channels_interconnect_ck_s[10], &valid);
                break;
            case 2:
                // receive from CK_S_2
                message = read_channel_nb_intel(channels_interconnect_ck_s[11], &valid);
                break;
            case 3:
                // receive from CK_R_3
                message = read_channel_nb_intel(channels_interconnect_ck_r_to_ck_s[3], &valid);
                break;
        }

        if (valid)
        {
            contiguous_reads++;
            char idx = external_routing_table[GET_HEADER_DST(message.header)];
            switch (idx)
            {
                case 0:
                    // send to QSFP
                    write_channel_intel(io_out_3, message);
                    break;
                case 1:
                    // send to CK_R_3
                    write_channel_intel(channels_interconnect_ck_s_to_ck_r[3], message);
                    break;
                case 2:
                    // send to CK_S_0
                    write_channel_intel(channels_interconnect_ck_s[2], message);
                    break;
                case 3:
                    // send to CK_S_1
                    write_channel_intel(channels_interconnect_ck_s[5], message);
                    break;
                case 4:
                    // send to CK_S_2
                    write_channel_intel(channels_interconnect_ck_s[8], message);
                    break;
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
__kernel void smi_kernel_ckr_3(__global volatile char *restrict rt, const char rank)
{
    // rt contains intertwined (dp0, cp0, dp1, cp1, ...)
    char external_routing_table[2 /* logical port count */][2];
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            external_routing_table[i][j] = rt[i * 2 + j];
        }
    }

    // QSFP + number of CK_Rs - 1 + CK_S
    const char num_sender = 5;
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
                message = read_channel_nb_intel(io_in_3, &valid);
                break;
            case 1:
                // receive from CK_R_0
                message = read_channel_nb_intel(channels_interconnect_ck_r[9], &valid);
                break;
            case 2:
                // receive from CK_R_1
                message = read_channel_nb_intel(channels_interconnect_ck_r[10], &valid);
                break;
            case 3:
                // receive from CK_R_2
                message = read_channel_nb_intel(channels_interconnect_ck_r[11], &valid);
                break;
            case 4:
                // receive from CK_S_3
                message = read_channel_nb_intel(channels_interconnect_ck_s_to_ck_r[3], &valid);
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
                    // send to CK_S_3
                    write_channel_intel(channels_interconnect_ck_r_to_ck_s[3], message);
                    break;
                case 1:
                    // send to CK_R_0
                    write_channel_intel(channels_interconnect_ck_r[2], message);
                    break;
                case 2:
                    // send to CK_R_1
                    write_channel_intel(channels_interconnect_ck_r[5], message);
                    break;
                case 3:
                    // send to CK_R_2
                    write_channel_intel(channels_interconnect_ck_r[8], message);
                    break;
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




