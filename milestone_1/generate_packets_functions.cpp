
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>  // For ceil function

using namespace std;

struct EthernetConfig {
    int lineRate;           // Gbps
    int captureSizeMs;      // Capture duration in milliseconds
    int minNumOfIFGs;       // Minimum number of IFGs per packet
    string destAddress;     // Destination MAC address
    string srcAddress;      // Source MAC address
    int maxPacketSize;      // Max packet size in bytes
    int burstSize;          // Number of packets per burst
    int burstPeriodicity;   // Burst periodicity in microseconds
};
// CRC32 table for Ethernet
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, /* and so on, complete with 256 values */
    // You can fill this table with the full CRC32 values
};

// Function to calculate CRC32
uint32_t calculateCRC32(const vector<string>& packet) {
    uint32_t crc = 0xFFFFFFFF; // Start with all bits set

    // Iterate over all packet segments
    for (const auto& segment : packet) {
        for (size_t i = 0; i < segment.length(); i += 2) {
            // Convert the current 2 hex characters into a byte (8 bits)
            uint8_t byte = stoi(segment.substr(i, 2), nullptr, 16);

            // Update CRC32 using the current byte
            uint8_t index = (crc ^ byte) & 0xFF;
            crc = (crc >> 8) ^ crc32_table[index];
        }
    }

    // Finalize the CRC
    crc ^= 0xFFFFFFFF;
    return crc;
}


EthernetConfig readConfig(const string& configFilePath) {
    ifstream configFile(configFilePath);
    EthernetConfig config;
    string line;

    if (configFile.is_open()) {
        while (getline(configFile, line)) {
            istringstream iss(line);
            string key;
            if (line.find("Eth.LineRate") != string::npos) {
                iss >> key >> config.lineRate;
            }
            else if (line.find("Eth.CaptureSizeMs") != string::npos) {
                iss >> key >> config.captureSizeMs;
            }
            else if (line.find("Eth.MinNumOfIFGsPerPacket") != string::npos) {
                iss >> key >> config.minNumOfIFGs;
            }
            else if (line.find("Eth.DestAddress") != string::npos) {
                iss >> key >> config.destAddress;
            }
            else if (line.find("Eth.SourceAddress") != string::npos) {
                iss >> key >> config.srcAddress;
            }
            else if (line.find("Eth.MaxPacketSize") != string::npos) {
                iss >> key >> config.maxPacketSize;
            }
            else if (line.find("Eth.BurstSize") != string::npos) {
                iss >> key >> config.burstSize;
            }
            else if (line.find("Eth.BurstPeriodicity_us") != string::npos) {
                iss >> key >> config.burstPeriodicity;
            }
        }
        configFile.close();
    }
    else {
        cerr << "Error opening config file." << endl;
    }

    return config;
};
vector<string> generatePacket(const EthernetConfig& config) {
    vector<string> packet;
    packet.push_back("FB555555555555D5");  // Preamble + SFD (8 bytes)
    packet.push_back(config.destAddress);  // Destination MAC (6 bytes)
    packet.push_back(config.srcAddress);   // Source MAC (6 bytes)
    packet.push_back("0800");              // EtherType (IPv4 as example) (2 bytes)

    // Calculate how much payload space we have left in the packet
    int payloadSize = config.maxPacketSize - (8 + 6 + 6 + 2 + 4); // Exclude Preamble, MACs, EtherType, and CRC
    string payload = string(payloadSize * 2, '0'); // Fill with zeros, each byte represented by 2 hex chars
    packet.push_back(payload);              // Dummy payload
    // Calculate the CRC for the current packet (excluding CRC itself)
    uint32_t crc = calculateCRC32(packet);

    // Convert the CRC32 result to a string and append it to the packet
    stringstream crcStream;
    crcStream << hex << setw(8) << setfill('0') << crc;
    packet.push_back(crcStream.str());      // CRC (4 bytes)

    return packet;

}
double calculatePacketTime(const EthernetConfig& config) {
    int packetSizeBytes = config.maxPacketSize;
    // Convert packet size to bits and calculate transmission time
    double packetTime = (packetSizeBytes * 8.0) / (config.lineRate * pow(10,15)); // in microseconds
    return packetTime;
}
void writePacketsToFile(const string& outputFilePath, const EthernetConfig& config) {
    ofstream outputFile(outputFilePath);
    int totalBursts = (config.captureSizeMs * 1000) / config.burstPeriodicity;
    int packetCount = 0;
    vector<string> packet;

    // Calculate the time for one packet
    double packetTime = calculatePacketTime(config);
    double totalBurstTime = packetTime * config.burstSize;

    if (outputFile.is_open()) {
        for (int burst = 0; burst < totalBursts; ++burst) {
            for (int pkt = 0; pkt < config.burstSize; ++pkt) {
                packet = generatePacket(config);

                string currentLine = "";  // Buffer to hold data to be written in 4-byte chunks

                for (const auto& segment : packet) {
                    string segmentData = segment; // Working on each packet segment

                    while (!segmentData.empty()) {
                        if (currentLine.length() < 8) { // If the current line has less than 4 bytes (8 hex characters)
                            int toTake = min(8 - currentLine.length(), segmentData.length());
                            currentLine += segmentData.substr(0, toTake);
                            segmentData.erase(0, toTake);
                        }

                        if (currentLine.length() == 8) { // Write the 4-byte chunk if it's ready
                            outputFile << currentLine << endl;
                            currentLine = ""; // Reset for the next line
                        }
                    }
                }

                // If there's any leftover in the current line buffer (less than 4 bytes)
                if (!currentLine.empty()) {
                    outputFile << currentLine << endl;
                }

                packetCount++;

                // Write IFGs (Inter Frame Gap), ensuring 4-byte alignment
                for (int ifg = 0; ifg < float(config.minNumOfIFGs / 4.0 ); ++ifg) {
                    outputFile << "07070707" << endl;  // 4-byte aligned IFG
                }
            }

            // Calculate the remaining time in the burst periodicity for IFGs
            double remainingTime = config.burstPeriodicity - totalBurstTime;

            // Add IFGs for the remaining time
            int ifgsNeeded = static_cast<int>(ceil((remainingTime * config.lineRate * pow(10,3) ) / 32 ));
            for (int i = 0; i < ifgsNeeded; ++i) {
                outputFile << "07070707" << endl;
            }
        }
        outputFile.close();
        cout << "Total Packets Sent: " << packetCount << endl;
    } else {
        cerr << "Error opening output file." << endl;
    }
}


