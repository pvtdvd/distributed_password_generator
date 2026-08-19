//Author: Davide Pivato
//Implement the worker component for distributed password dictionary generation software
#include <iostream>
#include <zmq.hpp>
#include <string>
using namespace std;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Declare function prototypes
string index_to_password(uint64_t index, const string& charset, int min_len, int max_len);
string pattern_to_password(uint64_t index, const string& pattern, const string& charset);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Main
int main(int argc, char* argv[])
{
    if (argc < 10)
    {
        cerr << "Usage: " << argv[0] << " <worker_name> <master_ip> <master_port> <charset> <min_len> <max_len> <start> <end> <mode> [pattern]" << endl;
        return 1;
    }

    string worker_name = argv[1];
    string master_ip   = argv[2];
    int master_port    = stoi(argv[3]);
    string charset     = argv[4];
    int min_len        = stoi(argv[5]);
    int max_len        = stoi(argv[6]);
    uint64_t start     = stoull(argv[7]);
    uint64_t end       = stoull(argv[8]);
    int mode           = stoi(argv[9]);  //0 = standard, 1 = pattern

    string pattern;
    if (mode == 1 && argc == 11) pattern = argv[10];

    zmq::context_t ctx(1);
    zmq::socket_t socket(ctx, ZMQ_PUSH);
    socket.connect("tcp://" + master_ip + ":" + to_string(master_port));

    const size_t BATCH_SIZE = 100000;

    uint64_t index = start;

    while (index <= end)
    {
        string msg;
        msg.reserve(20 + BATCH_SIZE * 12);

        msg += (worker_name + ":");

        size_t count = 0;

        while (count < BATCH_SIZE && index <= end)
        {
            string pwd;

            if (mode == 0) pwd = index_to_password(index, charset, min_len, max_len);
            else pwd = pattern_to_password(index, pattern, charset);

            msg.append(pwd);
            msg.push_back('\n');

            index++;
            count++;
        }
        socket.send(zmq::buffer(msg), zmq::send_flags::none);
    }
    socket.send(zmq::buffer("END:" + worker_name), zmq::send_flags::none);

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Convert a numeric index to a password (standard mode)
string index_to_password(uint64_t index, const string& charset, int min_len, int max_len)
{
    uint64_t n = charset.size();
    uint64_t offset = 0;

    for (int len = min_len; len <= max_len; len++)
    {
        uint64_t count = 1;
        for (int i = 0; i < len; i++) count *= n;

        if (index < offset + count)
        {
            uint64_t rel = index - offset;

            string pwd;
            pwd.resize(len);

            for (int i = len - 1; i >= 0; i--)
            {
                pwd[i] = charset[rel % n];
                rel /= n;
            }
            return pwd;
        }

        offset += count;
    }

    return "";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Convert a numeric index to a password (pattern mode)
string pattern_to_password(uint64_t index, const string& pattern, const string& charset)
{
    string result = pattern;
    size_t n = charset.size();

    for (int i = result.length() - 1; i >= 0; i--)
    {
        if (result[i] == '*')
        {
            result[i] = charset[index % n];
            index /= n;
        }
    }
    return result;
}
