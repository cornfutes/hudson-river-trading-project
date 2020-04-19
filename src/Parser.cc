#include "Parser.h"

#include <string>
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdexcept>
#include <fstream>

Parser::Parser(int date, const std::string &outputFilename)
{
    pos = 1;

    // Empty the file.
    std::ofstream outfile;
    outfile.open("new.txt");
    outfile.close();
}

void Parser::onUDPPacket(const char *buf, size_t len)
{
    printf("Received packet of size %zu\n", len);
    if(static_cast<int>(len) < 4) {
       throw std::runtime_error("Packet must be atleast 4 bytes");
    }

    uint16_t packetSize = 0;
    packetSize = (uint16_t)((buf[0] << 8) | buf[1]);
    printf("Packet size %zu\n", packetSize);
    if(static_cast<int>(packetSize) != static_cast<int>(len)) {
       throw std::runtime_error("Packet size does match buffer length");
    }
    uint16_t sequenceNumber = (uint16_t)((
        buf[2] << 24) | buf[3] << 16 | buf[4] << 8 | buf[5]);
    printf("Sequence number %zu\n", sequenceNumber);

    int payloadLength = static_cast<int>(len) - 6;
    printf("Payload length %zu\n", payloadLength);

    if(sequenceNumber == pos) {
       // Queue current payload.
       for( int i = 6; i < static_cast<int>(len); i++) {
           q.push(buf[i]);
       }
       pos++;

       // Queue previously skipped payloads.
       auto payload = m.find(pos);
       while(payload != m.end()) {
          uint16_t prevPacketSize  = (uint16_t)((buf[0] << 8) | buf[1]);
          for( int i = 6; i < static_cast<int>(prevPacketSize); i++) {
            q.push(buf[i]);
          }
          pos++;
          payload = m.find(pos);
       }

       // There's atleast 1 message that is processable.
       while(q.size() >= 34 ||
            (q.size() >= 33 && q.front() != 0x41) ||
            (q.size() >= 21 && q.front() == 0x58 && q.front() == 0x52))
        {
         char msgType = q.front();
         q.pop();

         bool isAdd = msgType == 0x41;
         bool isExecuted = msgType == 0x45;
         bool isCanceled = msgType == 0x58;
         bool isReplaced = msgType == 0x52;

         if(isAdd) {
           char* in = popNBytes(34 - 1);
           char* out = mapAdd(in);
           // TODO: delete[] vs delete.
           delete in;

           std::ofstream outfile;
           outfile.open("new.txt", std::ios_base::app); // append instead of overwrite
           outfile.write(out, 44);
           outfile.close();

           delete out;
         } else if(isExecuted) {
           char* in = popNBytes(21 - 1);
           char* out = mapExecuted(in);
           delete in;
           std::ofstream outfile;
           outfile.open("new.txt", std::ios_base::app); // append instead of overwrite
           outfile.write(out, 40);
           outfile.close();
           delete out;
         } else if(isCanceled) {
           char* in = popNBytes(21 - 1);
           char* out = mapReduced(in);
           delete in;
           std::ofstream outfile;
           outfile.open("new.txt", std::ios_base::app); // append instead of overwrite
           outfile.write(out, 32);
           outfile.close();
           delete out;
         } else if(isReplaced) {
           char* in = popNBytes(33 - 1);
           char* out = mapReplaced(in);
           delete in;
           std::ofstream outfile;
           outfile.open("new.txt", std::ios_base::app); // append instead of overwrite
           outfile.write(out, 48);
           outfile.close();
           delete out;
         } else {
           throw std::runtime_error("Unexpected message type");
         }
       }
    } else if (sequenceNumber < pos) {
      // Skip, duplicate / already proccessed.
    } else {
      // Store for revisit when the gap in sequence is closed.
      m[sequenceNumber] = buf;
    }
}

char* Parser::mapAdd(char *in) {
  char* out = new char[44];
  // Msg type. Offset 0, length 2.
  out[0] = 0x00;
  out[1] = 0x01;
  // Msg size. Offset 2, length 2.
  out[2] = 0x00;
  out[3] = 0x2C;
  // Stock Ticker. Offset 4, length 8.
  // Replace alphanumeric ASCII space with null.
  out[4] = in[22 - 1] == 0x20 ? 0x00: in[22 -1 ];
  out[5] = in[23 - 1] == 0x20 ? 0x00 : in[23 - 1];
  out[6] = in[24 - 1] == 0x20 ? 0x00: in[24 - 1];
  out[7] = in[25 - 1] == 0x20 ? 0x00: in[25 - 1];
  out[8] = in[26 - 1] == 0x20 ? 0x00: in[26 - 1];
  out[9] = in[27 - 1] == 0x20 ? 0x00: in[27 - 1];
  out[10] = in[28 - 1] == 0x20 ? 0x00: in[28 - 1];
  out[11] = in[29 - 1] == 0x20 ? 0x00: in[29 -1 ];
  // Timestamp. Offset 12, length 8.
  // Order reference number. Offset 20, length 8.
  out[20] = in[16 - 1];
  out[21] = in[15 - 1];
  out[22] = in[14 - 1];
  out[23] = in[13 - 1];
  out[24] = in[12 - 1];
  out[25] = in[11 - 1];
  out[26] = in[10 - 1];
  out[27] = in[9 - 1];
  // Side. Offset 28, length 1.
  out[28] = in[17 - 1];
  // Padding. Offset 29, length 3.
  out[29] = 0x00;
  out[30] = 0x00;
  out[31] = 0x00;
  // Size. Offset 32, length 4.
  out[32] = in[21 - 1];
  out[33] = in[20 - 1];
  out[34] = in[19 - 1];
  out[35] = in[18 - 1];

  return out;
}

char* Parser::mapExecuted(char *in) {
  char* out = new char[40];
  // Msg type. Offset 0, length 2.
  out[0] = 0x00;
  out[1] = 0x02;
  // Msg size. Offset 2, length 2.
  out[2] = 0x00;
  out[3] = 0x28;
  // TODO: stock ticker. offset 4, length 8. Lookup from order reference number.
  // TODO: timestamp. offset 12, length 8. map.
  // Order reference number. Offset 20, length 8.
  out[20] = in[16-1];
  out[21] = in[15-1];
  out[22] = in[14-1];
  out[23] = in[13-1];
  out[24] = in[12-1];
  out[25] = in[11-1];
  out[26] = in[10-1];
  out[27] = in[9-1];
  // Size. Offset 28, length 4.
  out[28] = in[20-1];
  out[29] = in[19-1];
  out[30] = in[18-1];
  out[31] = in[17-1];
  // TOOD: price. offset 32, length 8. map floating point. lookup from order ref.
  return out;
}

char* Parser::mapReduced(char* in) {
  char* out = new char[32];
  // Msg type. Offset 0, length 2.
  out[0] = 0x00;
  out[1] = 0x03;
    // Msg size. Offset 2, length 2.
  out[2] = 0x00;
  out[3] = 0x20;

  // Stock ticker. Offset 4, length 8.
  // TODO use reference number to lookup.

  // Timestamp, offset 12, length 8.
  // TODO: map from *in.

  // Order ref number. Offset 20, length 8.
  out[20] = in[16-1];
  out[21] = in[15-1];
  out[22] = in[14-1];
  out[23] = in[13-1];
  out[24] = in[12-1];
  out[25] = in[11-1];
  out[26] = in[10-1];
  out[27] = in[9-1];
  // Size remaining. Offset 28, length 4.
  // TODO: do substraction from ord er ref - *in.
  // TODO: watchout for endianness.
  // out[28] = 
  // out[29] = 
  // out[30] = 
  // out[31] = 
}

char* Parser::mapReplaced(char *in) {
  char* out = new char[48];
  // Msg type. Offset 0, length 2.
  out[0] = 0x00;
  out[1] = 0x04;
  // Msg size. Offset 2, length 2.
  out[2] = 0x00;
  out[3] = 0x30;
  
  // TODO: lookup old order reference.
  // TODO: stock ticker, offset 4, length 8. lookup from old order reference.
  // TODO: timestamp. offset 12, length 8, map timestamp from *in.
  // TODO: old order reference. offset 20, length 8, big->little endian mapping
  // New order reference number. offset 28, length 8.
  out[28] = in[24-1];
  out[29] = in[23-1];
  out[30] = in[22-1];
  out[31] = in[21-1];
  out[32] = in[20-1];
  out[33] = in[19-1];
  out[34] = in[18-1];
  out[35] = in[17-1];
  // New size. Offset 36, length 4.
  out[36] = in[28-1];
  out[37] = in[27-1];
  out[38] = in[26-1];
  out[39] = in[25-1];
  // New price. offset 40, length 8. mapping floating point.

  return out;
}

char* Parser::popNBytes(int n) {
  char* out = new char[n];
  for(int i = 0 ; i < n; i++) {
    out[i] = q.front();
    q.pop();
  }
  return out;
}