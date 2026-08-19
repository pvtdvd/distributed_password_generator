//Autore: Davide Pivato
//Programma che genera un dizionario di password in locale in modo tale da poter fare i confronti tra calcolo in locale e calcolo distribuito
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
using namespace std;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Funzione per calcolare combinazioni totali (stessa del master)
uint64_t calculate_total_combinations(const string& charset, int min_length, int max_length)
{
    uint64_t total = 0;
    uint64_t n = charset.size();

    for (int len = min_length; len <= max_length; len++)
    {
        uint64_t pow_val = 1;
        for (int i = 0; i < len; i++)
        {
            pow_val = pow_val * n;
        }
        total = total + pow_val;
    }

    return total;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Funzione per convertire da indice numerico a password (stessa del worker)
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
// Main
int main()
{
    auto start_time = chrono::high_resolution_clock::now();
    
    //Parsing del json
    ifstream f("cluster.json");
    if (!f)
    {
        cerr << "Error opening cluster.json"<<endl;
        return 1;
    }
    nlohmann::json j;
    f >> j;
    string charset = j["parameters"]["charset"];
    int min_length = j["parameters"]["min_length"];
    int max_length = j["parameters"]["max_length"];
    
    //Calcola il numero totale di combinazioni
    uint64_t total = calculate_total_combinations(charset, min_length, max_length);
    
    //Crea directory per i risultati
    filesystem::remove_all("Password_Local");
    filesystem::create_directory("Password_Local");
    
    //Apri file di output
    string filename = "Password_Local/passwords.txt";
    ofstream output_file(filename);
    
    if (!output_file)
    {
        cerr << "Errore nell'apertura del file " << filename << endl;
        return 1;
    }
    
    //Parametri di batch (stesso concetto del worker)
    const size_t BATCH_SIZE = 100000;
    uint64_t total_passwords_written = 0;
    
    //Genera tutte le password
    for (uint64_t index = 0; index < total; )
    {
        string batch;
        batch.reserve(BATCH_SIZE * 12);
        
        size_t count = 0;
        
        while (count < BATCH_SIZE && index < total)
        {
            string pwd = index_to_password(index, charset, min_length, max_length);
            batch.append(pwd);
            batch.push_back('\n');
            
            index++;
            count++;
            total_passwords_written++;
        }
        
        //Scrive il batch su file
        output_file << batch;
        output_file.flush();
        
        //Progress indicator
        if (index % (BATCH_SIZE * 10) == 0 || index == total)
        {
            double percent = (double)index / total * 100.0;
            cout << "Progress: " << index << "/" << total << " (" << percent << "%)" << endl;
        }
    }
    
    output_file.close();
    
    auto end_time = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
    
    cout << "\n=== GENERAZIONE COMPLETATA ===" << endl;
    cout << "Password totali generate: " << total_passwords_written << endl;
    cout << "File salvato in: " << filename << endl;
    cout << "Tempo totale: " << duration.count() / 1000.0 << " secondi" << endl;
    
    return 0;
}
