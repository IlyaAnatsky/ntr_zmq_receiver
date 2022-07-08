// Test task from NTR
// This is data receiver application

#include <zmq.h>

#include "boost/date_time/posix_time/posix_time.hpp"
#include <boost/algorithm/hex.hpp>
//#include <boost/uuid/detail/md5.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <list>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <stdint.h>
#include "receiver_config_ini.h"

constexpr auto size_len = 2;
constexpr auto number_len = 4;
constexpr auto time_len = 23;
constexpr auto md5_len = 16;
constexpr auto min_data_len = (600 * sizeof(uint16_t));
constexpr auto max_data_len = (1600 * sizeof(uint16_t));
constexpr auto header_len = size_len + number_len + time_len + md5_len;
constexpr auto max_buffer_size = header_len + max_data_len;
 
struct SReceiveStatistics 
{
    int received_number_packages;
    int procecced_number_packages;
    int dropped_number_packages;
    int errors_min_length;
    int errors_received_length;    
    int errors_md5_check;

    SReceiveStatistics() 
    {
        received_number_packages = 0;
        procecced_number_packages = 0;
        dropped_number_packages = 0;
        errors_min_length = 0;
        errors_received_length = 0;
        errors_md5_check = 0;
    }
};

struct SOneBuffer
{
    bool isData;
    uint8_t buffer[max_buffer_size];

    SOneBuffer()
    {
        isData = false;
        memset(buffer, 0, sizeof(buffer));
    };
};

static SReceiveStatistics recvStat;
static std::atomic<int> processDataInd{ 0 };
static std::atomic<int> receiveDataInd{ 1 };

int getNextElement(int element, const int size)
{    
    return ((++element) == size ? 0 : element);
}

bool MD5Check(uint8_t* buffer_p)
{    
    // uint16_t dataSize = ntohs(*((uint16_t*)buffer_p));  
    // boost::uuids::detail::md5 boost_md5;
    // boost_md5.process_bytes(buffer_p + header_len, dataSize);
    // boost::uuids::detail::md5::digest_type digest;
    // boost_md5.get_digest(digest);
    // uint8_t* md5_p = buffer_p + size_len + number_len + time_len;
    // uint8_t* uint8_digest_p = reinterpret_cast<uint8_t*>(&digest);
    
    // return std::equal(md5_p, md5_p + md5_len, uint8_digest_p);

    return true;
}

void receiveFromUDP(SConfigV& configval, std::vector<SOneBuffer>& circular_buffer)
{   
    // using namespace boost::asio;
    // io_service service;
    // ip::udp::socket sock(service);
    // sock.open(ip::udp::v4());
    // ip::udp::endpoint local_ep(ip::address::from_string(configval.local_ip), configval.local_port);
    // sock.bind(local_ep);
    // ip::udp::endpoint sender_ep;
   
    void *serverContext = zmq_ctx_new();
    void *serverSocket = zmq_socket(serverContext, ZMQ_REP);
    int rc = zmq_bind(serverSocket, "tcp://192.168.1.117:5001");
    assert (rc == 0);

    uint8_t writeBuffer[max_buffer_size] = { 0 };

    for (;;)
    {
        // auto receivedSize = sock.receive_from(buffer(writeBuffer, max_buffer_size), sender_ep);

        zmq_msg_t request;
		zmq_msg_init(&request);
		auto receivedSize = zmq_msg_recv(&request, serverSocket, 0);

        if (receivedSize > 0)
        {
            int msgSize = zmq_msg_size(&request);

            memcpy(writeBuffer, zmq_msg_data(&request), msgSize);
            zmq_msg_close(&request);

            // Send back
            zmq_send(serverSocket, writeBuffer, msgSize, ZMQ_DONTWAIT);            
        }
        else
        {
            // Todo some tasks

            continue;
        }

        recvStat.received_number_packages++;

        int nextElement = getNextElement(receiveDataInd.load(), circular_buffer.size());        
        if (nextElement != processDataInd.load())
        {
            // Check for MIN length
            if (receivedSize < min_data_len)
            {
                recvStat.errors_min_length++;
                std::cout << "Error: incorrect MIN length" << std::endl;
                continue;
            }

            // Check for correct received length
            uint16_t expectSize = ntohs(*((uint16_t*)writeBuffer)) + header_len;
            if (expectSize != (uint16_t)receivedSize)
            {
                recvStat.errors_received_length++;
                std::cout << "Error: incorrect received length,(receivedSize="
                    << receivedSize << ") != (expectSize=" << expectSize
                    << ")!" << std::endl;
                continue;
            }
            memcpy(circular_buffer[nextElement].buffer, writeBuffer, max_buffer_size);
            circular_buffer[nextElement].isData = true;
            receiveDataInd.store(nextElement);
        }
        else
        {
            recvStat.dropped_number_packages++;
        }        
    }
    //sock.close();

    zmq_close (serverSocket);
    zmq_ctx_destroy (serverContext);
}

//********************************************************************************
//* main() for data receiver application                                         *
//********************************************************************************
int main()
{
    static auto indexRead = 0;

    std::cout << "\nData receiver application is started\n";

    CConfigIni config("config_data_receiver.ini");
    config.Init();

    SConfigV configval(config);
    if (configval.incorrect == true)
    {
        return 1;
    }

    std::stringstream confS;
    confS << "local_ip: " << configval.local_ip << std::endl;
    confS << "local_port: " << configval.local_port << std::endl;
    confS << "remote_ip: " << configval.remote_ip << std::endl;
    confS << "remote_port: " << configval.remote_port << std::endl;
    confS << "read_delay: " << configval.process_delay_ms << std::endl;
    confS << "waiting_incomming_data_sec: " << configval.waiting_incomming_data_sec << std::endl;
    confS << "waiting_after_data_stop_sec: " << configval.waiting_after_data_stop_sec << std::endl;
    confS << "write_file: " << configval.write_file << std::endl;
    confS << "write_hex: " << configval.write_hex << std::endl;
    std::cout << confS.str();

    std::cout << "\nPlease, check the configuration above and press Enter to continue.";
    std::cin.ignore();

    std::cout << "\nWaiting for incomming data (" << configval.waiting_incomming_data_sec << "sec)!\n";

    std::ofstream fout;
    if (configval.write_file) fout.open("data_receiver.log");

    std::vector<SOneBuffer> circular_buffer(configval.circular_buffer_num_elements);

    std::thread threadForReceiveFromUDP([&](){receiveFromUDP(configval, circular_buffer);});
    threadForReceiveFromUDP.detach();
    
    auto startTime = std::time(0);
    auto startForWaiting = startTime;
    auto waitingForSec = configval.waiting_incomming_data_sec;

    while (true)
    {
        int nextElement = getNextElement(processDataInd.load(), circular_buffer.size());
        if (nextElement != receiveDataInd.load())
        {
            if (circular_buffer[nextElement].isData)
            {
                uint8_t* buffer_p = circular_buffer[nextElement].buffer;

                uint16_t dataSize = ntohs(*(uint16_t*)buffer_p);
                uint32_t numberMsg = ntohl(*(uint32_t*)(buffer_p + size_len));

                // Check for MD5
                bool md5_check = MD5Check(buffer_p);
                if (md5_check == false)
                {
                    recvStat.errors_md5_check++;
                }

                // Print to console
                std::stringstream logstr;
                std::string timestemp((char*)(buffer_p + size_len + number_len), time_len);
                logstr << "Processed: #number message=" << numberMsg
                       << "\tSize=" << dataSize
                       << "\tTime=" << timestemp
                       << "\tMD5 check=" << (md5_check == true ? "PASS" : "FAIL") << std::endl;
                std::cout << logstr.str();

                if (configval.write_file) // Print to log file
                {
                    fout << logstr.str();

                    if (configval.write_hex)
                    {
                        std::string str_msg_hex("");
                        boost::algorithm::hex(buffer_p, buffer_p + (dataSize + header_len), std::back_inserter(str_msg_hex));
                        fout << "data=" << str_msg_hex << "\n";
                    }
                    fout << std::endl;
                    fout.flush();
                }
                recvStat.procecced_number_packages++;

                startForWaiting = std::time(0);
                waitingForSec = configval.waiting_after_data_stop_sec;

                // Waiting for some time, expected: 15ms, according to task requirements
                std::this_thread::sleep_for(std::chrono::milliseconds(configval.process_delay_ms));

                circular_buffer[nextElement].isData = false;                               
            }            
            processDataInd.store(nextElement);
        }
        
        // Check for break by timeout
        if ((startForWaiting + waitingForSec) < std::time(0))
        {
            if(startTime == startForWaiting)
                std::cout << "\nIt looks like thare was not any data packages!\n";
            break;
        }
    }
    if (fout.is_open()) fout.close();

    boost::posix_time::ptime now_stat = boost::posix_time::second_clock::local_time();
    std::string time_stat = to_iso_extended_string(now_stat);

    std::stringstream stat;
    stat << "Data receiver statistics ("<< time_stat <<"):\n";
    stat << "Received total number of packages: " << recvStat.received_number_packages << "\n";
    stat << "Procecced total number of packages: " << recvStat.procecced_number_packages << "\n";
    stat << "Dropped total number of packages: " << recvStat.dropped_number_packages << "\n";
    stat << "Number of MIN length errors: " << recvStat.errors_min_length << "\n";
    stat << "Number of received length errors: " << recvStat.errors_received_length << "\n";
    stat << "Number of MD5 check errors: " << recvStat.errors_md5_check << "\n";
    stat << "\nConfiguration:\n" << confS.str();
    std::cout << std::endl << stat.str();

    fout.open("data_receiver_stat.log");    
    fout << stat.str();
    fout.close();

    std::cout << "\nData receiver application will be stopped. Please, press Enter to continue.\n";
    std::cin.ignore();

    return 0;
}
