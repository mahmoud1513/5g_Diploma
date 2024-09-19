#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>  // For ceil function

#include "generate_packets_headers.h"
using namespace std;


int main() {
    // Hardcoded config file path and output file path
    string configFilePath = "configration_file.txt";
    string outputFilePath = "output_packets.txt";

    // Read the configuration from the file
    EthernetConfig config = readConfig(configFilePath);

    // Write the packets to the output file
    writePacketsToFile(outputFilePath, config);


}

