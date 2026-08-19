//Author: Davide Pivato
//Implement the master component for distributed password dictionary generation software
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <iostream>
#include <zmq.hpp>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <string>
#include <vector>
using namespace std;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Define the structure to manage worker nodes
struct Worker
{
    string name, ip, user;
    uint64_t start_index;
    uint64_t end_index;
    uint64_t combinations;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Declare function prototypes
int read_int(const string& prompt, int min_value);
string read_valid_charset();
string read_pattern();
uint64_t calculate_total_combinations(const string& charset, int min_length, int max_length);
void assign_worker_ranges(vector<Worker>& workers, uint64_t total);
void sending_exe_via_scp(const vector<Worker>& workers);
void starting_workers_via_ssh(const vector<Worker>& workers, const string& master_ip, int master_port, const string& charset, int min_len, int max_len, int mode, const string& pattern = "");
void removing_exe_via_ssh(const vector<Worker>& workers);
void receive_from_workers(int master_port, const vector<Worker>& workers, uint64_t total_combinations);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Main
int main()
{
	//Delete previously generated files
    filesystem::remove_all("password_worker");
    filesystem::create_directory("password_worker");

    //Prompt the user to select the generation mode
    int mode;
    cout << "What do you want to generate?\n";
    cout << "1) All combinations (min/max length)\n";
    cout << "2) Pattern-based (replace * with charset)\n";
    cout << "Choice: ";

    cin >> mode;
    cin.ignore(numeric_limits<streamsize>::max(), '\n');

    if (mode != 1 && mode != 2)
    {
        cerr << "Error: invalid choice\n";
        exit(EXIT_FAILURE);
    }

    string charset = read_valid_charset();

    int min_len = 1, max_len = 1;
    string pattern;
    uint64_t total = 0;

    if (mode == 1)
    {
        min_len = read_int("Enter minimum length: ", 1);
        max_len = read_int("Enter maximum length: ", min_len);
        auto start_time = chrono::high_resolution_clock::now();
        total = calculate_total_combinations(charset, min_len, max_len);
    }
    else
    {
        pattern = read_pattern();
        auto start_time = chrono::high_resolution_clock::now();
        int wildcards = 0;
        for (char c : pattern)
        {
            if (c == '*') wildcards++;
        }

        total = 1;
        for (int i = 0; i < wildcards; i++) total *= charset.size();
    }

    //Load and parse the JSON configuration
    ifstream f("cluster.json");
    if (!f)
    {
        cerr << "Error opening cluster.json\n";
        exit(EXIT_FAILURE);
    }
    nlohmann::json j;
    f >> j;
    string master_ip = j["master"]["ip"];
    int master_port = j["master"]["port"];
    vector<Worker> workers;
    for (auto& w : j["workers"]) workers.push_back({w["name"], w["ip"], w["user"]});

	//Display initial information and start the program
    cout << "\n=== STARTING INFO ===" << endl;
    cout << "Master: "<< master_ip << ":" << master_port << endl;
    cout << "Charset: " << charset << endl;
    if (mode == 1)
    {
        cout << "Mode: All combinations" << endl;
        cout << "Minimum length: " << min_len << endl;
        cout << "Maximum length: " << max_len << endl;
    }
    else
    {
        cout << "Mode: Pattern-based" << endl;
        cout << "Pattern: " << pattern << endl;
        cout << "Wildcards: " << count(pattern.begin(), pattern.end(), '*') << endl;
    }
    cout << "Number of workers: " << workers.size() << endl;
    cout << "Total combinations: " << total << endl;

    //Assign ranges to worker nodes
    assign_worker_ranges(workers, total);

    //Display the assigned ranges
    for (const auto& w : workers) cout << w.name << ": [" << w.start_index << " - " << w.end_index << "] (" << w.combinations << " combinations)" << endl;

    //Send the executable to worker nodes via SCP
    sending_exe_via_scp(workers);

    //Start worker nodes via SSH in the background using the selected mode
    if (mode == 1) starting_workers_via_ssh(workers, master_ip, master_port, charset, min_len, max_len, mode);
    else starting_workers_via_ssh(workers, master_ip, master_port, charset, 0, 0, mode, pattern);

    //Receive passwords from worker nodes and write them to separate output filess
    receive_from_workers(master_port, workers, total);

    //Remove executables from worker nodes via SSH
    removing_exe_via_ssh(workers);

    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
    cout << "\nTotal processing time: " << duration.count() / 1000.0 << " seconds\n\n";

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Read min_length and max_length
int read_int(const string& prompt, int min_value)
{
    int value;

    while (true)
    {
        cout << prompt;
        cin >> value;

        if (cin.fail())
        {
            cout << "Error: you must enter a number\n";
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }
        else if (value < min_value)
        {
            cout << "Error: value must be >= " << min_value << "\n";
            continue;
        }

        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        return value;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Read the charset
string read_valid_charset()
{
    string charset;
    unordered_set<char> seen;

    while (true)
    {
        cout << "Enter the charset (no duplicate characters, no spaces): ";
        getline(cin, charset);

        if (charset.empty())
        {
            cout << "Error: charset cannot be empty\n";
            continue;
        }

        seen.clear();
        bool invalid = false;

        for (char c : charset)
        {
            if (c == ' ')
            {
                cout << "Error: spaces are not allowed in the charset\n";
                invalid = true;
                break;
            }
            else if (!seen.insert(c).second)
            {
                cout << "Error: charset contains duplicate characters\n";
                invalid = true;
                break;
            }
        }
        if (invalid) continue;

        return charset;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Read the pattern
string read_pattern()
{
    string pattern;

    while (true)
    {
        cout << "Enter the pattern (use * for wildcards, e.g., ciao**2**): ";
        getline(cin, pattern);

        if (pattern.empty())
        {
            cout << "Error: pattern cannot be empty\n";
            continue;
        }

        bool has_asterisk = false;
        for (char c : pattern)
        {
            if (c == '*')
            {
                has_asterisk = true;
                break;
            }
        }

        if (!has_asterisk)
        {
            cout << "Error: pattern must contain at least one asterisk (*)\n";
            continue;
        }

        return pattern;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Calculate the total number of combinations
uint64_t calculate_total_combinations(const string& charset, int min_length, int max_length)
{
    uint64_t total = 0;
    uint64_t n = charset.size();

    for (int len = min_length; len <= max_length; len++)
    {
        uint64_t pow_val = 1;
        for (int i = 0; i < len; i++) pow_val = pow_val * n;
        total = total + pow_val;
    }

    return total;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Assign work ranges to worker nodes
void assign_worker_ranges(vector<Worker>& workers, uint64_t total)
{
    if (workers.empty()) return;

    uint64_t base = total / workers.size();
    uint64_t remainder = total % workers.size();
    uint64_t current_index = 0;

    for (size_t i = 0; i < workers.size(); ++i)
    {
        uint64_t count = base;
        if (i < remainder) count++;

        workers[i].start_index = current_index;
        workers[i].end_index = current_index + count - 1;
        workers[i].combinations = count;

        current_index += count;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Send the executable to worker nodes via SCP
void sending_exe_via_scp(const vector<Worker>& workers)
{
    cout << "\n=== COPYING WORKER.EXE VIA SCP ===\n";

    for (size_t i = 0; i < workers.size(); ++i)
    {
        string command = "scp -q -i ~/.ssh/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR worker " + workers[i].user + "@" + workers[i].ip + ":/tmp";
        cout << "Copying .exe via scp on " << workers[i].name << " (" << workers[i].ip << ")...";

        int result = system(command.c_str());
        if (result == 0) cout << "SUCCEEDED\n";
        else
        {
            cerr << "FAILED (code: " << result << ")\n";
            exit(EXIT_FAILURE);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Start worker nodes via SSH
void starting_workers_via_ssh(const vector<Worker>& workers, const string& master_ip, int master_port, const string& charset, int min_len, int max_len, int mode, const string& pattern)
{
    cout << "\n=== STARTING WORKER VIA SSH ===\n";

    for (size_t i = 0; i < workers.size(); ++i)
    {
        string command;
        if (mode == 1) command = "ssh -i ~/.ssh/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR " + workers[i].user + "@" + workers[i].ip + " \"cd /tmp && ./worker " + workers[i].name + " " + master_ip + " " + to_string(master_port) + " " + charset + " " + to_string(min_len) + " " + to_string(max_len) + " " + to_string(workers[i].start_index) + " " + to_string(workers[i].end_index) + " 0\" &";
        else command = "ssh -i ~/.ssh/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR " + workers[i].user + "@" + workers[i].ip + " \"cd /tmp && ./worker " + workers[i].name + " " + master_ip + " " + to_string(master_port) + " " + charset + " 0 0 " + to_string(workers[i].start_index) + " " + to_string(workers[i].end_index) + " 1 " + pattern + "\" &";

        cout << "Starting " << workers[i].name << " (" << workers[i].ip << ")" << " via ssh...";

        int result = system(command.c_str());
        if (result == 0) cout << "SUCCEEDED\n";
        else cerr << "FAILED (code: " << result << ")\n";
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Remove the executable from worker nodes via SSH
void removing_exe_via_ssh(const vector<Worker>& workers)
{
    cout << "\n=== REMOVING WORKER.EXE VIA SSH ===\n";

    for (size_t i = 0; i < workers.size(); ++i)
    {
        string command = "ssh -i ~/.ssh/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR " + workers[i].user + "@" + workers[i].ip + " \"rm /tmp/worker\"";
        cout << "Removing .exe via ssh on " << workers[i].name << " (" << workers[i].ip << ")...";

        int result = system(command.c_str());
        if (result == 0) cout << "SUCCEEDED\n";
        else cerr << "FAILED (code: " << result << ")\n";
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Receive passwords from workers and write them to separate output files
void receive_from_workers(int master_port, const vector<Worker>& workers, uint64_t total_combinations)
{
    cout << "\n=== STARTING ELABORATION ===\n";

    zmq::context_t ctx(1);
    zmq::socket_t socket(ctx, ZMQ_PULL);
    socket.bind("tcp://*:" + to_string(master_port));

    unordered_map<string, ofstream> output_files;
    unordered_map<string, bool> worker_active;
    int total_workers = workers.size();
    int finished_workers = 0;

    for (const auto& worker : workers)
    {
        string filename = "password_worker/output_" + worker.name + ".txt";
        output_files[worker.name].open(filename);
        if (!output_files[worker.name])
        {
            cerr << "Error opening " << filename << endl;
            return;
        }
        worker_active[worker.name] = true;
    }

    uint64_t total_passwords_received = 0;
    const uint64_t PROGRESS_BATCH = 1000000; //Display progress every 1M passwords

    while (finished_workers < total_workers)
    {
        zmq::message_t msg;
        auto res = socket.recv(msg, zmq::recv_flags::none);

        if (res)
        {
            string message(static_cast<char*>(msg.data()), msg.size());

            if (message.substr(0, 4) == "END:")
            {
                string worker_name = message.substr(4);
                cout << worker_name << " has finished" << endl;

                if (worker_active[worker_name])
                {
                    output_files[worker_name].close();
                    worker_active[worker_name] = false;
                    finished_workers++;
                }
            }
            else
            {
                //È una batch di password
                size_t colon_pos = message.find(':');
                if (colon_pos != string::npos)
                {
                    string worker_name = message.substr(0, colon_pos);
                    string passwords = message.substr(colon_pos + 1);

                    if (worker_active[worker_name]) //Write passwords to the worker output file
                    {
                        output_files[worker_name] << passwords;

                        size_t newline_count = 0;
                        for (char c : passwords)
                        {
                            if (c == '\n') newline_count++;
                        }
                        total_passwords_received += newline_count;

                        if (total_passwords_received % PROGRESS_BATCH == 0 || total_passwords_received == total_combinations) //Progress indicator
                        {
                            double percent = (double)total_passwords_received / total_combinations * 100.0;
                            printf("Progress: %lu/%lu %.2f%%\n", total_passwords_received, total_combinations, percent);
                        }
                    }
                }
            }
        }
    }

    cout << "\n=== ELABORATION COMPLETED ===\n";
    cout << "All of " << total_workers << " worker have finished\n";
    cout << "Total password received: " << total_passwords_received << "\n";
}
