#include <cassert>
#include <cstring>
#include <string>
#include <cstdint>
// Expose counters only to exercise the 32-bit wrap without writing 4 GiB.
#define private public
#include "../source/ESP32-S3/V1.8/debug_log_buffer.h"
#undef private
int main() {
  DebugLogBuffer<8> buffer;
  char output[9] = {}; uint32_t cursor=0; bool lost=false;
  assert(buffer.read(cursor,true,output,8,lost)==0 && !lost);
  buffer.append("abc",3);
  assert(buffer.read(cursor,false,output,2,lost)==2 && !lost);
  assert(std::string(output,2)=="ab" && cursor==2);
  assert(buffer.read(cursor,false,output,8,lost)==1 && output[0]=='c');
  assert(buffer.read(cursor,false,output,8,lost)==0);
  buffer.append("0123456789",10);
  assert(buffer.read(cursor,false,output,8,lost)==8 && lost);
  assert(std::string(output,8)=="23456789");
  uint32_t secondClient=0;
  assert(buffer.read(secondClient,true,output,8,lost)==8 && !lost);
  assert(std::string(output,8)=="23456789");
  cursor+=100; // Invalid/future client cursor recovers to retained text.
  assert(buffer.read(cursor,false,output,8,lost)==8 && lost);
  DebugLogBuffer<8> rollover;
  rollover.end_=UINT32_MAX-2;
  rollover.append("abcd",4);
  cursor=UINT32_MAX-2;
  assert(rollover.read(cursor,false,output,8,lost)==4 && !lost);
  assert(std::string(output,4)=="abcd" && cursor==1);
  rollover.append("efghijkl",8);
  assert(rollover.read(cursor,false,output,8,lost)==8 && !lost);
  assert(std::string(output,8)=="efghijkl");
}
