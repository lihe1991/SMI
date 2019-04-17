#pragma OPENCL EXTENSION cl_intel_channels : enable

#include "smi_broadcast.h"



__kernel void app(const int N, char root,char my_rank, char num_ranks)
{
    SMI_BChannel  __attribute__((register)) chan= SMI_Open_bcast_channel(N, SMI_FLOAT, root,my_rank,num_ranks);
    //printf("Size of: %u\n",sizeof(chan));
    for(int i=0;i<N;i++)
    {
    	float to_comm=(float)i;
        SMI_Bcast(&chan,&to_comm);
     //   acc+=to_comm;
        //if(i!=to_comm)
        //	printf("Rank %d received %d while I was expecting %d\n",my_rank,i,to_comm);
    }
}
