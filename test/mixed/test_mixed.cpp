/**
    Gather
    Test must be executed with 8 ranks

    Once built, execute it with:
         env  CL_CONTEXT_EMULATOR_DEVICE_INTELFPGA=8 mpirun -np 8 ./test_gather.exe "./gather_emulator_<rank>.aocx"
 */


#include <gtest/gtest.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <utils/ocl_utils.hpp>
#include <utils/utils.hpp>
#include <limits.h>
#include <cmath>
#include <thread>
#include <future>
#include "smi_generated_host.c"
#define ROUTING_DIR "smi-routes/"
using namespace std;
std::string program_path;
int rank_count, my_rank;

cl::Platform  platform;
cl::Device device;
cl::Context context;
cl::Program program;
std::vector<cl::Buffer> buffers;
SMI_Comm comm;
//https://github.com/google/googletest/issues/348#issuecomment-492785854
#define ASSERT_DURATION_LE(secs, stmt) { \
  std::promise<bool> completed; \
  auto stmt_future = completed.get_future(); \
  std::thread([&](std::promise<bool>& completed) { \
    stmt; \
    completed.set_value(true); \
  }, std::ref(completed)).detach(); \
  if(stmt_future.wait_for(std::chrono::seconds(secs)) == std::future_status::timeout){ \
    GTEST_FATAL_FAILURE_("       timed out (> " #secs \
    " seconds). Check code for infinite loops"); \
    MPI_Finalize();\
    } \
}

template<typename T>
bool runAndReturn(cl::CommandQueue &queue, cl::Kernel &kernel, cl::Buffer &check, T exp)
{
    //only rank 0 and the recv rank start the app kernels
    MPI_Barrier(MPI_COMM_WORLD);

    queue.enqueueTask(kernel);

    queue.finish();

    MPI_Barrier(MPI_COMM_WORLD);
    //all the rank must have the same value

    T res;
    queue.enqueueReadBuffer(check,CL_TRUE,0,sizeof(T),&res);
    return res==exp;

}

TEST(Gather, MPIinit)
{
    ASSERT_EQ(rank_count,8);
}



TEST(Gather, IntegerMessages)
{
    //with this test we evaluate the correcteness of integer messages transmission

    cl::Kernel kernel;
    cl::CommandQueue queue;
    IntelFPGAOCLUtils::createCommandQueue(context,device,queue);
    IntelFPGAOCLUtils::createKernel(program,"test_int",kernel);

    cl::Buffer check(context,CL_MEM_WRITE_ONLY,sizeof(int));
    std::vector<int> starts={1,100,300};
    int runs=2;
    for(int start:starts)    //consider different roots
    {
        kernel.setArg(0,sizeof(int),&start);
        kernel.setArg(1,sizeof(SMI_Comm),&comm);
        kernel.setArg(2,sizeof(cl_mem),&check);

        for(int i=0;i<runs;i++)
        {
            if(my_rank==0)  //remove emulated channels
                system("rm emulated_chan* 2> /dev/null;");

            //source https://github.com/google/googletest/issues/348#issuecomment-492785854
            ASSERT_DURATION_LE(10, {
              ASSERT_TRUE(runAndReturn<int>(queue,kernel,check,start+7));
            });

        }

    }
}



int main(int argc, char *argv[])
{
//        std::cerr << "Usage: [env CL_CONTEXT_EMULATOR_DEVICE_INTELFPGA=8 mpirun -np 8 " << argv[0] << " <fpga binary file>" << std::endl;

    int result = 0;

    ::testing::InitGoogleTest(&argc, argv);
    //delete listeners for all the rank except 0
    if(argc==2)
        program_path =argv[1];
    else
        program_path = "emulator_<rank>/mixed.aocx";
    ::testing::TestEventListeners& listeners =
            ::testing::UnitTest::GetInstance()->listeners();
    CHECK_MPI(MPI_Init(&argc, &argv));

    CHECK_MPI(MPI_Comm_size(MPI_COMM_WORLD, &rank_count));
    CHECK_MPI(MPI_Comm_rank(MPI_COMM_WORLD, &my_rank));
    if (my_rank!= 0) {
        delete listeners.Release(listeners.default_result_printer());
    }

    //create environemnt
    int fpga=my_rank%2;
       program_path = replace(program_path, "<rank>", std::to_string(my_rank));
    comm=SmiInit_mixed(my_rank, rank_count, program_path.c_str(), ROUTING_DIR, platform, device, context, program, fpga,buffers);


    result = RUN_ALL_TESTS();
    MPI_Finalize();

    return result;

}
